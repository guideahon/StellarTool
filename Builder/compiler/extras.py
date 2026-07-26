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


def ammo_stacks(ct_doc) -> int:
    return _qol_fields(ct_doc, [x for x in _QOL_VALUES if x.startswith("StackBullet")])


def consumable_stacks(ct_doc) -> int:
    return _qol_fields(ct_doc, [x for x in _QOL_VALUES if x.startswith("StackConsumable")])


def shield_regen(ct_doc) -> int:
    return _qol_fields(ct_doc, ["ShieldRegenPerSecond", "ShieldRegenPerSecondWhenBattle"])


def attack_speed(ct_doc) -> int:
    return _qol_fields(ct_doc, ["AttackSpeed"])


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
    """Multiplica MaxHP/MaxShield de filas de enemigo (no Player, no _MB clones
    ya buffeados). Global -> combo con mini-boss para picos + piso mas duro."""
    n = 0
    for r in _rows(ct_doc):
        name = r.get("Name", "")
        if name.startswith("Player") or name.endswith("_MB"):
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
    if "longerTachy" in extras:
        rep["longerTachy"] = longer_tachy(ct_doc)
    if "hpDrain" in extras:
        rep["hpDrain"] = hp_drain(ct_doc)
    if "harderEnemies" in extras:
        rep["harderEnemies"] = harder_enemies(ct_doc, harder_mult)
    return rep
