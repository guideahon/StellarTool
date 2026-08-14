"""Configuración granular de dificultad para bosses y enemigos normales.

Los filtros usan la clasificación nativa de CharacterTable. No se inventan
filas ni se convierten bosses en mini-bosses: sólo se escalan propiedades que
ya existen en las tablas vanilla.
"""
from __future__ import annotations


def _rows(doc):
    return doc["Exports"][0]["Table"]["Data"]


def _prop(row, name):
    return next((p for p in row.get("Value", []) if p.get("Name") == name), None)


def _get(row, name):
    p = _prop(row, name)
    return p.get("Value") if p else None


def _set(row, name, value):
    p = _prop(row, name)
    if not p or p.get("Value") == value:
        return 0
    p["Value"] = value
    p["IsZero"] = value in (0, 0.0, None)
    return 1


def _is_boss(row):
    return (_get(row, "ActorType") == "ActorType_BossMonster"
            or str(row.get("Name", "")).endswith(("_MB", "_OW")))


def _selected(row, boss):
    return _is_boss(row) == boss and _get(row, "Name") != "Player"


def apply_character(doc, boss, config):
    config = config or {}
    n = 0
    for row in _rows(doc):
        if not _selected(row, boss):
            continue
        if config.get("health"):
            v = _get(row, "MaxHP")
            if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                n += _set(row, "MaxHP", int(round(v * float(config.get("healthMultiplier", 1.0)))))
        if config.get("attack"):
            mult = float(config.get("attackMultiplier", 1.0))
            for field in ("PhysicAttackPower", "RangeAttackPower"):
                v = _get(row, field)
                if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                    value = v * mult
                    n += _set(row, field, int(round(value)) if isinstance(v, int) else value)
        if config.get("size"):
            v = _get(row, "MeshScale")
            if isinstance(v, (int, float)) and not isinstance(v, bool):
                n += _set(row, "MeshScale", min(v * float(config.get("sizeMultiplier", 1.0)), 3.0))
        if config.get("removeShield"):
            n += _set(row, "MaxShield", 0)
            n += _set(row, "ShieldBlock", 0.0)
        if config.get("staggerImmunity"):
            v = _get(row, "HitDefenseLevel")
            if isinstance(v, (int, float)) and v < 5:
                n += _set(row, "HitDefenseLevel", 5)
    return n


def apply_skills(doc, boss, config):
    """Escala daño de skills del arquetipo elegido sin tocar ataques de EVE."""
    if not (config or {}).get("attack"):
        return 0
    # SkillTable no repite ActorType. Se relaciona por el prefijo del alias
    # CharacterTable, igual que el transform nativo de daño de enemigos.
    import table_compiler
    characters = table_compiler.load_table("CharacterTable", "vanilla")
    aliases = {str(row.get("Name", "")) + "_" for row in _rows(characters)
               if _selected(row, boss)}
    n = 0
    mult = float(config.get("attackMultiplier", 1.0))
    for row in _rows(doc):
        name = str(row.get("Name", ""))
        if not any(name.startswith(alias) for alias in aliases):
            continue
        for field in ("AttackDamageRate", "ShieldAttackDamageRate", "FixedDamage"):
            p = _prop(row, field)
            v = p.get("Value") if p else None
            if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                value = v * mult
                p["Value"] = int(round(value)) if isinstance(v, int) else value
                p["IsZero"] = False
                n += 1
    return n


def apply_xp(doc, boss, config):
    """Escala campos XP/EXP existentes en RewardGroupTable.

    La tabla de recompensas no siempre trae una propiedad de experiencia en
    todas las versiones; en ese caso devuelve cero y el manifest lo deja claro.
    """
    if not (config or {}).get("xp"):
        return 0
    mult = float(config.get("xpMultiplier", 1.0))
    n = 0
    for row in _rows(doc):
        # No hay un vínculo universal reward->actor; sólo se escriben campos
        # explícitamente identificados como experiencia, nunca cantidades de loot.
        for p in row.get("Value", []):
            name = str(p.get("Name", ""))
            if not any(token in name.casefold() for token in ("xp", "exp", "experience")):
                continue
            v = p.get("Value")
            if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                value = v * mult
                p["Value"] = int(round(value)) if isinstance(v, int) else value
                p["IsZero"] = False
                n += 1
    return n
