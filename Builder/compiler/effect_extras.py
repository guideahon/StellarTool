"""effect_extras - tweaks BETA sobre EffectTable (valores propios).

Ideas inspiradas en automod (credito en Shoutouts); implementacion y valores
propios. Cada funcion muta el doc EffectTable (UAssetAPI) in place.

- no_fall_damage: neutraliza la muerte/dano por caida.
- no_environment_death: sin muerte instantanea por oceano profundo / arena.
- tachy_reduce: baja el consumo de Tachy (mas duracion).
- stronger_gear(mult): multiplica el efecto de los engranajes (Gear_*).
"""
from __future__ import annotations


def _rows(doc):
    return doc["Exports"][0]["Table"]["Data"]


def _idx(doc):
    return {r.get("Name"): r for r in _rows(doc)}


def _prop(row, name):
    return next((p for p in row["Value"] if p["Name"] == name), None)


def _set(row, name, value):
    p = _prop(row, name)
    if p:
        p["Value"] = value
        p["IsZero"] = value in (0, 0.0, "+0", None)
        return True
    return False


def _neutralize_death(row):
    n = 0
    if _set(row, "Action1", "EffectAction_None"):
        n += 1
    _set(row, "ActionValue1", None)
    _set(row, "ActorState1", "ActorState_None")
    # dano por HP-rate (caida): a 0
    p = _prop(row, "CalculationValue")
    if p and isinstance(p.get("Value"), (int, float)) and p["Value"] < 0:
        _set(row, "CalculationValue", 0)
    return n


def no_fall_damage(et_doc) -> int:
    idx = _idx(et_doc)
    n = 0
    for name in ("LV_Dead_Falling", "LV_Dead_Falling_HPRateDamage",
                 "LV_Dead_Falling_DonotWarp", "LV_Dead_Falling_HPRateDamage_NoSound"):
        r = idx.get(name)
        if r:
            n += _neutralize_death(r)
    return n


def no_environment_death(et_doc) -> int:
    idx = _idx(et_doc)
    n = 0
    for name in ("LV_Dead_Ocean", "LV_Dead_DesertSandTrap"):
        r = idx.get(name)
        if r:
            n += _neutralize_death(r)
    return n


def tachy_reduce(et_doc, value=0.6) -> bool:
    r = _idx(et_doc).get("P_Eve_SkillTree_TachyGaugeReduceConsumeRate")
    return bool(r and _set(r, "CalculationValue", value))


def stronger_gear(et_doc, mult=2.0) -> int:
    """Multiplica CalculationValue positivo de filas Gear_* (beneficios).
    Evita filas de dano recibido (_HitDmgUp) para no empeorar los perjuicios."""
    n = 0
    for r in _rows(et_doc):
        name = r.get("Name", "")
        if not name.startswith("Gear_") or "_HitDmgUp" in name:
            continue
        p = _prop(r, "CalculationValue")
        if p and isinstance(p.get("Value"), (int, float)) and not isinstance(p["Value"], bool) and p["Value"] > 0:
            v = p["Value"] * mult
            p["Value"] = int(round(v)) if isinstance(p["Value"], int) else v
            p["IsZero"] = False
            n += 1
    return n


def auto_gauge_recovery(et_doc) -> int:
    """Beta al parry perfecto / Burst al dodge perfecto SIN el nodo del arbol.

    Los efectos ya existen y los dispara la skill; solo estan gateados por
    `ConditionActive_ConstructorActorAcquisitionAlias` (ParryUpgrade3_x /
    EvadeUpgrade2_x). Limpiando ese gate quedan activos desde el inicio.
    """
    idx = _idx(et_doc)
    n = 0
    for name in ("P_Eve_SkillTree_JustParry_BetaGauge1", "P_Eve_SkillTree_JustParry_BetaGauge2",
                 "P_Eve_SkillTree_JustEvade_BurstGauge1", "P_Eve_SkillTree_JustEvade_BurstGauge2"):
        r = idx.get(name)
        if r and _set(r, "ConditionActive_ConstructorActorAcquisitionAlias", None):
            n += 1
    return n


def tumbler_heal(et_doc, value=60.0) -> bool:
    """Curación base del Tumbler usada por el release Combat+Outfit."""
    r = _idx(et_doc).get("Item_HP_RPotion")
    return bool(r and _set(r, "CalculationValue", value))


# Que extras tocan EffectTable (para saber si hace falta el pase tojson/fromjson).
EFFECT_EXTRAS = {
    "noFallDamage", "noEnvDeath", "tachyReduce", "strongerGear",
    "autoGaugeRecovery", "tumblerHeal",
}


def apply_effect_extras(et_doc, extras: list, gear_mult=2.0) -> dict:
    rep = {}
    if "noFallDamage" in extras:
        rep["noFallDamage"] = no_fall_damage(et_doc)
    if "noEnvDeath" in extras:
        rep["noEnvDeath"] = no_environment_death(et_doc)
    if "tachyReduce" in extras:
        rep["tachyReduce"] = tachy_reduce(et_doc)
    if "strongerGear" in extras:
        rep["strongerGear"] = stronger_gear(et_doc, gear_mult)
    if "autoGaugeRecovery" in extras:
        rep["autoGaugeRecovery"] = auto_gauge_recovery(et_doc)
    if "tumblerHeal" in extras:
        rep["tumblerHeal"] = tumbler_heal(et_doc)
    return rep
