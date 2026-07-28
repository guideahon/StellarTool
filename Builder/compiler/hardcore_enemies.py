"""Ajustes de enemigos limitados a Hardcore (DifficultyStatGroupTable).

La tabla expresa porcentajes estáticos. Los presets reproducen la semántica
medida en el mod de referencia, pero se calculan desde los valores vanilla:

* Main: bosses HP x2, shield x1.25, physical/ranged damage x1.25.
* Insane: bosses HP x3, shield x2, physical/ranged damage x1.25.
Los enemigos regulares no se modifican: pertenecen al perfil nativo de
Stellar Souls. Esta opción es exclusivamente para bosses.

Maelstrom se excluye siempre. Las demás dificultades nunca se recorren.
"""
from __future__ import annotations


MAIN = "main"
INSANE = "insane"
PRESETS = {MAIN, INSANE}

def _rows(doc):
    return doc["Exports"][0]["Table"]["Data"]


def _props(row):
    return {p["Name"]: p for p in row["Value"]}


def _set(props, name, value):
    prop = props.get(name)
    if not prop or prop.get("Value") == value:
        return False
    prop["Value"] = value
    prop["IsZero"] = value in (0, 0.0, None)
    return True


def apply(doc, preset=MAIN) -> dict:
    """Aplica un preset y devuelve conteos auditables por clase de enemigo."""
    if preset not in PRESETS:
        raise ValueError(f"Preset Hardcore desconocido: {preset}")

    report = {"preset": preset, "regularRows": 0, "bossRows": 0,
              "changedValues": 0, "excludedMaelstrom": 0}
    boss_hp = 2.0 if preset == MAIN else 3.0
    boss_shield = 1.25 if preset == MAIN else 2.0

    for row in _rows(doc):
        p = _props(row)
        if p.get("DifficultyAlias", {}).get("Value") != "HardMode":
            continue
        alias = str(p.get("DifficultyStatGroupAlias", {}).get("Value", ""))
        if "maelstrom" in alias.casefold():
            report["excludedMaelstrom"] += 1
            continue

        try:
            row_id = int(p.get("ID", {}).get("Value", row.get("Name", 0)))
        except (TypeError, ValueError):
            continue
        if row_id < 301:
            continue

        changed = 0
        for field in ("StatValue1", "StatValue3"):
            value = p.get(field, {}).get("Value")
            if isinstance(value, (int, float)) and not isinstance(value, bool) and value > 0:
                changed += _set(p, field, value * 1.25)
        for field, mult in (("StatValue4", boss_hp), ("StatValue5", boss_shield)):
            value = p.get(field, {}).get("Value")
            if isinstance(value, (int, float)) and not isinstance(value, bool) and value > 0:
                changed += _set(p, field, value * mult)
        if changed:
            report["bossRows"] += 1
            report["changedValues"] += changed
    return report
