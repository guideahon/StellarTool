"""TableCompiler (F2) — compilacion real de gameplay por feature.

Aplica transforms declarados en el manifest sobre las tablas base JSON, luego
fromjson + to-zen (toolchain) -> pak Zen verificado. Cada transform declara la
tabla que edita y la 'base' que necesita (full/vanilla). El compilador solo carga
y packea las tablas realmente tocadas.

Helpers de fila portados de Development/*.py (rows/find/prop/get/setv/midpoint).
"""
from __future__ import annotations

import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import toolchain

BUILDER_DIR = Path(__file__).resolve().parent.parent
SOURCES = BUILDER_DIR / "base_tables" / "sources.json"


# ---- helpers de fila (formato UAssetAPI) ----

def rows(doc):
    return doc["Exports"][0]["Table"]["Data"]


def find(table, name):
    return next((r for r in table if r.get("Name") == name), None)


def prop(row, name):
    return next((p for p in row["Value"] if p["Name"] == name), None)


def get(row, name):
    p = prop(row, name)
    return p.get("Value") if p else None


def setv(row, name, value):
    p = prop(row, name)
    if p:
        p["Value"] = value
        p["IsZero"] = value in (0, 0.0, None)


def numeric(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def midpoint(vanilla, current):
    v = vanilla + (current - vanilla) / 2
    return int(round(v)) if isinstance(vanilla, int) and isinstance(current, int) else v


# ---- registro de transforms ----
# Cada entrada: id -> {"table": <name>, "base": full|vanilla, "fn": callable(doc, vanilla_doc)}
# fn muta doc in place. vanilla_doc se pasa solo si base != vanilla y se necesita diff.

REGISTRY = {}


def transform(tid, table, base="full", needs_vanilla=False):
    def deco(fn):
        REGISTRY[tid] = {"table": table, "base": base, "needs_vanilla": needs_vanilla, "fn": fn}
        return fn
    return deco


@transform("combat.skill.full", table="SkillTable", base="full")
def _skill_full(doc, vanilla):
    pass  # identidad: la base full ya ES el combate completo.


@transform("combat.skill.firstRunMidpoint", table="SkillTable", base="full", needs_vanilla=True)
def _skill_firstrun(doc, vanilla):
    van_by = {r["Name"]: r for r in rows(vanilla)}
    n = 0
    for row in rows(doc):
        vrow = van_by.get(row["Name"])
        if not vrow:
            continue
        vprops = {p["Name"]: p for p in vrow["Value"]}
        for cp in row["Value"]:
            vp = vprops.get(cp["Name"])
            if not vp:
                continue
            cv, vv = cp.get("Value"), vp.get("Value")
            if numeric(cv) and numeric(vv) and cv != vv:
                cp["Value"] = midpoint(vv, cv)
                cp["IsZero"] = False
                n += 1
    doc.setdefault("_report", {})["skillMidpoints"] = n


@transform("combat.character.full", table="CharacterTable", base="full")
def _character_full(doc, vanilla):
    pass  # identidad: base full = CharacterTable del combate.


_OVERRIDES = None


def _load_overrides():
    global _OVERRIDES
    if _OVERRIDES is None:
        p = BUILDER_DIR / "base_tables" / "public_1_2_19_overrides.json"
        _OVERRIDES = json.loads(p.read_text(encoding="utf-8"))
    return _OVERRIDES


def _apply_overrides(doc, table):
    """Aplica valores exactos de la tuning publica 1.2.19 por fila+propiedad."""
    ov = _load_overrides().get(table, {})
    idx = {r["Name"]: r for r in rows(doc)}
    n = 0
    for rn, props in ov.items():
        row = idx.get(rn)
        if not row:
            continue
        pmap = {p["Name"]: p for p in row["Value"]}
        for pn, val in props.items():
            p = pmap.get(pn)
            if p is not None and p.get("Value") != val:
                p["Value"] = val
                p["IsZero"] = val in (0, 0.0)
                n += 1
    doc.setdefault("_report", {})["publicTuningOverrides"] = n


# Tuning publica 1.2.19: baja gauge caps + beta reduce (Player) y costo/cooldown
# de skills Beta. Convierte la base interna (1.2.39) en el combate publico shipped.
@transform("combat.tuning.public1219.skill", table="SkillTable", base="full")
def _tuning_skill(doc, vanilla):
    _apply_overrides(doc, "SkillTable")


@transform("combat.tuning.public1219.character", table="CharacterTable", base="full")
def _tuning_character(doc, vanilla):
    _apply_overrides(doc, "CharacterTable")


@transform("outfit.effectTable.skinSuitOnBreak", table="EffectTable", base="full")
def _outfit_skinsuit(doc, vanilla):
    pass  # identidad: base full = EffectTable con SkinSuit-on-break (DirectRestore).


# Tuning publica 1.2.19 de EffectTable: tachy chain clears + heal/tumbler
# (Item_HP_RPotion CalculationValue=60). Convierte DR base interna en la shipped.
@transform("outfit.tuning.public1219", table="EffectTable", base="full")
def _outfit_tuning(doc, vanilla):
    _apply_overrides(doc, "EffectTable")


@transform("combat.blasterCellDamageX2", table="SkillTable", base="full")
def _blaster_x2(doc, vanilla):
    n = 0
    for row in rows(doc):
        name = row.get("Name", "")
        if name.startswith("P_Eve_Gun_ShootRailgun"):
            for field in ("AttackDamageRate", "ShieldAttackDamageRate"):
                dp = prop(row, field)
                if dp and numeric(dp.get("Value")):
                    dp["Value"] = dp["Value"] * float(PARAMS.get("blaster_mult", 2.0))
                    dp["IsZero"] = False
                    n += 1
    doc.setdefault("_report", {})["blasterRailgunRows"] = n


# ---- extras BETA como transforms (para el path combat-only / sin mini-boss) ----
# Parametros mutables leidos por los transforms (los setea compile_targets/caller).
PARAMS = {
    "harder_mult": 2.0, "gear_mult": 2.0, "combat_levels": {},
    "economy_levels": {}, "blaster_mult": 2.0, "just_mult": 1.5, "air_count": 2,
    "tumbler_value": 60.0, "world_values": {},
}


# Cambios semánticos del mod histórico. A diferencia de combat.skill.full, estos
# parten de vanilla y copian únicamente el subconjunto elegido por el usuario.
_DAMAGE_PROPS = {"AttackDamageRate", "ShieldAttackDamageRate", "FixedDamage"}
_DRONE_EXCEPTIONS = {
    "P_Eve_Gun_ShootSlug1_1", "P_Eve_Gun_ShootTutorialSlug1_1",
    "P_Eve_Gun_ShootRailgun1_1", "P_Eve_Gun_ShootRailgun2_1",
    "P_Eve_Gun_ShootRailgun3_1", "P_Eve_Gun_ShootRailgun4_1",
}


def _copy_changed(doc, source, row_pred, prop_pred, report_key, amount=1.0):
    source_rows = {r["Name"]: r for r in rows(source)}
    n = 0
    for row in rows(doc):
        name = row.get("Name", "")
        if not row_pred(row):
            continue
        src = source_rows.get(name)
        if not src:
            continue
        src_props = {p["Name"]: p for p in src["Value"]}
        for target_prop in row["Value"]:
            if not prop_pred(row, target_prop):
                continue
            source_prop = src_props.get(target_prop["Name"])
            if source_prop is not None and target_prop.get("Value") != source_prop.get("Value"):
                old, new = target_prop.get("Value"), source_prop.get("Value")
                if numeric(old) and numeric(new) and amount != 1.0:
                    value = old + (new - old) * amount
                    new = int(round(value)) if isinstance(old, int) and not isinstance(old, bool) else value
                target_prop["Value"] = new
                target_prop["IsZero"] = source_prop.get("IsZero", False)
                n += 1
    doc.setdefault("_report", {})[report_key] = n


def _skill_feature(tid, key, row_pred, prop_pred=lambda _r, p: p["Name"] in _DAMAGE_PROPS):
    @transform(tid, table="SkillTable", base="vanilla")
    def apply(doc, _vanilla, _rp=row_pred, _pp=prop_pred, _tid=tid):
        amount = float(PARAMS.get("combat_levels", {}).get(key, 1.0))
        _copy_changed(doc, load_table("SkillTable", "full"), _rp, _pp, _tid, amount)


def _is_boss_character(row):
    return (get(row, "ActorType") == "ActorType_BossMonster"
            or str(row.get("Name", "")).endswith("_MB"))


_BOSS_SKILL_PREFIXES = None


def _is_boss_skill(row):
    """Relaciona SkillTable con aliases BossMonster de CharacterTable."""
    global _BOSS_SKILL_PREFIXES
    if _BOSS_SKILL_PREFIXES is None:
        aliases = [
            str(r.get("Name", "")) for r in rows(load_table("CharacterTable", "vanilla"))
            if _is_boss_character(r)
        ]
        _BOSS_SKILL_PREFIXES = tuple(f"{alias}_" for alias in aliases if alias)
    return str(row.get("Name", "")).startswith(_BOSS_SKILL_PREFIXES)


_skill_feature("combat.betaBurstDamage", "betaBurstDamage",
               lambda r: str(r.get("Name", "")).startswith(("P_Eve_Sword_Beta_", "P_Eve_Sword_Burst_")))
_skill_feature("combat.droneDamage", "droneDamage",
               lambda r: str(r.get("Name", "")).startswith("P_Eve_Gun_")
               and r.get("Name") not in _DRONE_EXCEPTIONS)
_skill_feature("combat.dashDamage", "dashDamage",
               lambda r: get(r, "UseableCheckGroup") == "DashAttack")
_skill_feature("combat.eveDamage", "eveDamage",
               lambda r: str(r.get("Name", "")).startswith("P_Eve_")
               and not str(r.get("Name", "")).startswith(("P_Eve_Gun_", "P_Eve_Sword_Beta_", "P_Eve_Sword_Burst_"))
               and get(r, "UseableCheckGroup") != "DashAttack")
_skill_feature("combat.enemyDamage", "enemyDamage",
               lambda r: not str(r.get("Name", "")).startswith("P_Eve_")
               and not _is_boss_skill(r))
_skill_feature("combat.perfectDodge", "perfectDodge",
               lambda r: r.get("Name") in {
                   "P_Eve_Sword_Normal_Evade2_1", "P_Eve_Tachy_Normal_Evade1_1",
                   "P_Eve_Fusion_Normal_Evade1_1",
               },
               lambda _r, p: p["Name"] == "JustSkillActiveAlias")


@transform("combat.antiSpamSkill", table="SkillTable", base="vanilla")
def _anti_spam_skill(doc, _vanilla):
    overrides = _load_overrides()["SkillTable"]
    idx = {r["Name"]: r for r in rows(doc)}
    n = 0
    for row_name, values in overrides.items():
        row = idx.get(row_name)
        if row and "CoolTime" in values and get(row, "CoolTime") != values["CoolTime"]:
            setv(row, "CoolTime", float(PARAMS.get("economy_levels", {}).get(
                "cooldown", values["CoolTime"])))
            n += 1
    doc.setdefault("_report", {})["combat.cooldown"] = n


@transform("combat.tachyDuration", table="CharacterTable", base="vanilla")
def _tachy_duration(doc, _vanilla):
    _copy_changed(doc, load_table("CharacterTable", "full"),
                  lambda r: r.get("Name") == "Player",
                  lambda _r, p: p["Name"] == "MaxTachyGauge", "combat.tachyDuration",
                  float(PARAMS.get("combat_levels", {}).get("tachyDuration", 1.0)))


@transform("combat.enemyVulnerability", table="CharacterTable", base="vanilla")
def _enemy_vulnerability(doc, _vanilla):
    _copy_changed(doc, load_table("CharacterTable", "full"),
                  lambda r: r.get("Name") != "Player" and not _is_boss_character(r),
                  lambda _r, p: p["Name"] == "FinalHPDamageReduceRate",
                  "combat.enemyVulnerability",
                  float(PARAMS.get("combat_levels", {}).get("enemyVulnerability", 1.0)))


@transform("combat.antiSpamCharacter", table="CharacterTable", base="vanilla")
def _anti_spam_character(doc, _vanilla):
    # Compatibilidad con plantillas anteriores: equivale a gain + capacity.
    _slower_gain(doc, _vanilla)
    _lower_capacity(doc, _vanilla)


@transform("combat.slowerGain", table="CharacterTable", base="vanilla")
def _slower_gain(doc, _vanilla):
    player = find(rows(doc), "Player")
    value = float(PARAMS.get("economy_levels", {}).get(
        "slowerGain", _load_overrides()["CharacterTable"]["Player"]["BetaGaugeAdditiveRate"]))
    changed = bool(player and get(player, "BetaGaugeAdditiveRate") != value)
    if changed:
        setv(player, "BetaGaugeAdditiveRate", value)
    doc.setdefault("_report", {})["combat.slowerGain"] = int(changed)


@transform("combat.lowerCapacity", table="CharacterTable", base="vanilla")
def _lower_capacity(doc, _vanilla):
    player = find(rows(doc), "Player")
    values = _load_overrides()["CharacterTable"]["Player"]
    n = 0
    if player:
        selected = PARAMS.get("economy_levels", {}).get("lowerCapacity")
        capacity = selected if isinstance(selected, dict) else values
        for field in ("MaxBetaGauge", "MaxBurstGauge"):
            if get(player, field) != capacity[field]:
                setv(player, field, capacity[field])
                n += 1
    doc.setdefault("_report", {})["combat.lowerCapacity"] = n


def _reg_extra(tid, table, base, fn_name, module):
    """Registra un transform que delega en extras.py / effect_extras.py."""
    def run(doc, vanilla, _m=module, _f=fn_name):
        mod = __import__(_m)
        fn = getattr(mod, _f)
        if _f == "harder_enemies":
            n = fn(doc, PARAMS.get("harder_mult", 2.0))
        elif _f == "stronger_gear":
            n = fn(doc, PARAMS.get("gear_mult", 2.0))
        elif _f == "forgiving_just":
            n = fn(doc, PARAMS.get("just_mult", 1.5))
        elif _f == "extra_air_dodge":
            n = fn(doc, PARAMS.get("air_count", 2))
        elif _f == "tumbler_heal":
            n = fn(doc, PARAMS.get("tumbler_value", 60.0))
        elif _f in {
                "ammo_stacks", "ammo_100x", "consumable_stacks",
                "shield_regen", "attribute_shield_regen", "base_attributes",
                "high_gauge_capacity", "passive_hp_regen", "fishing_power",
                "attack_speed"}:
            values = PARAMS.get("extra_values", {}).get(_f, {})
            n = fn(doc, **values)
        elif _m == "world_extras":
            n = fn(doc, **mod.values_for(_f, PARAMS.get("world_values", {})))
        else:
            n = fn(doc)
        doc.setdefault("_report", {})[_f] = n
    REGISTRY[tid] = {"table": table, "base": base, "needs_vanilla": False, "fn": run}


# CharacterTable (van sobre el pak de combate)
_reg_extra("extras.playerQol", "CharacterTable", "full", "player_qol", "extras")
_reg_extra("extras.ammoStacks", "CharacterTable", "full", "ammo_stacks", "extras")
_reg_extra("extras.consumableStacks", "CharacterTable", "full", "consumable_stacks", "extras")
_reg_extra("extras.shieldRegen", "CharacterTable", "full", "shield_regen", "extras")
_reg_extra("extras.attackSpeed", "CharacterTable", "full", "attack_speed", "extras")
_reg_extra("extras.longerTachy", "CharacterTable", "full", "longer_tachy", "extras")
_reg_extra("extras.hpDrain", "CharacterTable", "full", "hp_drain", "extras")
_reg_extra("extras.harderEnemies", "CharacterTable", "full", "harder_enemies", "extras")
_reg_extra("extras.bossStaggerImmunity", "CharacterTable", "full", "boss_stagger_immunity", "extras")
_reg_extra("extras.baseAttributes", "CharacterTable", "full", "base_attributes", "extras")
_reg_extra("extras.attributeShieldRegen", "CharacterTable", "full", "attribute_shield_regen", "extras")
_reg_extra("extras.highGaugeCapacity", "CharacterTable", "full", "high_gauge_capacity", "extras")
_reg_extra("extras.passiveHpRegen", "CharacterTable", "full", "passive_hp_regen", "extras")
_reg_extra("extras.fishingPower", "CharacterTable", "full", "fishing_power", "extras")
_reg_extra("extras.ammo100x", "CharacterTable", "full", "ammo_100x", "extras")
# EffectTable (van sobre el pak de outfit, o sobre uno propio desde vanilla)
for _tid, _fn in (("noFallDamage", "no_fall_damage"), ("noEnvDeath", "no_environment_death"),
                  ("noWaterDeath", "no_water_death"), ("noSandDeath", "no_sand_death"),
                  ("tachyReduce", "tachy_reduce"), ("strongerGear", "stronger_gear"),
                  ("autoGaugeRecovery", "auto_gauge_recovery"),
                  ("betaParryRecovery", "beta_parry_recovery"),
                  ("burstDodgeRecovery", "burst_dodge_recovery"),
                  ("tumblerHeal", "tumbler_heal"),
                  ("gaugeRecoveryOverTime", "gauge_recovery_over_time"),
                  ("gunGorgonRotation", "gun_gorgon_free_rotation")):
    _reg_extra(f"extras.{_tid}", "EffectTable", "full", _fn, "effect_extras")
    _reg_extra(f"extrasVanilla.{_tid}", "EffectTable", "vanilla", _fn, "effect_extras")
# Colaterales del pak de outfit: solo tienen sentido sobre la base full (la
# EffectTable con el swap Skin-Suit-on-break); en vanilla ya estan intactos.
_reg_extra("outfit.vanillaRestFX", "EffectTable", "full",
           "restore_camp_rest_fx", "effect_extras")
_reg_extra("outfit.vanillaShieldRegenBlock", "EffectTable", "full",
           "restore_shield_regen_block", "effect_extras")
_reg_extra("outfit.qteRestoreAlpha", "EffectTable", "full",
           "keep_outfit_restore_running_during_qte", "effect_extras")
# SkillTable (sensacion de combate)
_reg_extra("extras.forgivingJust", "SkillTable", "full", "forgiving_just", "skill_extras")
_reg_extra("extras.extraAirDodge", "SkillTable", "full", "extra_air_dodge", "skill_extras")


@transform("hardcoreEnemies.main", table="DifficultyStatGroupTable", base="vanilla")
def _hardcore_enemies_main(doc, _vanilla):
    import hardcore_enemies
    doc.setdefault("_report", {})["hardcoreEnemies"] = hardcore_enemies.apply(doc, "main")


@transform("hardcoreEnemies.insane", table="DifficultyStatGroupTable", base="vanilla")
def _hardcore_enemies_insane(doc, _vanilla):
    import hardcore_enemies
    doc.setdefault("_report", {})["hardcoreEnemies"] = hardcore_enemies.apply(doc, "insane")
_reg_extra("extras.dashCooldown4", "SkillTable", "full", "dash_cooldown_4s", "skill_extras")
_reg_extra("extras.droneScanCooldown", "SkillTable", "full", "drone_scan_cooldown_5s", "skill_extras")
# Mundo/economia: tablas que ningun pak previo tocaba, siempre desde vanilla.
for _wid, (_wtable, _wfn) in __import__("world_extras").WORLD_EXTRAS.items():
    _reg_extra(f"world.{_wid}", _wtable, "vanilla", _wfn, "world_extras")
_reg_extra("extras.droneScanDuration", "EffectTable", "full", "drone_scan_duration", "effect_extras")
_reg_extra("extrasVanilla.droneScanDuration", "EffectTable", "vanilla", "drone_scan_duration", "effect_extras")


# ---- compilador ----

def _sources():
    root = json.loads(SOURCES.read_text(encoding="utf-8"))
    override = os.environ.get("SSMOD_TABLES")
    return root["tables"], override


# vendor/ (dentro de Builder) tiene prioridad -> Builder autocontenido en Stellar
# Tool, sin depender de rutas de Stellar Souls en runtime.
_VENDOR_SSMOD = BUILDER_DIR / "vendor" / "ssmod"
# Renombres al vendorizar (ver Builder/vendor.py).
_VENDOR_RENAME = {"SkillTable.json": "SkillTable_full.json"}


_progress_seq = 0


def _emit_progress(kind, detail):
    """Linea de progreso legible por la app (no localizada: la UI traduce).

    Formato: ``PROGRESS <kind> <n> <detail>``. Se va por stdout con flush para
    que Qt la reciba mientras el paso largo corre, no al final del proceso.
    """
    global _progress_seq
    _progress_seq += 1
    print(f"PROGRESS {kind} {_progress_seq} {detail}", flush=True)


def _generate_writable_vanilla(table, output):
    """Extrae una tabla vanilla y conserva el JSON UAssetAPI escribible.

    Se estagean sólo contenedores raíz mediante hardlinks junto al juego para
    que ningún archivo de ``~mods`` pueda contaminar la base.
    """
    import gamepaths

    game = gamepaths.detect_game()
    if not game:
        raise FileNotFoundError(
            f"{table} necesita la instalación de Stellar Blade para generar su baseline "
            "escribible, y no se encontró el juego. Elegí la carpeta del juego en Stellar "
            "Tool (la que contiene SB\\Content\\Paks) o definí la variable de entorno "
            f"{gamepaths.ENV_VAR} con esa ruta y volvé a compilar.")
    paks = Path(game) / "SB" / "Content" / "Paks"
    if toolchain.oodle_dir() is None:
        raise FileNotFoundError(
            f"{table} se extrae de los paks del juego con retoc, que necesita "
            "Oodle. " + toolchain.missing_oodle_msg())
    # Cada extraccion escanea TODOS los contenedores del juego: son minutos por
    # tabla. Sin esta linea la UI muestra "Compilando..." fijo y parece colgada.
    _emit_progress("baseline", table)
    stage = Path(tempfile.mkdtemp(prefix="~st_builder_vanilla_", dir=paks))
    extracted = Path(tempfile.mkdtemp(prefix="st_builder_legacy_"))
    try:
        roots = list(paks.glob("global.*")) + list(paks.glob("pakchunk*"))
        for src in roots:
            if src.is_file():
                os.link(src, stage / src.name)
        cp = toolchain._retoc([
            "to-legacy", "-f", table, "--version", "UE4_26",
            str(stage), str(extracted),
        ])
        uasset = next(extracted.rglob(f"{table}.uasset"), None)
        if cp.returncode != 0 or not uasset:
            raise RuntimeError(
                f"No se pudo extraer {table} de los contenedores vanilla "
                f"(retoc rc={cp.returncode}): {toolchain.out_err(cp)}")
        output = Path(output)
        output.parent.mkdir(parents=True, exist_ok=True)
        usmap = toolchain.tools_dir() / "StellarBlade.usmap"
        cp = toolchain._run([
            str(toolchain.tools_dir() / "UAssetGUI.exe"), "tojson",
            str(uasset), str(output), "VER_UE4_26", str(usmap),
        ], timeout=600)
        if cp.returncode != 0 or not output.exists():
            raise RuntimeError(
                f"UAssetGUI no generó la baseline escribible de {table} "
                f"(rc={cp.returncode}): {toolchain.out_err(cp)}")
    finally:
        shutil.rmtree(extracted, ignore_errors=True)
        shutil.rmtree(stage, ignore_errors=True)


def _is_baseline_cache(path) -> bool:
    """True si la ruta apunta al cache de tablas vanilla de Stellar Tool.

    Esas tablas no se versionan (son contenido del juego): se extraen del pak
    del usuario la primera vez que un transform las pide.
    """
    parts = [p.lower() for p in Path(path).parts]
    return "baseline" in parts and "uasset_json" in parts


def load_table(name, base):
    tables, env = _sources()
    configured = os.path.expandvars(tables[name][base])
    orig = Path(env) / Path(configured).name if env else Path(configured)
    if _VENDOR_SSMOD.exists():
        fname = orig.name
        cand = _VENDOR_SSMOD / _VENDOR_RENAME.get(fname, fname)
        if cand.exists():
            orig = cand
    if not orig.exists() and _is_baseline_cache(orig):
        _generate_writable_vanilla(name, orig)
    if not orig.exists():
        raise FileNotFoundError(
            f"No se encontro la tabla base {name}: {orig}. "
            "Genera el baseline vanilla desde Ajustes de Stellar Tool.")
    return json.loads(orig.read_text(encoding="utf-8"))


def apply_transforms(table, transform_ids, base="vanilla"):
    """Aplica una selección a una tabla en memoria; útil para paks combinados."""
    doc = load_table(table, base)
    applied = []
    for tid in transform_ids:
        spec = REGISTRY.get(tid)
        if not spec:
            raise KeyError(f"Transform desconocido: {tid}")
        if spec["table"] != table:
            continue
        vanilla = load_table(table, "vanilla") if spec["needs_vanilla"] else None
        spec["fn"](doc, vanilla)
        applied.append(tid)
    return doc, {"transforms": applied, **doc.pop("_report", {})}


_FNAME_KEYS = ("Name", "EnumType", "InnerType", "ArrayType", "StructType", "ObjectName")


def repair_namemap(doc):
    """Registra los FName usados por una tabla antes de enviarla a UAssetGUI.

    UAssetGUI no crea entradas de ``NameMap`` al serializar: ante un FName
    ausente termina sin producir el uasset. Las transformaciones pueden copiar
    aliases desde otra tabla/base (por ejemplo Perfect Dodge sin lock-on), por
    lo que la reparación debe correr para *toda* tabla compilada, no sólo para
    los pipelines de mini-boss.
    """
    name_map = doc.get("NameMap")
    if not isinstance(name_map, list):
        return 0
    if not doc.get("Exports"):   # uasset sin tabla: nada que registrar
        return 0

    known = set(name_map)
    added = []

    def add(value):
        if not isinstance(value, str) or not value or value in known:
            return
        # Los elementos de un ArrayProperty llevan el indice como ``Name`` ("0",
        # "1", ...): no son FNames y meterlos infla el NameMap y aleja el uasset
        # del vanilla byte a byte.
        if value.isdigit():
            return
        known.add(value)
        added.append(value)

    def walk(value):
        if isinstance(value, dict):
            t = value.get("$type", "")
            if "NamePropertyData" in t or "EnumPropertyData" in t:
                add(value.get("Value"))
            # Todo FName del serializado: nombre de la propiedad y los nombres de
            # tipo (enum/inner/array/struct/objeto). Cualquiera de estos ausente
            # hace que fromjson termine sin escribir el uasset.
            for key in _FNAME_KEYS:
                add(value.get(key))
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)

    for row in rows(doc):
        add(row.get("Name"))
        walk(row)
    name_map.extend(added)
    return len(added)


