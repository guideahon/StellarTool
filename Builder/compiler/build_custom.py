"""build_custom — orquestador F1 del Stellar Souls Builder.

Toma respuestas del cuestionario y produce un ZIP instalable personalizado:
  - gameplay: copia el/los pak(s) prebuilt del preset que matchea el combo
    (F1 fallback; F2+ los compilara por feature con TableCompiler).
  - helper: compila config.lua desde las respuestas (helper_compiler).
  - guia: INSTALL_<lang>.txt localizada indicando donde va cada archivo.

Uso:
    python build_custom.py --answers @answers.json --out <dir> [--lang es]

answers.json ej:
    {"combatProfile":"full","outfitSkinSuit":true,"miniBoss":"on",
     "helperMode":"randomPeriodic","helperIntervalSeconds":30,"lang":"es"}
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

# Python embebido no agrega el dir del script a sys.path -> hacerlo explicito
# para que los modulos hermanos (helper_compiler, etc.) importen.
sys.path.insert(0, str(Path(__file__).resolve().parent))

# La app lee nuestra salida como UTF-8; con el locale por defecto (cp936 en
# Windows chino) los acentos o rutas no-ASCII rompen el print.
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

from helper_compiler import compile_helper
import build_specs
import table_compiler
import toolchain
import vanilla_helper

BUILDER_DIR = Path(__file__).resolve().parent.parent
FEATURES = BUILDER_DIR / "features"
INSTALL_TEMPLATES = BUILDER_DIR / "i18n" / "install"

SUPPORTED_LANGS = ["es", "en", "fr", "it", "de", "ja", "ko", "pt_BR", "ru", "zh_Hans"]


def load_json(path: Path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


OUTFIT_MODES = ("off", "helper", "noHelperAlpha")
# Modos del comportamiento del outfit que configuran el helper CNS. El cuarto,
# "lastNoCns", no lo usa: pide el helper vanilla (ALPHA) en su lugar.
CNS_HELPER_MODES = ("last", "randomAny", "randomPeriodic")
DEFAULT_ALPHA = "alpha6"   # el chain: prueba las tres estrategias y se queda con la que repinta
# Respuesta del cuestionario -> factor de vida de la variante de boss (`_OW`).
OVERWORLD_HEALTH = {"quarter": 0.25, "half": 0.5, "threeQuarter": 0.75, "full": 1.0}


def normalize(answers: dict) -> dict:
    """Rellena defaults y normaliza miniBoss a on/off para la clave de preset."""
    a = dict(answers)
    a.setdefault("combatProfile", "full")
    # El swap tiene tres estados; `outfitSkinSuit`/`outfitHelperless` se derivan
    # de ahi y son lo que lee el resto del pipeline. Respuestas viejas traen
    # solo el bool, asi que se acepta como entrada.
    mode = a.get("outfitMode")
    if mode not in OUTFIT_MODES:
        mode = "helper" if a.get("outfitSkinSuit", True) else "off"
    a["outfitMode"] = mode
    a["outfitSkinSuit"] = mode != "off"
    a["outfitHelperless"] = mode == "noHelperAlpha"
    # El FX de campamento no es opcional: el swap lo pisa y dejarlo pisado no le
    # sirve a nadie.
    a["outfitVanillaRestFX"] = True
    # El modo sin helper ES el restore table-side; ya no es un check aparte.
    a["outfitQteRestoreAlpha"] = a["outfitHelperless"]
    a.setdefault("helperMode", "last")
    a.setdefault("miniBoss", "off")
    # Bosses de overworld: la UI manda el dict armado; el cuestionario CLI manda
    # las tres respuestas sueltas. Adentro siempre viaja el dict.
    ow = a.get("overworldBosses")
    if not isinstance(ow, dict):
        ow = {"enabled": bool(ow)}
        ow["pct"] = int(a.get("overworldBossPct", 3) or 0)
        ow["healthFactor"] = OVERWORLD_HEALTH.get(a.get("overworldBossHealth", "half"), 0.5)
    a["overworldBosses"] = ow
    # Build ALPHA del helper vanilla (sin CNS). "off" = no incluirlo.
    a.setdefault("vanillaHelperBuild", "off")
    # "Restaurar ultimo outfit (SIN CNS)" se sirve con el helper vanilla: si no
    # hay ALPHA elegida, va la del chain.
    if uses_vanilla_helper_mode(a) and not vanilla_helper.is_enabled(a["vanillaHelperBuild"]):
        a["vanillaHelperBuild"] = DEFAULT_ALPHA
    # Valor graduable visible en pasos de 10%. Proyectos viejos conservan 60%.
    tumbler = int(round(float(a.get("tumblerHealPercent", 60)) / 10.0) * 10)
    a["tumblerHealPercent"] = max(10, min(100, tumbler))
    # Mundo/economia: tablas fuera del combate (tienda, drops, progresion, pesca).
    a.setdefault("worldTweaks", [])
    a.setdefault("worldTweakValues", {})
    a.setdefault("lang", "es")
    if a["lang"] not in SUPPORTED_LANGS:
        a["lang"] = "es"
    return a


def uses_vanilla_helper_mode(a: dict) -> bool:
    """True si el comportamiento del outfit pedido es el restore SIN CNS."""
    return a.get("outfitMode") == "helper" and a.get("helperMode") == "lastNoCns"


def needs_cns_helper(a: dict) -> bool:
    """True si hay que compilar e instalar StellarSoulsOutfitRestore (CNS)."""
    return a.get("outfitMode") == "helper" and a.get("helperMode") in CNS_HELPER_MODES


def preset_key(a: dict) -> str:
    mb = "off" if a["miniBoss"] in ("off", False, None) else "on"
    outfit = "true" if a["outfitSkinSuit"] else "false"
    return f'{a["combatProfile"]}|{outfit}|{mb}'


def only_vanilla_helper(a: dict) -> bool:
    """True si lo unico pedido es una ALPHA del helper vanilla (sin paks)."""
    mb_on = a.get("miniBoss", "off") not in ("off", False, None)
    return (a.get("combatProfile") == "none" and not a.get("outfitSkinSuit") and not mb_on
            and not build_specs.world_selection(a)
            and vanilla_helper.is_enabled(a.get("vanillaHelperBuild")))


def validate(a: dict) -> None:
    """Rechaza combos sin sentido con mensajes accionables (antes de buscar preset)."""
    combat, outfit, mb = a["combatProfile"], a["outfitSkinSuit"], a["miniBoss"]
    mb_on = mb not in ("off", False, None)
    hardcore_on = a.get("hardcoreEnemyBoost", "off") in ("main", "insane")
    tuning_on = any((a.get(key) or {}).get("enabled") for key in ("harderBosses", "harderEnemies"))
    world_on = bool(build_specs.world_selection(a))
    if combat == "none" and not outfit and not mb_on and not hardcore_on and not tuning_on and not world_on:
        raise SystemExit(
            "[builder] Nada seleccionado: activa combate, outfit, mini-boss o "
            "algun ajuste de mundo (tienda, drops, progresion, pesca).")
    if combat == "firstRun" and not mb_on:
        raise SystemExit("[builder] First Run solo existe con mini-boss activado (miniBoss=on).")
    if combat == "none" and mb_on:
        raise SystemExit("[builder] Mini-boss requiere un perfil de combate (full o firstRun).")


def resolve(a: dict) -> dict:
    """Devuelve {'paks':[...], 'folder':..., 'needsHelper':bool, 'warnings':[...]}."""
    validate(a)
    preset_map = load_json(FEATURES / "preset_map.json")
    key = preset_key(a)
    preset = preset_map["presets"].get(key)
    warnings = []
    if preset is None:
        # Un build que solo pide ajustes de mundo no tiene (ni necesita) preset
        # precompilado: se compila entero desde las tablas vanilla.
        if not a.get("forcePreset") and build_specs.combo_to_targets(a):
            preset = {"folder": "(compilado)", "paks": []}
        else:
            raise SystemExit(
                f"[builder] Combo sin preset F1: {key}. "
                f"Requiere TableCompiler (F2+) o ajustar respuestas. "
                f"Presets: {sorted(preset_map['presets'])}"
            )
    # Solo el fallback precompilado ignora sub-features granulares.
    if a.get("forcePreset") and a.get("miniBossDensity") and a["miniBoss"] != "off":
        warnings.append("densityIgnored")
    if a.get("forcePreset") and a.get("gaugeTweaks"):
        warnings.append("gaugeTweaksIgnored")
    if a.get("forcePreset") and build_specs.world_selection(a):
        warnings.append("worldTweaksIgnored")
    # Los paks precompilados traen los colaterales del swap de outfit tal cual.
    if a.get("forcePreset") and a["outfitSkinSuit"] and build_specs.outfit_fix_extras(a):
        warnings.append("outfitFixesIgnored")
    # Helper base: preferir el vendorizado (Builder autocontenido en Stellar
    # Tool); si no, la ruta de preset_map (dev, apunta a Stellar Souls/Release).
    vendor_helper = BUILDER_DIR / "vendor" / "helper" / "StellarSoulsOutfitRestore" / "Scripts" / "config.lua"
    helper_base = vendor_helper if vendor_helper.exists() else (BUILDER_DIR / preset_map["helperBase"]).resolve()
    return {
        "folder": preset["folder"],
        "paks": preset["paks"],
        "releaseRoot": (BUILDER_DIR / preset_map["releaseRoot"]).resolve(),
        "helperBase": helper_base,
        "needsHelper": needs_cns_helper(a),
        "warnings": warnings,
    }


def check_out_dir(out_dir, game: str | None = None) -> None:
    """Rechaza una carpeta de salida dentro de ~mods.

    build() deja ahi ``stage\\Paks`` y ``compile_mb`` con paks completos, y el
    juego carga ~mods recursivamente: quedan como mods fantasma que pisan al
    instalado, no aparecen en installed_status y --uninstall-paks no los toca.
    """
    import gamepaths
    if gamepaths.is_inside_mods(out_dir, game):
        raise SystemExit(
            "[builder] La carpeta de salida esta dentro de ~mods: "
            f"{out_dir}. El juego carga ~mods de forma recursiva, asi que las "
            "carpetas intermedias del build quedarian cargadas como mods "
            "fantasma. Elegi una carpeta fuera del juego (ej. "
            "Documents\\StellarSouls-builds) y usa la instalacion de la tool "
            "para copiar el pak a ~mods.")


def build_install_guide(a: dict, plan: dict, paks: list[str], has_helper: bool) -> str:
    lang = a["lang"]
    tpl_path = INSTALL_TEMPLATES / f"{lang}.txt"
    if not tpl_path.exists():
        tpl_path = INSTALL_TEMPLATES / "en.txt"
    tpl = tpl_path.read_text(encoding="utf-8")
    helper_block = ""
    if has_helper:
        helper_tpl = INSTALL_TEMPLATES / f"{lang}.helper.txt"
        if not helper_tpl.exists():
            helper_tpl = INSTALL_TEMPLATES / "en.helper.txt"
        helper_block = helper_tpl.read_text(encoding="utf-8")
    return (
        tpl.replace("{{PAKS}}", "\n".join(f"  - {p}.pak / .ucas / .utoc" for p in paks))
        .replace("{{HELPER_BLOCK}}", helper_block)
        .replace("{{PROFILE}}", a["combatProfile"])
        .replace("{{MINIBOSS}}", str(a["miniBoss"]))
    )


def build_vanilla_helper_only(a: dict, out_dir: Path, install: dict | None = None) -> Path:
    """ZIP con SOLO la ALPHA del helper vanilla (para testers sin CNS)."""
    alpha = a["vanillaHelperBuild"]
    stage = out_dir / "stage"
    if stage.exists():
        shutil.rmtree(stage)
    ue4ss = stage / "ue4ss" / "Mods"
    ue4ss.mkdir(parents=True)
    vanilla_helper.compile_vanilla_helper(alpha, ue4ss)

    (stage / f"INSTALL_{a['lang']}.txt").write_text(
        vanilla_helper.install_note(alpha).lstrip("\n"), encoding="utf-8")
    (stage / "build_manifest.json").write_text(
        json.dumps({"answers": a, "mode": "vanilla-helper-only",
                    "vanillaHelperBuild": vanilla_helper.build_name(alpha),
                    "paks": [], "warnings": []},
                   indent=2, ensure_ascii=False),
        encoding="utf-8")

    digest = hashlib.sha1(json.dumps(a, sort_keys=True).encode()).hexdigest()[:8]
    zip_path = out_dir / f"StellarSouls-{vanilla_helper.build_name(alpha)}-{digest}.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in stage.rglob("*"):
            if f.is_file():
                zf.write(f, f.relative_to(stage))

    install = install or {}
    if install.get("helper"):
        import gamepaths, installer
        game = gamepaths.detect_game(install.get("game"))
        if not game:
            raise RuntimeError("Juego no encontrado; instalar manualmente desde el ZIP")
        backup_before_install(game, ue4ss, ue4ss, {"paks": False, "helper": True})
        installer.install_helper(game, ue4ss, approved=True)
        # Una ALPHA a la vez: apaga el helper CNS y cualquier ALPHA anterior.
        installer.prune_helpers(game, installer.helper_targets(ue4ss), approved=True)

    try:
        import history
        history.record(a, str(zip_path))
    except Exception:
        pass
    import buildjournal
    buildjournal.finish()
    return zip_path


def install_targets(paks_dir: Path, helper_src: Path, install: dict) -> tuple[list, list]:
    """Que paks/helpers va a pisar la instalacion (para respaldarlos antes)."""
    import installer
    bases = []
    if install.get("paks"):
        bases = sorted({f.stem for f in Path(paks_dir).glob("*")
                        if f.suffix.lower() in installer.PAK_SUFFIXES})
    helpers = []
    if install.get("helper") and Path(helper_src).is_dir():
        helpers = installer.helper_targets(helper_src)
    return bases, helpers


def backup_before_install(game: str, paks_dir: Path, helper_src: Path, install: dict) -> None:
    """Respalda la instalacion previa y lo anota en el diario del build.

    Cancelar mata el proceso en cualquier punto, incluso a mitad de copiar los
    paks; el rollback necesita el estado anterior completo para reponerlo.
    """
    import buildjournal, installer
    bases, helpers = install_targets(paks_dir, helper_src, install)
    buildjournal.record_backup(
        installer.backup_install(game, bases, helpers,
                                 buildjournal.backup_root() / "install"))


def build(answers: dict, out_dir: Path, install: dict | None = None) -> Path:
    a = normalize(answers)
    check_out_dir(out_dir, (install or {}).get("game"))
    # Diario para poder deshacer si el usuario cancela (el build muere de golpe,
    # sin chance de limpiar por su cuenta).
    import buildjournal
    buildjournal.begin(out_dir)
    # La ruta elegida en la UI tambien la necesita el compilador (baselines
    # vanilla escribibles), no solo el instalador.
    if (install or {}).get("game"):
        import gamepaths
        gamepaths.remember_game(install["game"])
    # Solo la ALPHA del helper vanilla: no hay pak que compilar ni preset que
    # resolver, asi que se saltea todo el pipeline de gameplay.
    if only_vanilla_helper(a):
        return build_vanilla_helper_only(a, Path(out_dir), install)
    # Fail-fast: el pipeline extrae baselines y compila tablas (minutos) antes de
    # tocar UAssetGUI. Si el exe no puede correr (tipico bajo Wine/Proton sin
    # .NET), se avisa ahora y no despues de todo ese trabajo.
    problem = toolchain.selftest_uassetgui()
    if problem:
        raise RuntimeError(problem)
    plan = resolve(a)
    out_dir = Path(out_dir)
    stage = out_dir / "stage"
    if stage.exists():
        shutil.rmtree(stage)
    paks_dir = stage / "Paks"
    paks_dir.mkdir(parents=True)

    # Gameplay: compilar por feature si el combo esta cubierto, si no, preset.
    mode = "preset"
    compilation_report = {}
    world_applied = set()   # extras de mundo que ya aplico el pak combinado
    paks_out = list(plan["paks"])
    # Los bosses de overworld viven en CharacterTable + EventSpawnTable: salen por
    # el mismo pak combinado que el mini-boss, aunque no haya mini-bosses.
    mb_on = (a.get("miniBoss", "off") not in ("off", False, None)
             or build_specs.overworld_enabled(a))
    targets = build_specs.combo_to_targets(a) if not a.get("forcePreset") else None
    # Los parámetros también se usan en el pak combinado con mini-bosses.
    table_compiler.PARAMS["harder_mult"] = float(a.get("harderEnemiesMult", 2.0))
    table_compiler.PARAMS["gear_mult"] = float(a.get("strongerGearMult", 2.0))
    table_compiler.PARAMS["combat_levels"] = a.get("combatFeatureLevels") or {}
    table_compiler.PARAMS["economy_levels"] = a.get("combatEconomyLevels") or {}
    table_compiler.PARAMS["blaster_mult"] = float(a.get("blasterMultiplier", 2.0))
    table_compiler.PARAMS["just_mult"] = float(a.get("forgivingJustMult", 1.5))
    table_compiler.PARAMS["air_count"] = int(a.get("airDodgeCount", 2))
    table_compiler.PARAMS["tumbler_value"] = float(a.get("tumblerHealPercent", 60))
    table_compiler.PARAMS["extra_values"] = a.get("gameplayExtraValues") or {}
    table_compiler.PARAMS["world_values"] = a.get("worldTweakValues") or {}
    table_compiler.PARAMS["harder_bosses"] = a.get("harderBosses") or {}
    table_compiler.PARAMS["harder_enemies"] = a.get("harderEnemies") or {}
    world_ids = build_specs.world_selection(a)
    if a.get("combatProfile") == "firstRun" and not a.get("forcePreset"):
        # First Run = variante fija (repack del staging legacy).
        import miniboss_builder
        # El staging trae la EffectTable con el swap de outfit: los colaterales
        # sobre filas vanilla se restauran con el mismo pase de effect_extras.
        res = miniboss_builder.compile_from_staging(
            "firstRun", out_dir / "compile_fr",
            extras=build_specs.outfit_fix_extras(a),
            world_extra_ids=world_ids,
            world_values=a.get("worldTweakValues"))
        for key in ("pak", "ucas", "utoc"):
            shutil.copy2(res[key], paks_dir / Path(res[key]).name)
        paks_out = [res["pakName"]]
        compilation_report = res.get("report", {})
        world_applied = set(compilation_report.get("worldExtras", {}))
        mode = "compiled-firstrun"
    elif mb_on and not a.get("forcePreset"):
        # Mini-boss = pak unico combinado (combat+outfit+miniboss) via compile_miniboss.
        import miniboss_builder
        region = "greatDesert" if a.get("miniBoss") == "greatDesert" else "allRegions"
        density = a.get("miniBossDensity", "p20")
        res = miniboss_builder.compile_miniboss(
            out_dir / "compile_mb", density=density, region=region,
            outfit=bool(a.get("outfitSkinSuit", True)),
            difficulty=a.get("miniBossDifficulty", "flat"),
            variety=bool(a.get("enemyVariety", False)),
            extras=(a.get("gameplayExtras") or []) + build_specs.outfit_fix_extras(a),
            harder_mult=float(a.get("harderEnemiesMult", 2.0)),
            gear_mult=float(a.get("strongerGearMult", 2.0)),
            tumbler_value=float(a.get("tumblerHealPercent", 60)),
            toml_dir=a.get("customPatchesDir") or None,
            area_densities=a.get("miniBossRegionDensity"),
            miniboss_config=a.get("miniBossConfig"),
            overworld_config=a.get("overworldBosses"),
            just_mult=float(a.get("forgivingJustMult", 1.5)),
            air_count=int(a.get("airDodgeCount", 2)),
            combat_transform_ids=build_specs.combat_transforms(a)
            + [tid for tid in build_specs.enemy_tuning_transforms(a)
               if tid.endswith(".character") or tid.endswith(".skill")],
            world_extra_ids=world_ids,
            world_values=a.get("worldTweakValues"))
        for key in ("pak", "ucas", "utoc"):
            shutil.copy2(res[key], paks_dir / Path(res[key]).name)
        paks_out = [res["pakName"]]
        compilation_report = res.get("report", {})
        world_applied = set(compilation_report.get("worldExtras", {}))
        mode = "compiled-miniboss"
    elif targets:
        mode = "compiled"
        # Multiplicadores de los extras BETA (leidos por los transforms).
        if (set(a.get("gameplayExtras") or []) & build_specs._CT_EXTRAS) and a.get("combatProfile") != "full":
            plan["warnings"].append("ctExtrasNeedCombat")
        results = table_compiler.compile_targets(targets, out_dir / "compile",
                                                 toml_dir=a.get("customPatchesDir") or None)
        paks_out = []
        for name, res in results.items():
            compilation_report[name] = res.get("reports", {})
            for key in ("pak", "ucas", "utoc"):
                shutil.copy2(res[key], paks_dir / Path(res[key]).name)
            paks_out.append(name)
    else:
        src_paks = plan["releaseRoot"] / plan["folder"] / "Paks"
        for base in plan["paks"]:
            for ext in (".pak", ".ucas", ".utoc"):
                src = src_paks / f"{base}{ext}"
                if src.exists():
                    shutil.copy2(src, paks_dir / src.name)

    # Mini-boss y First Run compilan su propio pak combinado: los ajustes de
    # mundo que ya aplicaron adentro (RewardGroupTable) no se repiten, y el resto
    # sale como pak aparte para no volver a empaquetar las mismas tablas.
    if mode in ("compiled-miniboss", "compiled-firstrun"):
        pending = [e for e in world_ids if e not in world_applied]
        world = build_specs.world_target(dict(a, worldTweaks=pending))
        if world:
            res = table_compiler.compile_pak(
                world["transforms"], world["name"], out_dir / "compile_world",
                toml_dir=a.get("customPatchesDir") or None)
            compilation_report[world["name"]] = res.get("reports", {})
            for key in ("pak", "ucas", "utoc"):
                shutil.copy2(res[key], paks_dir / Path(res[key]).name)
            paks_out.append(world["name"])

    # El ajuste Hardcore vive en una tabla independiente y se compila como pak
    # componible aparte.
    hardcore = a.get("hardcoreEnemyBoost", "off")
    if hardcore in ("main", "insane") and (mb_on or a.get("combatProfile") == "firstRun"):
        hc_name = "StellarSouls-HarderBosses"
        hc = table_compiler.compile_pak(
            [f"hardcoreEnemies.{hardcore}"], hc_name, out_dir / "compile_hardcore")
        compilation_report[hc_name] = hc.get("reports", {})
        for key in ("pak", "ucas", "utoc"):
            shutil.copy2(hc[key], paks_dir / Path(hc[key]).name)
        paks_out.append(hc_name)

    # Helper: compilar config.lua si aplica.
    if plan["needsHelper"]:
        ue4ss = stage / "ue4ss" / "Mods"
        ue4ss.mkdir(parents=True, exist_ok=True)
        compile_helper(plan["helperBase"], ue4ss, a)

    # Helper vanilla (ALPHA, sin CNS): convive con el helper CNS en el ZIP, pero
    # son mods distintos de UE4SS; instalar solo el que corresponda al setup.
    alpha = a.get("vanillaHelperBuild")
    if vanilla_helper.is_enabled(alpha):
        ue4ss = stage / "ue4ss" / "Mods"
        ue4ss.mkdir(parents=True, exist_ok=True)
        vanilla_helper.compile_vanilla_helper(alpha, ue4ss)

    # Guia localizada.
    guide = build_install_guide(a, plan, paks_out, plan["needsHelper"])
    if vanilla_helper.is_enabled(alpha):
        guide += vanilla_helper.install_note(alpha)
    (stage / f"INSTALL_{a['lang']}.txt").write_text(guide, encoding="utf-8")

    # Manifest de la build (trazabilidad).
    (stage / "build_manifest.json").write_text(
        json.dumps({"answers": a, "mode": mode, "preset": plan["folder"],
                    "paks": paks_out, "warnings": plan["warnings"],
                    "compilationReport": compilation_report},
                   indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

    # Un build de bosses sin trazabilidad anti-farm es demasiado riesgoso para
    # instalarlo: el usuario debe poder auditarlo desde el ZIP sin abrir JSONs
    # internos. Esto también evita declarar éxito si el camino compilado no
    # produjo el reporte esperado.
    if mode in ("compiled-miniboss", "compiled-firstrun"):
        core = compilation_report.get("core", compilation_report)
        if isinstance(core, dict) and "antiFarm" not in core:
            raise RuntimeError("El build de mini-boss no produjo reporte anti-farm verificable")

    # ZIP.
    digest = hashlib.sha1(json.dumps(a, sort_keys=True).encode()).hexdigest()[:8]
    zip_path = out_dir / f"StellarSouls-Custom-{digest}.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in stage.rglob("*"):
            if f.is_file():
                zf.write(f, f.relative_to(stage))

    # Instalacion directa (SOLO con aprobacion del usuario, via `install`).
    install = install or {}
    if install.get("paks") or install.get("helper"):
        import gamepaths, installer
        game = gamepaths.detect_game(install.get("game"))
        if not game:
            raise RuntimeError("Juego no encontrado; instalar manualmente desde el ZIP")
        helper_src = stage / "ue4ss" / "Mods"
        want_helper = install.get("helper") and (plan["needsHelper"]
                                                 or vanilla_helper.is_enabled(alpha))
        backup_before_install(game, paks_dir, helper_src,
                              {"paks": install.get("paks"), "helper": want_helper})
        pak_keep, _ = install_targets(
            paks_dir, helper_src, {"paks": install.get("paks"), "helper": want_helper})
        # Que helpers PIDE esta build, mire o no el check de instalar helper: si
        # no se calcula asi, instalar solo los paks borraria el helper que la
        # build si quiere.
        helper_keep = installer.helper_targets(helper_src) if helper_src.is_dir() else []
        if install.get("paks"):
            installer.install_paks(game, paks_dir, approved=True)
            installer.prune_paks(game, pak_keep, approved=True)
        if want_helper:
            installer.install_helper(game, helper_src, approved=True)
        # Instalar es "dejar el juego como pide esta build": el helper que sobra
        # (outfit destildado, modo sin helper, otra ALPHA) se apaga aunque el
        # check de helper este destildado, porque si no queda corriendo solo.
        installer.prune_helpers(game, helper_keep, approved=True)

    # Historial (para 'usar de plantilla' / re-exportar).
    try:
        import history
        history.record(a, str(zip_path))
    except Exception:
        pass
    buildjournal.finish()   # build completo: no queda nada que deshacer
    return zip_path


def main():
    ap = argparse.ArgumentParser(description="Compila un mod Stellar Souls personalizado.")
    ap.add_argument("--answers", help="JSON de respuestas o @archivo.")
    ap.add_argument("--out", help="carpeta de salida.")
    ap.add_argument("--template", help="id de una config del historial para pre-cargar respuestas.")
    ap.add_argument("--install-paks", action="store_true", help="instalar el pak en ~mods (aprobado por el usuario).")
    ap.add_argument("--install-helper", action="store_true", help="instalar+activar el helper (aprobado por el usuario).")
    ap.add_argument("--game", help="ruta del juego (autodetecta si se omite).")
    ap.add_argument("--history", action="store_true", help="listar configs guardadas y salir.")
    ap.add_argument("--reexport", help="re-exportar el zip de una config del historial (requiere --out).")
    ap.add_argument("--installed-status", action="store_true", help="JSON de lo instalado por la tool.")
    ap.add_argument("--uninstall-paks", action="store_true", help="desinstalar el mod que la tool instalo.")
    ap.add_argument("--uninstall-helper", action="store_true", help="desinstalar el helper que la tool instalo.")
    ap.add_argument("--rollback", action="store_true",
                    help="deshacer el ultimo build cortado (restaura la instalacion y limpia la salida).")
    args = ap.parse_args()

    if args.game:
        import gamepaths
        gamepaths.remember_game(args.game)

    if args.rollback:
        import buildjournal
        print(f"OK -> {json.dumps(buildjournal.rollback())}")
        return

    if args.installed_status:
        import installer
        print(json.dumps(installer.installed_status(args.game)))
        return
    if args.uninstall_paks or args.uninstall_helper:
        import installer, gamepaths
        game = gamepaths.detect_game(args.game)
        if not game:
            raise SystemExit("[builder] juego no encontrado")
        res = {}
        if args.uninstall_paks:
            res["paks"] = installer.uninstall_paks(game, approved=True)
        if args.uninstall_helper:
            res["helper"] = installer.uninstall_helper(game, approved=True)
        print(f"OK -> {json.dumps(res)}")
        return

    if args.history:
        import history
        for r in history.list_records():
            print(f"{r['id']}  {r['label']:24}  {r.get('zip','')}")
        return
    if args.reexport:
        import history
        check_out_dir(args.out or ".", args.game)
        print(f"OK -> {history.reexport(args.reexport, args.out or '.')}")
        return

    if args.template:
        import history
        answers = history.as_template(args.template) or {}
        if args.answers:  # overrides sobre la plantilla
            raw = args.answers
            if raw.startswith("@"):
                raw = Path(raw[1:]).read_text(encoding="utf-8")
            answers.update(json.loads(raw))
    else:
        raw = args.answers
        if raw and raw.startswith("@"):
            raw = Path(raw[1:]).read_text(encoding="utf-8")
        answers = json.loads(raw)

    install = {"paks": args.install_paks, "helper": args.install_helper, "game": args.game}
    zip_path = build(answers, Path(args.out), install=install)
    print(f"OK -> {zip_path}")


if __name__ == "__main__":
    # El CLI se consume desde Qt y también desde terminal. Nunca ocultar el
    # traceback: el mensaje genérico "build_custom failed" no permite saber si
    # falló Python, una herramienta externa o una tabla concreta.
    try:
        main()
    except SystemExit:
        raise
    except Exception as exc:
        import traceback
        print(f"BUILD_ERROR {type(exc).__name__}: {exc}", file=sys.stderr, flush=True)
        traceback.print_exc(file=sys.stderr)
        raise
