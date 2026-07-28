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
    # Los efectos de caída pueden encadenar la muerte o el warp en cualquiera
    # de los cinco slots. Conservamos acciones inocuas (p. ej. StopTheater).
    for slot in range(1, 6):
        action = _prop(row, f"Action{slot}")
        if action and action.get("Value") in (
                "EffectAction_ImmediateDeath", "EffectAction_WarpToSafeLocation"):
            _set(row, f"Action{slot}", "EffectAction_None")
            _set(row, f"ActionValue{slot}", None)
            n += 1
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
                 "LV_Dead_Falling_DonotWarp", "LV_Dead_Falling_HPRateDamage_NoSound",
                 "LV_Dead_Falling_KeepTheater"):
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


def no_water_death(et_doc) -> int:
    row = _idx(et_doc).get("LV_Dead_Ocean")
    return _neutralize_death(row) if row else 0


def no_sand_death(et_doc) -> int:
    row = _idx(et_doc).get("LV_Dead_DesertSandTrap")
    return _neutralize_death(row) if row else 0


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


def beta_parry_recovery(et_doc) -> int:
    idx = _idx(et_doc)
    return sum(1 for name in ("P_Eve_SkillTree_JustParry_BetaGauge1", "P_Eve_SkillTree_JustParry_BetaGauge2")
               if idx.get(name) and _set(idx[name], "ConditionActive_ConstructorActorAcquisitionAlias", None))


def burst_dodge_recovery(et_doc) -> int:
    idx = _idx(et_doc)
    return sum(1 for name in ("P_Eve_SkillTree_JustEvade_BurstGauge1", "P_Eve_SkillTree_JustEvade_BurstGauge2")
               if idx.get(name) and _set(idx[name], "ConditionActive_ConstructorActorAcquisitionAlias", None))


def tumbler_heal(et_doc, value=60.0) -> bool:
    """Curación base del Tumbler usada por el release Combat+Outfit."""
    r = _idx(et_doc).get("Item_HP_RPotion")
    return bool(r and _set(r, "CalculationValue", value))


def gauge_recovery_over_time(et_doc) -> int:
    """Recuperación sostenida tras parry/dodge perfecto, conservando el skill gate."""
    idx = _idx(et_doc)
    rates = {
        "P_Eve_SkillTree_JustParry_BetaGauge1": 8.0,
        "P_Eve_SkillTree_JustParry_BetaGauge2": 6.0,
        "P_Eve_SkillTree_JustEvade_BurstGauge1": 4.0,
        "P_Eve_SkillTree_JustEvade_BurstGauge2": 3.0,
    }
    n = 0
    for name, rate in rates.items():
        row = idx.get(name)
        if not row:
            continue
        for field, value in (
                ("LoopTargetFilterAlias", "Self"),
                ("CalculationMultipleValue", rate),
                ("LoopIntervalTime", 1.0),
                ("StartDelayTime", 1.0),
                ("LifeTime", 11.0)):
            if _set(row, field, value):
                n += 1
    return n


def drone_scan_duration(et_doc) -> int:
    row = _idx(et_doc).get("N_Drone_Scan")
    if not row:
        return 0
    return sum(1 for field, value in (
        ("ExpansionValue1", "1"), ("LifeTime", 10.0),
    ) if _set(row, field, value))


def gun_gorgon_free_rotation(et_doc) -> bool:
    row = _idx(et_doc).get("P_Eve_Stance_GunGorgon")
    # CUE4Parse muestra el enum calificado, pero UAssetAPI legacy espera el
    # FName sin el prefijo del tipo.
    return bool(row and _set(row, "ActorState1", "ActorState_None"))


# Que extras tocan EffectTable (para saber si hace falta el pase tojson/fromjson).
EFFECT_EXTRAS = {
    "noFallDamage", "noEnvDeath", "tachyReduce", "strongerGear",
    "autoGaugeRecovery", "noWaterDeath", "noSandDeath", "betaParryRecovery",
    "burstDodgeRecovery", "tumblerHeal",
    "gaugeRecoveryOverTime", "droneScanBoost", "gunGorgonRotation",
}


def apply_effect_extras(et_doc, extras: list, gear_mult=2.0) -> dict:
    rep = {}
    if "noFallDamage" in extras:
        rep["noFallDamage"] = no_fall_damage(et_doc)
    if "noEnvDeath" in extras:
        rep["noEnvDeath"] = no_environment_death(et_doc)
    if "noWaterDeath" in extras:
        rep["noWaterDeath"] = no_water_death(et_doc)
    if "noSandDeath" in extras:
        rep["noSandDeath"] = no_sand_death(et_doc)
    if "tachyReduce" in extras:
        rep["tachyReduce"] = tachy_reduce(et_doc)
    if "strongerGear" in extras:
        rep["strongerGear"] = stronger_gear(et_doc, gear_mult)
    if "autoGaugeRecovery" in extras:
        rep["autoGaugeRecovery"] = auto_gauge_recovery(et_doc)
    if "betaParryRecovery" in extras:
        rep["betaParryRecovery"] = beta_parry_recovery(et_doc)
    if "burstDodgeRecovery" in extras:
        rep["burstDodgeRecovery"] = burst_dodge_recovery(et_doc)
    if "tumblerHeal" in extras:
        rep["tumblerHeal"] = tumbler_heal(et_doc)
    if "gaugeRecoveryOverTime" in extras:
        rep["gaugeRecoveryOverTime"] = gauge_recovery_over_time(et_doc)
    if "droneScanBoost" in extras:
        rep["droneScanBoost"] = drone_scan_duration(et_doc)
    if "gunGorgonRotation" in extras:
        rep["gunGorgonRotation"] = gun_gorgon_free_rotation(et_doc)
    return rep
