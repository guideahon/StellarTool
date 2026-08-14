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
    return _is_boss(row) == boss and row.get("Name") != "Player"


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
        if config.get("shieldRegen"):
            mult = float(config.get("shieldRegenMultiplier", 1.0))
            for field in ("ShieldRegenPerSecond", "ShieldRegenPerSecondWhenBattle"):
                v = _get(row, field)
                if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                    n += _set(row, field, v * mult)
        if config.get("shieldDamageReduction"):
            v = _get(row, "BaseDamageReductionByShield")
            if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                n += _set(row, "BaseDamageReductionByShield",
                          min(0.99, v * float(config.get("shieldDamageReductionMultiplier", 1.0))))
        if config.get("stamina"):
            v = _get(row, "MaxStamina")
            if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                value = v * float(config.get("staminaMultiplier", 1.0))
                n += _set(row, "MaxStamina", int(round(value)) if isinstance(v, int) else value)
        if config.get("staminaRegen"):
            v = _get(row, "StaminaRegenPerSecond")
            if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                n += _set(row, "StaminaRegenPerSecond",
                          v * float(config.get("staminaRegenMultiplier", 1.0)))
        if config.get("attackSpeed"):
            v = _get(row, "AttackSpeed")
            if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                n += _set(row, "AttackSpeed", v * float(config.get("attackSpeedMultiplier", 1.0)))
        if config.get("moveSpeed"):
            v = _get(row, "MoveSpeed")
            if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0:
                value = v * float(config.get("moveSpeedMultiplier", 1.0))
                n += _set(row, "MoveSpeed", int(round(value)) if isinstance(v, int) else value)
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


def _reward_aliases(boss):
    """Obtiene grupos de recompensa vinculados a la clase desde CharacterTable."""
    try:
        import table_compiler
        characters = table_compiler.load_table("CharacterTable", "vanilla")
    except (FileNotFoundError, KeyError):
        return set()
    aliases = set()
    for row in _rows(characters):
        if _selected(row, boss):
            for field in ("RewardGroupAlias", "RewardGroup"):
                value = _get(row, field)
                if isinstance(value, str) and value not in ("None", ""):
                    aliases.add(value)
    return aliases


def _belongs_to_alias(row, aliases):
    if not aliases:
        return False
    if str(row.get("Name", "")) in aliases:
        return True
    for p in row.get("Value", []):
        value = p.get("Value")
        if isinstance(value, str) and value in aliases:
            return True
    return False


def apply_rewards(doc, boss, config):
    """Escala XP y drops sólo de grupos vinculados a los arquetipos elegidos.

    Si una versión del juego no expone el vínculo, no aplica un cambio global
    silencioso: devuelve un reporte de grupos no encontrados.
    """
    config = config or {}
    aliases = _reward_aliases(boss)
    report = {"changed": 0, "matchedGroups": 0, "aliases": sorted(aliases)}
    if not aliases:
        return report
    for row in _rows(doc):
        if not _belongs_to_alias(row, aliases):
            continue
        report["matchedGroups"] += 1
        for p in row.get("Value", []):
            name = str(p.get("Name", ""))
            v = p.get("Value")
            if config.get("xp") and any(token in name.casefold() for token in ("xp", "exp", "experience")):
                value = v * float(config.get("xpMultiplier", 1.0)) if isinstance(v, (int, float)) and not isinstance(v, bool) and v > 0 else v
            elif config.get("drops") and name in ("DropRate", "ItemMinCount", "ItemMaxCount") and isinstance(v, int) and v > 0:
                value = v * float(config.get("dropMultiplier", 1.0))
            else:
                continue
            p["Value"] = int(round(value)) if isinstance(v, int) else value
            p["IsZero"] = False
            report["changed"] += 1
    return report


# Compatibilidad con el nombre anterior usado por transforms ya publicadas.
apply_xp = apply_rewards
