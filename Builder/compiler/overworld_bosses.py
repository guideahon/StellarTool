"""overworld_bosses — BETA: bosses de campo sueltos en el overworld.

Clona bosses reales (los mismos que el juego usa en sus arenas) a variantes
`<alias>_OW` con la vida reducida (por defecto la mitad) y repunta un porcentaje
de spawns EXISTENTES a esos clones. No agrega placements ni toca umaps: usa las
mismas coordenadas validas que ya trae EventSpawnTable, igual que el path de
mini-boss y el de variedad.

La variante existe justamente para que el boss del overworld NO sea el mismo row
que el boss de su arena: bajarle la vida al row original arruinaria el encuentro
guionado. Los `_OW` son filas nuevas.

Se excluyen del pool los story bosses del Nest, Mann/Scarlet, Maelstrom
(SuperLarge flotante), las variantes Nikke, las CHAL_* (boss rush) y los `_Seq`;
ver features/overworld_boss_pool.json, editable sin recompilar.
"""
from __future__ import annotations

import copy
import json
from collections import defaultdict
from pathlib import Path

import miniboss_builder as mb

ID_OFFSET = 720000000
SUFFIX = "_OW"
DEFAULT_PCT = 3
DEFAULT_HEALTH_FACTOR = 0.5

_POOL_PATH = Path(__file__).resolve().parent.parent / "features" / "overworld_boss_pool.json"
_POOL = None


def pool():
    global _POOL
    if _POOL is None:
        _POOL = json.loads(_POOL_PATH.read_text(encoding="utf-8")) if _POOL_PATH.exists() else {}
    return _POOL


def _variant(src_row, index, config):
    """Fila `<alias>_OW`: mismo boss, vida (y escudo) recortados."""
    row = copy.deepcopy(src_row)
    row["Name"] = src_row["Name"] + SUFFIX
    idp = mb.prop(row, "ID")
    if idp and isinstance(idp.get("Value"), int):
        idp["Value"] = idp["Value"] + ID_OFFSET + index

    factor = float(config.get("healthFactor", DEFAULT_HEALTH_FACTOR))
    for field, floor in (("MaxHP", 1), ("MaxShield", 0)):
        p = mb.prop(row, field)
        if p and isinstance(p.get("Value"), (int, float)):
            p["Value"] = max(int(round(p["Value"] * factor)), floor)
            p["IsZero"] = p["Value"] == 0

    if config.get("staggerImmunity", True):
        p = mb.prop(row, "HitDefenseLevel")
        if p is not None and (not isinstance(p.get("Value"), (int, float))
                              or p["Value"] < mb.STAGGER_IMMUNE_LEVEL):
            p["Value"] = mb.STAGGER_IMMUNE_LEVEL
            p["IsZero"] = False

    # Anti-farm: la muerte se guarda, igual que un boss de campo.
    mb.sv(row, "RewardOverrideSaveType", "ESBItemOverrideSaveType_Save")
    if config.get("rewards", True):
        mb.sv(row, "RewardSpawnBucketType", "ESBItemBucketType_World")
    # Sin ejecucion instantanea: un boss con la vida a la mitad se despacharia
    # de un stealth attack.
    p = mb.prop(row, "SpawnEffectList")
    if config.get("executionImmunity", True) and p is not None:
        if not isinstance(p.get("Value"), list):
            p["Value"] = []
        if not any(isinstance(e, dict) and e.get("Value") == mb.MARK for e in p["Value"]):
            p["Value"].append(mb.nameprop(mb.MARK))
    return row


def _picks_for_area(area, config):
    p = pool()
    if config.get("crossArea"):
        return list(p.get("crossPool", []))
    return list(p.get("byArea", {}).get(area, []))


def apply(ct_doc, es_doc, spawns, converted_ids, config=None):
    """Muta ct_doc/es_doc. `spawns` = [(arch, area, row, element)] ya filtrado por
    build_core (sin respawneables ni guionados). Devuelve reporte auditable."""
    config = config or {}
    ct = mb.R(ct_doc)
    ctnames = {r["Name"] for r in ct}
    pct = int(config.get("pct", DEFAULT_PCT) or 0)
    if pct <= 0:
        return {}
    denom = max(1, int(round(100 / pct)))
    areas = config.get("areas")
    areas = set(areas) if areas else None

    per_area = defaultdict(list)
    for arch, area, row, el in spawns:
        if id(el) in converted_ids:
            continue
        if not area:   # zonas sin `Zone_<AREA>_` (tests internos del juego)
            continue
        if areas is not None and area not in areas:
            continue
        per_area[area].append((row, el))

    clones = {}          # alias original -> nombre del clone
    placed = 0
    by_area = defaultdict(int)
    new_ct_names = []

    def clone_for(alias):
        if alias in clones:
            return clones[alias]
        src = mb.find(ct, alias)
        if src is None:
            return None
        row = _variant(src, len(clones), config)
        ct.append(row)
        clones[alias] = row["Name"]
        new_ct_names.append(row["Name"])
        return row["Name"]

    for area in sorted(per_area):
        picks = [a for a in _picks_for_area(area, config) if a in ctnames]
        if not picks:
            continue
        entries = sorted(per_area[area], key=lambda x: str(mb.gv(x[0], "SpawnPointName")))
        # Fase distinta a la del mini-boss (que toma i % denom == 0) para no
        # amontonar boss y mini-boss en el mismo tramo del area.
        offset = denom // 2
        for i, (row, el) in enumerate(entries):
            if i % denom != offset:
                continue
            name = clone_for(picks[(i // denom) % len(picks)])
            if not name:
                continue
            el["Value"] = name
            converted_ids.add(id(el))
            if config.get("xpRewards", True) and area in mb.AREA_XP_GROUP:
                mb.sv(row, "RewardGroup", mb.AREA_XP_GROUP[area])
            placed += 1
            by_area[area] += 1

    mb.addnames(ct_doc, new_ct_names + [mb.MARK, "ESBItemBucketType_World",
                                        "ESBItemOverrideSaveType_Save"])
    mb.addnames(es_doc, list(clones.values()) + list(mb.AREA_XP_GROUP.values()))
    return {"clones": len(clones), "placed": placed, "byArea": dict(sorted(by_area.items())),
            "antiFarm": {"persistentRewards": True,
                          "executionImmunity": bool(config.get("executionImmunity", True)),
                          "sourceSpawnsOnly": True},
            "healthFactor": float(config.get("healthFactor", DEFAULT_HEALTH_FACTOR)),
            "pct": pct}