def compile_pak(transform_ids, pak_name, work_dir, verify_result=True, toml_dir=None):
    """Aplica transforms, escribe JSON por tabla tocada, y packea a un pak Zen.

    toml_dir (opcional): carpeta con <Tabla>.toml aplicados sobre las tablas que
    este pak incluye (las que no estan en el pak se saltean).
    Devuelve dict del toolchain.stage_and_pack + reporte por tabla.
    """
    work_dir = Path(work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)

    # Agrupar transforms por tabla (respetando orden dado).
    by_table = {}
    for tid in transform_ids:
        spec = REGISTRY.get(tid)
        if not spec:
            raise KeyError(f"Transform desconocido: {tid}")
        by_table.setdefault(spec["table"], []).append((tid, spec))

    # Patches TOML del usuario, por tabla (solo las que este pak incluye).
    toml_by_table = {}
    if toml_dir:
        import toml_patch
        for tf in Path(toml_dir).glob("*.toml"):
            toml_by_table[toml_patch.table_of(tf)] = toml_patch.load_toml(tf)

    uassets = []
    reports = {}
    for table, specs in by_table.items():
        base = specs[0][1]["base"]
        doc = load_table(table, base)
        vanilla = None
        if any(s[1]["needs_vanilla"] for s in specs):
            vanilla = load_table(table, "vanilla")
        for tid, spec in specs:
            spec["fn"](doc, vanilla)
        reports[table] = {"transforms": [t for t, _ in specs], **doc.pop("_report", {})}
        if table in toml_by_table:
            import toml_patch
            reports[table]["tomlPatched"] = toml_patch.apply_toml_to_doc(doc, toml_by_table[table])
        reports[table]["nameMapAdded"] = repair_namemap(doc)
        out_json = work_dir / f"{table}.json"
        out_json.write_text(json.dumps(doc), encoding="utf-8")
        uassets.append(toolchain.fromjson(out_json, work_dir / f"{table}.uasset"))

    result = toolchain.stage_and_pack(uassets, pak_name, work_dir, verify_result)
    result["reports"] = reports
    return result


def compile_targets(targets, work_dir, verify_result=True, toml_dir=None):
    """Compila varios paks. targets = [{'name':..., 'transforms':[...]}].

    Cada pak en su subdir (evita colision de <Table>.json). Devuelve
    {name: result}. Los .pak/.ucas/.utoc quedan en work_dir/<name>/.
    """
    work_dir = Path(work_dir)
    out = {}
    for t in targets:
        sub = work_dir / t["name"]
        out[t["name"]] = compile_pak(t["transforms"], t["name"], sub, verify_result, toml_dir)
    return out


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Compila un pak desde transforms.")
    ap.add_argument("--transforms", required=True, help="lista separada por comas.")
    ap.add_argument("--name", default="StellarSouls-Compiled")
    ap.add_argument("--out", required=True)
    ap.add_argument("--no-verify", action="store_true")
    args = ap.parse_args()
    res = compile_pak(args.transforms.split(","), args.name, Path(args.out),
                      verify_result=not args.no_verify)
    print(json.dumps({k: str(v) if isinstance(v, Path) else v
                      for k, v in res.items()}, indent=2, ensure_ascii=False))
