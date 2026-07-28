"""skill_extras - tweaks BETA de sensacion de combate sobre SkillTable.

Valores propios. Implementa lo que la SkillTable permite de forma clara y
acotada; el "cancel routing" profundo (dodge/guard cancelling, rutas de combo)
vive en SkillCommandTable / SkillActiveStepTable y NO se toca aca.

- forgiving_just(mult): agranda la ventana de dodge/parry perfecto
  (JustActionTime) en las 9 skills que la usan (Sword/Tachy/Fusion).
- extra_air_dodge(count): mas usos de esquive aereo (UsableCount).
"""
from __future__ import annotations


def _rows(doc):
    return doc["Exports"][0]["Table"]["Data"]


def _prop(row, name):
    return next((p for p in row["Value"] if p["Name"] == name), None)


def _set(row, name, value):
    p = _prop(row, name)
    if p:
        p["Value"] = value
        p["IsZero"] = value in (0, 0.0, "+0", None)
        return True
    return False


def forgiving_just(sk_doc, mult=1.5) -> int:
    """JustActionTime x mult (ventana de parry/dodge perfecto mas indulgente).
    Solo toca skills del jugador que ya tienen ventana > 0."""
    n = 0
    for r in _rows(sk_doc):
        if not str(r.get("Name", "")).startswith("P_Eve_"):
            continue
        p = _prop(r, "JustActionTime")
        if p and isinstance(p.get("Value"), (int, float)) and not isinstance(p["Value"], bool) and p["Value"] > 0:
            p["Value"] = round(p["Value"] * mult, 4)
            p["IsZero"] = False
            n += 1
    return n


def extra_air_dodge(sk_doc, count=2) -> int:
    """Mas esquives aereos seguidos (UsableCount de las skills Air Evade)."""
    n = 0
    for r in _rows(sk_doc):
        name = str(r.get("Name", ""))
        if not name.startswith("P_Eve_") or "Air" not in name or "Evade" not in name:
            continue
        p = _prop(r, "UsableCount")
        if p and isinstance(p.get("Value"), int) and 0 < p["Value"] < count:
            p["Value"] = count
            p["IsZero"] = False
            n += 1
    return n


def dash_cooldown_4s(sk_doc) -> int:
    idx = {r.get("Name"): r for r in _rows(sk_doc)}
    n = 0
    for name in (
            "P_Eve_Sword_Normal_DashAttack1_1",
            "P_Eve_Sword_Normal_DashAttack3_1",
            "P_Eve_Sword_Normal_DashAttack4_1"):
        row = idx.get(name)
        if row and _set(row, "CoolTime", 4.0):
            n += 1
    return n


def drone_scan_cooldown_5s(sk_doc) -> bool:
    row = next((r for r in _rows(sk_doc)
                if r.get("Name") == "N_Drone_Normal_Scan1_1"), None)
    return bool(row and _set(row, "CoolTime", 5.0))


# Extras que tocan SkillTable.
SKILL_EXTRAS = {
    "forgivingJust", "extraAirDodge", "dashCooldown4", "droneScanBoost",
}


def apply_skill_extras(sk_doc, extras: list, just_mult=1.5, air_count=2) -> dict:
    rep = {}
    if "forgivingJust" in extras:
        rep["forgivingJust"] = forgiving_just(sk_doc, just_mult)
    if "extraAirDodge" in extras:
        rep["extraAirDodge"] = extra_air_dodge(sk_doc, air_count)
    if "dashCooldown4" in extras:
        rep["dashCooldown4"] = dash_cooldown_4s(sk_doc)
    if "droneScanBoost" in extras:
        rep["droneScanBoost"] = drone_scan_cooldown_5s(sk_doc)
    return rep
