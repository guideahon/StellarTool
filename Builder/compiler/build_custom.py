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

from helper_compiler import compile_helper
import build_specs
import table_compiler

BUILDER_DIR = Path(__file__).resolve().parent.parent
FEATURES = BUILDER_DIR / "features"
INSTALL_TEMPLATES = BUILDER_DIR / "i18n" / "install"

SUPPORTED_LANGS = ["es", "en", "fr", "it", "de", "ja", "ko", "pt_BR", "ru", "zh_Hans"]


def load_json(path: Path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def normalize(answers: dict) -> dict:
    """Rellena defaults y normaliza miniBoss a on/off para la clave de preset."""
    a = dict(answers)
    a.setdefault("combatProfile", "full")
    a.setdefault("outfitSkinSuit", True)
    a.setdefault("miniBoss", "off")
    a.setdefault("lang", "es")
    if a["lang"] not in SUPPORTED_LANGS:
        a["lang"] = "es"
    return a


def preset_key(a: dict) -> str:
    mb = "off" if a["miniBoss"] in ("off", False, None) else "on"
    outfit = "true" if a["outfitSkinSuit"] else "false"
    return f'{a["combatProfile"]}|{outfit}|{mb}'


def validate(a: dict) -> None:
    """Rechaza combos sin sentido con mensajes accionables (antes de buscar preset)."""
    combat, outfit, mb = a["combatProfile"], a["outfitSkinSuit"], a["miniBoss"]
    mb_on = mb not in ("off", False, None)
    if combat == "none" and not outfit and not mb_on:
        raise SystemExit("[builder] Nada seleccionado: activa combate, outfit o mini-boss.")
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
        raise SystemExit(
            f"[builder] Combo sin preset F1: {key}. "
            f"Requiere TableCompiler (F2+) o ajustar respuestas. "
            f"Presets: {sorted(preset_map['presets'])}"
        )
    # Presets no honran sub-features granulares.
    if a.get("miniBossDensity") and a["miniBoss"] != "off":
        warnings.append("densityIgnored")
    if a.get("gaugeTweaks"):
        warnings.append("gaugeTweaksIgnored")
    # Helper base: preferir el vendorizado (Builder autocontenido en Stellar
    # Tool); si no, la ruta de preset_map (dev, apunta a Stellar Souls/Release).
    vendor_helper = BUILDER_DIR / "vendor" / "helper" / "StellarSoulsOutfitRestore" / "Scripts" / "config.lua"
    helper_base = vendor_helper if vendor_helper.exists() else (BUILDER_DIR / preset_map["helperBase"]).resolve()
    return {
        "folder": preset["folder"],
        "paks": preset["paks"],
        "releaseRoot": (BUILDER_DIR / preset_map["releaseRoot"]).resolve(),
        "helperBase": helper_base,
        "needsHelper": bool(a["outfitSkinSuit"]),
        "warnings": warnings,
    }


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


def build(answers: dict, out_dir: Path, install: dict | None = None) -> Path:
    a = normalize(answers)
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
    paks_out = list(plan["paks"])
    mb_on = a.get("miniBoss", "off") not in ("off", False, None)
    targets = build_specs.combo_to_targets(a) if not a.get("forcePreset") else None
    # Los parámetros también se usan en el pak combinado con mini-bosses.
    table_compiler.PARAMS["harder_mult"] = float(a.get("harderEnemiesMult", 2.0))
    table_compiler.PARAMS["gear_mult"] = float(a.get("strongerGearMult", 2.0))
    table_compiler.PARAMS["combat_levels"] = a.get("combatFeatureLevels") or {}
    table_compiler.PARAMS["economy_levels"] = a.get("combatEconomyLevels") or {}
    table_compiler.PARAMS["blaster_mult"] = float(a.get("blasterMultiplier", 2.0))
    table_compiler.PARAMS["just_mult"] = float(a.get("forgivingJustMult", 1.5))
    table_compiler.PARAMS["air_count"] = int(a.get("airDodgeCount", 2))
    if a.get("combatProfile") == "firstRun" and not a.get("forcePreset"):
        # First Run = variante fija (repack del staging legacy).
        import miniboss_builder
        res = miniboss_builder.compile_from_staging("firstRun", out_dir / "compile_fr")
        for key in ("pak", "ucas", "utoc"):
            shutil.copy2(res[key], paks_dir / Path(res[key]).name)
        paks_out = [res["pakName"]]
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
            extras=a.get("gameplayExtras") or [],
            harder_mult=float(a.get("harderEnemiesMult", 2.0)),
            gear_mult=float(a.get("strongerGearMult", 2.0)),
            toml_dir=a.get("customPatchesDir") or None,
            area_densities=a.get("miniBossRegionDensity"),
            miniboss_config=a.get("miniBossConfig"),
            just_mult=float(a.get("forgivingJustMult", 1.5)),
            air_count=int(a.get("airDodgeCount", 2)),
            combat_transform_ids=build_specs.combat_transforms(a))
        for key in ("pak", "ucas", "utoc"):
            shutil.copy2(res[key], paks_dir / Path(res[key]).name)
        paks_out = [res["pakName"]]
        compilation_report = res.get("report", {})
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

    # Helper: compilar config.lua si aplica.
    if plan["needsHelper"]:
        ue4ss = stage / "ue4ss" / "Mods"
        ue4ss.mkdir(parents=True, exist_ok=True)
        compile_helper(plan["helperBase"], ue4ss, a)

    # Guia localizada.
    guide = build_install_guide(a, plan, paks_out, plan["needsHelper"])
    (stage / f"INSTALL_{a['lang']}.txt").write_text(guide, encoding="utf-8")

    # Manifest de la build (trazabilidad).
    (stage / "build_manifest.json").write_text(
        json.dumps({"answers": a, "mode": mode, "preset": plan["folder"],
                    "paks": paks_out, "warnings": plan["warnings"],
                    "compilationReport": compilation_report},
                   indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

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
        game = install.get("game") or gamepaths.detect_game()
        if not game:
            raise RuntimeError("Juego no encontrado; instalar manualmente desde el ZIP")
        if install.get("paks"):
            installer.install_paks(game, paks_dir, approved=True)
        if install.get("helper") and plan["needsHelper"]:
            installer.install_helper(game, stage / "ue4ss" / "Mods", approved=True)

    # Historial (para 'usar de plantilla' / re-exportar).
    try:
        import history
        history.record(a, str(zip_path))
    except Exception:
        pass
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
    args = ap.parse_args()

    if args.installed_status:
        import installer
        print(json.dumps(installer.installed_status(args.game)))
        return
    if args.uninstall_paks or args.uninstall_helper:
        import installer, gamepaths
        game = args.game or gamepaths.detect_game()
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
    main()
