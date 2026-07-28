"""extras - tweaks de gameplay opcionales (BETA) sobre CharacterTable.

Ideas inspiradas en automod (jpabscale) pero con VALORES PROPIOS (no se copian
patches de otros autores). Cada funcion muta el doc de CharacterTable in place.

- player_qol: stacks de municion/consumibles altos, mas shield-regen, attack
  speed, y drenaje de HP leve al pegar. Calidad de vida.
- harder_enemies(mult): multiplica MaxHP/MaxShield de enemigos (x2..x6).
- longer_tachy: mas duracion de Tachy (sube MaxTachyGauge del Player).
"""
from __future__ import annotations


def _rows(doc):
    return doc["Exports"][0]["Table"]["Data"]


def _prop(row, name):
    return next((p for p in row["Value"] if p["Name"] == name), None)


def _get(row, name):
    p = _prop(row, name)
    return p.get("Value") if p else None


def _set(row, name, value):
    p = _prop(row, name)
    if p:
        p["Value"] = value
        p["IsZero"] = value in (0, 0.0, None)
        return True
    return False


def _find(rows, name):
    return next((r for r in rows if r.get("Name") == name), None)


# Valores propios (calidad de vida generosa, no de un autor especifico).
_QOL_VALUES = {
    "StackBullet1": 999, "StackBullet2": 999, "StackBullet3": 999,
    "StackBullet4": 999, "StackBullet5": 999, "StackBullet6": 999,
    "StackConsumable1": 99, "StackConsumable2": 99, "StackConsumable3": 99,
    "StackConsumable4": 99, "StackConsumable5": 99, "StackConsumable6": 99,
    "StackConsumable7": 99,
    "ShieldRegenPerSecond": 120.0, "ShieldRegenPerSecondWhenBattle": 30.0,
    "AttackSpeed": 1.3,
}

_BASE_ATTRIBUTE_VALUES = {
    "MaxHP": 3000,
    "MaxShield": 1000,
    "DamageReductionPerShieldBock": 0.2,
}

_AMMO_100X_VALUES = {
    "StackBullet1": 3000, "StackBullet2": 300, "StackBullet3": 1600,
    "StackBullet4": 1200, "StackBullet5": 6000, "StackBullet6": 800,
}


def player_qol(ct_doc) -> int:
    player = _find(_rows(ct_doc), "Player")
    if not player:
        return 0
    n = 0
    for name, val in _QOL_VALUES.items():
        if _set(player, name, val):
            n += 1
    return n


def _qol_fields(ct_doc, fields) -> int:
    player = _find(_rows(ct_doc), "Player")
    if not player:
        return 0
    return sum(1 for name in fields if _set(player, name, _QOL_VALUES[name]))


def ammo_stacks(ct_doc, stack_size=999, values=None) -> int:
    player = _find(_rows(ct_doc), "Player")
    fields = [name for name in _QOL_VALUES if name.startswith("StackBullet")]
    selected = values if isinstance(values, dict) else {}
    return sum(1 for name in fields
               if player and _set(player, name, int(selected.get(name, stack_size))))


def consumable_stacks(ct_doc, stack_size=99, values=None) -> int:
    player = _find(_rows(ct_doc), "Player")
    fields = [name for name in _QOL_VALUES if name.startswith("StackConsumable")]
    selected = values if isinstance(values, dict) else {}
    return sum(1 for name in fields
               if player and _set(player, name, int(selected.get(name, stack_size))))


def shield_regen(ct_doc, normal=120.0, combat=30.0) -> int:
    player = _find(_rows(ct_doc), "Player")
    return sum(1 for name, value in {
        "ShieldRegenPerSecond": normal,
        "ShieldRegenPerSecondWhenBattle": combat,
    }.items() if player and _set(player, name, float(value)))


def attack_speed(ct_doc, multiplier=1.3) -> int:
    player = _find(_rows(ct_doc), "Player")
    return int(bool(player and _set(player, "AttackSpeed", float(multiplier))))


def base_attributes(ct_doc, max_hp=3000, max_shield=1000,
                    shield_reduction_percent=20.0) -> int:
    player = _find(_rows(ct_doc), "Player")
    if not player:
        return 0
    values = {
        "MaxHP": int(max_hp),
        "MaxShield": int(max_shield),
        "DamageReductionPerShieldBock": float(shield_reduction_percent) / 100.0,
    }
    return sum(1 for name, value in values.items()
               if _set(player, name, value))


def attribute_shield_regen(ct_doc, normal=160.0, combat=20.0) -> int:
    player = _find(_rows(ct_doc), "Player")
    if not player:
        return 0
    values = {
        "ShieldRegenPerSecond": float(normal),
        "ShieldRegenPerSecondWhenBattle": float(combat),
    }
    return sum(1 for name, value in values.items() if _set(player, name, value))


def high_gauge_capacity(ct_doc, beta=1500, burst=2000) -> int:
    player = _find(_rows(ct_doc), "Player")
    if not player:
        return 0
    return sum(1 for name, value in {
        "MaxBetaGauge": int(beta), "MaxBurstGauge": int(burst),
    }.items() if _set(player, name, value))


def passive_hp_regen(ct_doc, per_second=20.0) -> bool:
    player = _find(_rows(ct_doc), "Player")
    return bool(player and _set(player, "HPRegenPerSecond", float(per_second)))


def fishing_power(ct_doc, power=50.0) -> bool:
    player = _find(_rows(ct_doc), "Player")
    return bool(player and _set(player, "FishingAttackPower", float(power)))


def ammo_100x(ct_doc, multiplier=100.0, values=None) -> int:
    player = _find(_rows(ct_doc), "Player")
    if not player:
        return 0
    vanilla = {
        "StackBullet1": 30, "StackBullet2": 3, "StackBullet3": 16,
        "StackBullet4": 12, "StackBullet5": 60, "StackBullet6": 8,
    }
    selected = values if isinstance(values, dict) else {}
    return sum(1 for name, value in vanilla.items()
               if _set(player, name, int(selected.get(
                   name, round(value * float(multiplier))))))


def longer_tachy(ct_doc, gauge=18000) -> bool:
    player = _find(_rows(ct_doc), "Player")
    return bool(player and _set(player, "MaxTachyGauge", gauge))


def hp_drain(ct_doc) -> bool:
    """Cura HP al pegar (Player). Valor propio moderado."""
    player = _find(_rows(ct_doc), "Player")
    if not player:
        return False
    a = _set(player, "DrainHpByAttackPowerRate", 0.03)
    b = _set(player, "DrainHpFixedValue", 30)
    return bool(a or b)


def harder_enemies(ct_doc, mult=2.0) -> int:
    """Multiplica HP/escudo sólo de enemigos normales.

    Bosses y clones mini-boss pertenecen a opciones independientes.
    """
    n = 0
    for r in _rows(ct_doc):
        name = r.get("Name", "")
        if (name.startswith("Player") or name.endswith("_MB")
                or _get(r, "ActorType") == "ActorType_BossMonster"):
            continue
        for field in ("MaxHP", "MaxShield"):
            v = _get(r, field)
            if isinstance(v, int) and v > 0:
                _set(r, field, int(round(v * mult)))
                n += 1
    return n


# Registro para wiring por nombre desde las respuestas del cuestionario.
def apply_extras(ct_doc, extras: list, harder_mult=2.0) -> dict:
    rep = {}
    if "playerQol" in extras:
        rep["playerQol"] = player_qol(ct_doc)
    if "ammoStacks" in extras:
        rep["ammoStacks"] = ammo_stacks(ct_doc)
    if "consumableStacks" in extras:
        rep["consumableStacks"] = consumable_stacks(ct_doc)
    if "shieldRegen" in extras:
        rep["shieldRegen"] = shield_regen(ct_doc)
    if "attackSpeed" in extras:
        rep["attackSpeed"] = attack_speed(ct_doc)
    if "baseAttributes" in extras:
        rep["baseAttributes"] = base_attributes(ct_doc)
    if "attributeShieldRegen" in extras:
        rep["attributeShieldRegen"] = attribute_shield_regen(ct_doc)
    if "highGaugeCapacity" in extras:
        rep["highGaugeCapacity"] = high_gauge_capacity(ct_doc)
    if "passiveHpRegen" in extras:
        rep["passiveHpRegen"] = passive_hp_regen(ct_doc)
    if "fishingPower" in extras:
        rep["fishingPower"] = fishing_power(ct_doc)
    if "ammo100x" in extras:
        rep["ammo100x"] = ammo_100x(ct_doc)
    if "longerTachy" in extras:
        rep["longerTachy"] = longer_tachy(ct_doc)
    if "hpDrain" in extras:
        rep["hpDrain"] = hp_drain(ct_doc)
    if "harderEnemies" in extras:
        rep["harderEnemies"] = harder_enemies(ct_doc, harder_mult)
    return rep
