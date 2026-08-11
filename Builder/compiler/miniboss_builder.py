"""miniboss_builder — genera CharacterTable + EventSpawnTable del mini-boss NG+.

Port parametrizado de build_allmaps.py. Produce los 2 JSON core (con densidad y
region configurables) a partir de las bases de combate (combatCT) y EventSpawn.
Las otras 8 tablas del pak mini-boss (EffectTable/SkillTable/SkillResult/
RewardGroup/TargetFilter/Item*/GearStat) son FIJAS (no dependen de densidad) y
se toman de sus sources; ver miniboss_targets().

Parametros:
  density: "p10"|"p20"|"p33"  -> denominador global (10 -> ~10% de spawns, etc.)
  region:  "allRegions" | "greatDesert"

Reglas de area (densidad base 1.31.1). density escala el denominador.
"""
from __future__ import annotations

import copy
import json
from collections import defaultdict
from pathlib import Path

MARK = "ss_NoStealth"
HP_FLOOR = 40000
# HitDefenseLevel gatea las reacciones "HitLevelResult*" (stun/knockdown/airborne)
# de SkillResultTable. Las skills del jugador llegan a HitLevel 3; el juego ya usa
# 5 en los monstruos SuperLarge que nunca se tambalean. 5 = inmune al stagger.
STAGGER_IMMUNE_LEVEL = 5

# Mapa arch -> reward group (elites por area) derivado de shipped 1.31.1.
_RG_PATH = Path(__file__).resolve().parent.parent / "base_tables" / "miniboss_reward_groups.json"
try:
    _REWARD_GROUPS = json.loads(_RG_PATH.read_text(encoding="utf-8"))
except FileNotFoundError:
    _REWARD_GROUPS = {}

# Denominador base por area (1.31.1). None = area sin mini-boss.
AREA_RULES_BASE = {
    "SD": (None, None), "DED": (None, None), "Xion": (None, None),
    "WLA": (20, "mk1_123"), "ATL": (20, "mk1_123"),
    "ME": (10, "mk1_123"), "WLB": (10, "mk1_123"),
    "AYL": (10, "mk2_123"), "DED40": (7, "mk2_23"),
    "DEDA": (7, "mk2_23"), "SE": (7, "mk2_23"),
}
AREA_XP_GROUP = {
    "WLA": "m_wasteland_elite1", "ATL": "m_altesLabor_elite1",
    "ME": "m_matrix11_elite1", "WLB": "m_Desert_elite1",
    "AYL": "m_abyssLabor_elite1", "DED40": "m_eidos9_elite1_Group",
    "DEDA": "m_eidos9_elite1_Group", "SE": "m_elevator_elite1",
}
# density -> factor sobre el denominador base (mayor denom = menos mini-bosses).
DENSITY_FACTOR = {"p33": 0.5, "p20": 1.0, "p10": 2.0}
GREAT_DESERT_AREAS = {"WLB"}


def R(d):
    return d["Exports"][0]["Table"]["Data"]


def find(rows, n):
    return next((r for r in rows if r.get("Name") == n), None)


def prop(r, n):
    return next((p for p in r["Value"] if p["Name"] == n), None)


def gv(r, n):
    p = prop(r, n)
    return p.get("Value") if p else None


def sv(r, n, v):
    p = prop(r, n)
    if p:
        p["Value"] = v


def addnames(d, names):
    for x in names:
        if x and x not in d["NameMap"]:
            d["NameMap"].append(x)


def nameprop(v):
    return {"$type": "UAssetAPI.PropertyTypes.Objects.NamePropertyData, UAssetAPI",
            "Name": "0", "ArrayIndex": 0, "PropertyGuid": None, "IsZero": False,
            "PropertyTagFlags": "None", "PropertyTypeName": None,
            "PropertyTagExtensions": "NoExtension", "Value": v}


def area_for_zone(zone):
    s = str(zone or "")
    return s.split("_")[1] if s.startswith("Zone_") else None


def _buff(r, reward_group, config=None):
    legacy = config is None
    config = config or {}
    def mul(n, m, i=False):
        p = prop(r, n)
        if p and isinstance(p.get("Value"), (int, float)):
            v = p["Value"] * m
            p["Value"] = int(round(v)) if i else v
    # 1.31.1 (build_ngplus_corrected + remove_miniboss_shield_hp15):
    # HP = max(round(base*3), 40000), luego *1.5. El floor va ANTES del *1.5.
    p = prop(r, "MaxHP")
    if config.get("health", True) and p and isinstance(p.get("Value"), (int, float)):
        base_hp = p["Value"]
        hp_mult = float(config.get("healthMultiplier", 4.5))
        p["Value"] = (int(max(round(base_hp * 3), HP_FLOOR) * 1.5) if legacy
                      else max(int(round(base_hp * hp_mult)), HP_FLOOR))
    # 1.31.1 (derivado empiricamente de datos shipped): shield y block a 0.
    if config.get("removeShield", True):
        sv(r, "MaxShield", 0)
        sv(r, "ShieldBlock", 0.0)
    if config.get("attack", True):
        attack_mult = float(config.get("attackMultiplier", 1.6))
        mul("PhysicAttackPower", attack_mult)
        mul("RangeAttackPower", attack_mult)
    p = prop(r, "MeshScale")
    if config.get("scale", True) and p and isinstance(p.get("Value"), (int, float)):
        p["Value"] = min(p["Value"] * float(config.get("scaleMultiplier", 1.6)), 3.0)
    if config.get("rewards", True):
        sv(r, "RewardGroupAlias", reward_group)
        sv(r, "RewardSpawnBucketType", "ESBItemBucketType_World")
        sv(r, "RewardFormationAssetPath", "None")
    if config.get("persistent", True):
        sv(r, "RewardOverrideSaveType", "ESBItemOverrideSaveType_Save")
    p = prop(r, "ActorType")
    if config.get("bossType", True) and p:
        p["Value"] = "ActorType_BossMonster"
    # Inmunidad al stagger: sin esto el mini-boss queda stuneado con spam de espada.
    # No aplica en el path legacy (paridad byte con el shipped 1.31.1).
    p = prop(r, "HitDefenseLevel")
    if not legacy and config.get("staggerImmunity", True) and p is not None:
        if not isinstance(p.get("Value"), (int, float)) or p["Value"] < STAGGER_IMMUNE_LEVEL:
            p["Value"] = STAGGER_IMMUNE_LEVEL
            p["IsZero"] = False
    p = prop(r, "SpawnEffectList")
    if config.get("executionImmunity", True) and p is not None:
        if not isinstance(p.get("Value"), list):
            p["Value"] = []
        if not any(isinstance(e, dict) and e.get("Value") == MARK for e in p["Value"]):
            p["Value"].append(nameprop(MARK))


# Orden de campana (temprano -> tardio) para la dificultad progresiva.
AREA_CAMPAIGN_RANK = {
    "WLA": 0, "ATL": 1, "ME": 2, "WLB": 3, "AYL": 4, "DED40": 5, "DEDA": 6, "SE": 7,
}
# Progresivo: factor por area (menor = mas denso). Temprano ~1.3 (menos mini-bosses),
# tardio ~0.5 (mas mini-bosses) -> la dificultad sube hacia el final.
def _progressive_factor(area):
    rank = AREA_CAMPAIGN_RANK.get(area)
    if rank is None:
        return 1.0
    span = max(AREA_CAMPAIGN_RANK.values())
    frac = rank / span if span else 0.0
    return 1.3 - 0.8 * frac  # 1.3 (early) -> 0.5 (late)


def _area_rules(density, region, difficulty="flat", area_densities=None):
    if area_densities is not None:
        rules = {}
        for area, (_denom, loot) in AREA_RULES_BASE.items():
            pct = int(area_densities.get(area, 0) or 0)
            rules[area] = ((max(1, int(round(100 / pct))), loot) if pct > 0 else (None, None))
        return rules
    factor = DENSITY_FACTOR.get(density, 1.0)
    rules = {}
    for area, (denom, loot) in AREA_RULES_BASE.items():
        if denom is None:
            rules[area] = (None, None)
            continue
        if region == "greatDesert" and area not in GREAT_DESERT_AREAS:
            rules[area] = (None, None)
            continue
        f = factor * (_progressive_factor(area) if difficulty == "progressive" else 1.0)
        rules[area] = (max(1, int(round(denom * f))), loot)
    return rules


def _is_respawnable(row):
    """Spawn que revive (SpawnRuleType RespawnAfterDead o RespawnInterval>0).
    Se excluye de la conversion a mini-boss: mini-boss + EXP elite + respawn =
    granja infinita (reporte de FengYeLy: Lurkers subterraneos en WLB_20)."""
    if gv(row, "SpawnRuleType") == "ESBSpawnRule_RespawnAfterDead":
        return True
    for f in ("RespawnIntervalTimeMin", "RespawnIntervalTimeMax"):
        v = gv(row, f)
        if isinstance(v, (int, float)) and v > 0:
            return True
    return False


# Campos que delatan un spawn guionado (arena con barrera, gate de mision,
# encuentro que dispara eventos al morir o al entrar en combate).
_SCRIPTED_FIELDS = (
    "EventOnDead", "EventOnBattle", "EventFirstTimeOnBattle", "EventOnSpawning",
    "ConditionsTrigger", "ConditionTriggerEvent",
)


def _is_scripted(row):
    """Spawn atado a script/mision: NO se convierte a mini-boss.

    ActorType_BossMonster solo funciona en enemigos que el juego registra como
    boss (barra de vida, secuencia de entrada). Aplicado a un enemigo de arena
    guionada queda sin barra, sin target y en estado no-combate: si la barrera
    solo cae al matarlo, el jugador queda encerrado (reporte de FengYeLy).
    Tambien se excluyen los spawns con MetaAI o con transito de zona por el
    enemigo, que son los mismos encuentros dirigidos.
    """
    for f in _SCRIPTED_FIELDS:
        if gv(row, f):
            return True
    if gv(row, "MetaAIAlias") not in (None, "None"):
        return True
    return bool(gv(row, "bEnableTransitZoneByEnemyActor"))


def _is_combat_alias(a):
    if not a or a.startswith("N_") or "Dummy" in a:
        return False
    if any(k in a for k in ["Shop", "Citizen", "Talker", "Gardener", "Bolt", "Scarlet", "Emil", "Merchant"]):
        return False
    return True


_VARIETY_POOL = None


def _variety_pool():
    global _VARIETY_POOL
    if _VARIETY_POOL is None:
        p = _BUILDER_ROOT / "features" / "variety_pool.json"
        _VARIETY_POOL = json.loads(p.read_text(encoding="utf-8")) if p.exists() else {}
    return _VARIETY_POOL


def _apply_variety(esd, ES, ct_names, spawns, converted_ids, named):
    """BETA - repunta un % de spawns NO convertidos a arquetipos curados
    (elite/cross-area/raven) sobre coords existentes. Ideas A/B/C. Determinista.
    Devuelve conteo por categoria y agrega los FName usados al NameMap."""
    pool = _variety_pool()
    if not pool:
        return {}
    elite = [a for a in pool.get("elite", []) if a in ct_names]
    cross = [a for a in pool.get("cross", []) if a in ct_names]
    raven = [a for a in pool.get("raven", []) if a in ct_names]
    late = set(pool.get("lateAreas", []))
    # arquetipos nativos por area (para cross = solo lo que NO esta ahi)
    native = defaultdict(set)
    for a, area, r, el in spawns:
        native[area].add(a)
    # elegibles por area: spawn no convertido, no respawneable, no named
    per_area = defaultdict(list)
    for a, area, r, el in spawns:
        if id(el) in converted_ids or a in named:
            continue
        per_area[area].append((a, r, el))

    used_names = set()
    rep = defaultdict(int)

    def inject(entries, denom, picker):
        n = 0
        for i, (a, r, el) in enumerate(entries):
            if id(el) in converted_ids:
                continue
            if denom <= 0 or i % denom != 0:
                continue
            pick = picker(a, r, i)
            if not pick:
                continue
            el["Value"] = pick
            converted_ids.add(id(el))
            used_names.add(pick)
            n += 1
        return n

    def pct_denom(p):
        return max(0, int(round(100 / p))) if p else 0

    for area, entries in per_area.items():
        entries = sorted(entries, key=lambda x: str(gv(x[1], "SpawnPointName")))
        # B - elite en zonas tardias
        if area in late and elite:
            rep["elite"] += inject(entries, pct_denom(pool.get("eliteLatePct", 0)),
                                    lambda a, r, i: elite[i % len(elite)])
        # A - cross-area (solo arquetipos no nativos del area)
        if cross:
            foreign = [c for c in cross if c not in native.get(area, set())]
            if foreign:
                rep["cross"] += inject(entries, pct_denom(pool.get("crossPct", 0)),
                                       lambda a, r, i: foreign[i % len(foreign)])
        # C - raven raro
        if raven:
            rep["raven"] += inject(entries, pct_denom(pool.get("ravenPct", 0)),
                                   lambda a, r, i: raven[i % len(raven)])
    addnames(esd, list(used_names))
    return dict(rep)


def build_core(combat_ct: dict, event_spawn: dict, density="p20", region="allRegions",
               difficulty="flat", variety=False, extras=None, harder_mult=2.0,
               area_densities=None, miniboss_config=None, overworld_config=None):
    """Muta combat_ct y event_spawn con clones `<arch>_MB` + subset de spawns.

    Esquema 1.31.1: UN clone por arquetipo de combate distinto que aparece en
    spawns no-boss (todas las areas), nombre `<arch>_MB`. Densidad/region/dificultad
    afectan que porcion de spawns se repunta a los clones. difficulty="progressive"
    hace las zonas tardias mas densas. Spawns respawneables se EXCLUYEN (anti-farm)
    y los guionados tambien (anti-softlock, ver _is_scripted).
    Devuelve reporte {clones, conv, byArea, skippedRespawn, skippedScripted}.
    """
    rules = _area_rules(density, region, difficulty, area_densities)
    ctd, esd = combat_ct, event_spawn
    CT, ES = R(ctd), R(esd)
    ctnames = {r["Name"] for r in CT}
    named = {r["Name"] for r in CT
             if gv(r, "DifficultyStatGroupAlias") == r["Name"] and not r["Name"].startswith("Player")}
    newnames = [MARK, "ActorType_BossMonster", "ESBItemBucketType_World", "ESBItemOverrideSaveType_Save"]

    # 1) Recolectar spawns candidatos y arquetipos distintos (todas las areas no-boss).
    spawns = []          # (arch, area, row, element)
    archetypes = []      # orden estable de aparicion
    seen = set()
    skipped_respawn = 0
    skipped_scripted = 0
    for r in ES:
        z = gv(r, "Zone")
        if not z or "Boss" in str(z):
            continue
        area = area_for_zone(z)
        p = prop(r, "CharacterAlias")
        els = p["Value"] if p and isinstance(p.get("Value"), list) else []
        if len(els) != 1:
            continue
        a = els[0].get("Value")
        if not _is_combat_alias(a) or a in named or a not in ctnames:
            continue
        if a not in seen:   # el arquetipo se clona igual (aparece en spawns normales)
            seen.add(a)
            archetypes.append(a)
        if _is_respawnable(r):   # NO convertir spawns que reviven (anti-farm)
            skipped_respawn += 1
            continue
        if _is_scripted(r):      # NO tocar encuentros guionados (anti-softlock)
            skipped_scripted += 1
            continue
        spawns.append((a, area, r, els[0]))

    # 2) Un clone `<arch>_MB` por arquetipo. Loot group por area del arquetipo
    #    (primera area donde aparece; fallback mk1_123).
    arch_area = {}
    for a, area, _, _ in spawns:
        arch_area.setdefault(a, area)
    clones = {}
    # Sin ninguna area con densidad (caso "solo bosses de overworld") no hace
    # falta clonar los 301 arquetipos: quedarian sin usar en la tabla.
    any_rule = any(v[0] for v in rules.values())
    for i, a in enumerate(archetypes if any_rule else []):
        nw = copy.deepcopy(find(CT, a))
        mbn = a + "_MB"
        nw["Name"] = mbn
        ip = prop(nw, "ID")
        if ip and isinstance(ip.get("Value"), int):
            ip["Value"] = ip["Value"] + 700000000 + i
        # 1.31.1: ss_ngplus por defecto; 74 arquetipos usan elite groups por area
        # (mapa exacto derivado de shipped en miniboss_reward_groups.json).
        group = _REWARD_GROUPS.get(a, "ss_ngplus")
        _buff(nw, group, miniboss_config)
        CT.append(nw)
        clones[a] = mbn
        newnames.extend([mbn, group])
    ll = find(CT, "WLB_M_LesserLurker_01_MB")
    if ll:
        p = prop(ll, "WeightType")
        if p:
            p["Value"] = "ActorWeightType_SuperLarge"
        addnames(ctd, ["ActorWeightType_SuperLarge"])
    addnames(ctd, newnames)

    # 3) Repuntar subset de spawns (densidad/region) a los clones.
    conv = 0
    by_area = defaultdict(int)
    converted_ids = set()
    per_area = defaultdict(list)
    for a, area, r, el in spawns:
        per_area[area].append((a, r, el))
    for area, lst in per_area.items():
        rule = rules.get(area)
        if not rule or rule[0] is None:
            continue
        denom = rule[0]
        for i, (a, r, el) in enumerate(sorted(lst, key=lambda x: str(gv(x[1], "SpawnPointName")))):
            if i % denom == 0:
                el["Value"] = clones[a]
                converted_ids.add(id(el))
                if (miniboss_config or {}).get("xpRewards", True) and area in AREA_XP_GROUP:
                    sv(r, "RewardGroup", AREA_XP_GROUP[area])
                conv += 1
                by_area[area] += 1
    addnames(esd, list(clones.values()) + list(AREA_XP_GROUP.values()))

    # 4) BETA - variedad de enemigos (repunta % de spawns no convertidos).
    variety_rep = {}
    if variety:
        variety_rep = _apply_variety(esd, ES, ctnames, spawns, converted_ids, named)

    # 4b) BETA - bosses de campo sueltos en el overworld (variantes `_OW`).
    overworld_rep = {}
    if overworld_config and overworld_config.get("enabled"):
        import overworld_bosses
        cfg = dict(overworld_config)
        cfg.setdefault("xpRewards", (miniboss_config or {}).get("xpRewards", True))
        overworld_rep = overworld_bosses.apply(ctd, esd, spawns, converted_ids, cfg)

    # 5) BETA - extras de gameplay (Player QoL / harder enemies / tachy).
    extras_rep = {}
    if extras:
        import extras as _extras
        extras_rep = _extras.apply_extras(ctd, extras, harder_mult)

    # Datos explícitos para el manifest: permiten distinguir un anti-farm
    # aplicado de un build que simplemente no encontró candidatos.
    return {"clones": len(clones), "conv": conv, "byArea": dict(sorted(by_area.items())),
            "skippedRespawn": skipped_respawn, "skippedScripted": skipped_scripted,
            "antiFarm": {"respawnExcluded": skipped_respawn,
                          "scriptedExcluded": skipped_scripted,
                          "persistentRewards": bool((miniboss_config or {}).get("persistent", True)),
                          "executionImmunity": bool((miniboss_config or {}).get("executionImmunity", True))},
            "variety": variety_rep, "overworldBosses": overworld_rep, "extras": extras_rep}


# Fuentes de las 8 tablas fijas del pak mini-boss (no dependen de densidad).
# CharacterTable + EventSpawnTable las genera build_core. Las otras 8 se toman
# del STAGING LEGACY ya compilado (v131 = 1.31.x), copiando los .uasset/.uexp
# directamente (no re-fromjson: los MERGED_*.json son intermedios no encodeables).
import os as _os

_BUILDER_ROOT = Path(__file__).resolve().parent.parent
# vendor/ tiene prioridad (distribucion portable); rutas relativas a Builder/.
_VENDOR_PATHS = _BUILDER_ROOT / "vendor" / "paths.json"
if _VENDOR_PATHS.exists():
    _PATHS = json.loads(_VENDOR_PATHS.read_text(encoding="utf-8"))
    _rel = lambda p: str((_BUILDER_ROOT / p).resolve())
    _PATHS["ssmodTables"]["path"] = _rel(_PATHS["ssmodTables"]["path"])
    _PATHS["tools"]["path"] = _rel(_PATHS["tools"]["path"])
    _PATHS["stagings"]["miniBoss"] = _rel(_PATHS["stagings"]["miniBoss"])
    _PATHS["stagings"]["firstRun"] = _rel(_PATHS["stagings"]["firstRun"])
else:
    _PATHS = json.loads((_BUILDER_ROOT / "base_tables" / "paths.json").read_text(encoding="utf-8"))


def _cfg_path(key_path, env, default):
    val = _os.environ.get(env)
    return Path(val) if val else Path(default)


_SSMOD = _cfg_path("ssmodTables", _PATHS["ssmodTables"]["env"], _PATHS["ssmodTables"]["path"])
_STAGING = Path(_os.environ.get("SSMOD_MINIBOSS_STAGING", _PATHS["stagings"]["miniBoss"]))
_STATIC_TABLES = [
    "EffectTable", "RewardGroupTable", "SkillTable", "SkillResultTable",
    "TargetFilterTable", "ItemTable", "ItemEquipableTable", "GearStatTable",
]


# Staging legacy de variantes fijas (10 tablas ya compiladas). Se repackean tal
# cual (no dependen de parametros). Cercanas al shipped publico (delta menor por
# tweaks post-staging; el staging exacto de la version publica no siempre existe).
_STAGINGS = {
    "firstRun": {
        "dir": Path(_os.environ.get("SSMOD_FIRSTRUN_STAGING", _PATHS["stagings"]["firstRun"])),
        "pak": "StellarSouls-FirstRun-CombatOutfitMiniBoss",
        # Nombre del pak publico de la variante sin outfit (mismo que en Nexus).
        "pakNoOutfit": "StellarSouls-FirstRun-MiniBossNoOutfit",
    },
}


def _script_path(name):
    """Ubica un script de transform: Builder/scripts (portable) o Development (dev)."""
    local = _BUILDER_ROOT / "scripts" / name
    if local.exists():
        return local
    return _BUILDER_ROOT.parent / "Development" / name


def _load_script_module(name):
    """Importa un script de Builder/scripts como modulo (sin lanzar un python)."""
    import importlib.util
    path = _script_path(name)
    spec = importlib.util.spec_from_file_location(Path(name).stem, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _edit_uasset(data_dir, table, mutators, repair=True):
    """Ediciones de una tabla staged en un solo roundtrip (ver toolchain)."""
    import toolchain
    return toolchain.edit_uasset(Path(data_dir) / f"{table}.uasset", mutators, repair)


def _apply_effect_extras_pass(data_dir, extras, gear_mult=2.0,
                              tumbler_value=60.0):
    """Extras de EffectTable en un roundtrip. Devuelve reporte o {}."""
    import effect_extras
    sel = [e for e in (extras or []) if e in effect_extras.EFFECT_EXTRAS]
    if not sel:
        return {}
    return _edit_uasset(data_dir, "EffectTable", [
        lambda doc: effect_extras.apply_effect_extras(doc, sel, gear_mult, tumbler_value)
    ])


def _apply_skill_extras_pass(data_dir, extras, just_mult=1.5, air_count=2):
    """Extras de SkillTable en un roundtrip (solo si aplica)."""
    import skill_extras
    sel = [e for e in (extras or []) if e in skill_extras.SKILL_EXTRAS]
    if not sel:
        return {}
    return _edit_uasset(data_dir, "SkillTable", [
        lambda doc: skill_extras.apply_skill_extras(doc, sel, just_mult, air_count)
    ])


def _apply_world_extras_pass(data_dir, extras, world_values=None):
    """tojson -> world_extras -> fromjson sobre las tablas de mundo que este pak
    ya incluye (hoy RewardGroupTable).

    El pak de mini-boss / First Run trae su propia copia de esas tablas, asi que
    el extra tiene que aplicarse adentro: empaquetar la misma tabla en dos paks
    dejaria que una de las dos gane en silencio.
    """
    import world_extras
    sel = [e for e in (extras or []) if e in world_extras.WORLD_EXTRAS]
    if not sel:
        return {}
    data_dir = Path(data_dir)
    report = {}
    for table in sorted(world_extras.tables_for(sel)):
        report.update(_edit_uasset(data_dir, table, [
            lambda doc, t=table: world_extras.apply_world_extras(doc, t, sel, world_values)
        ]))
    return report


def _apply_beta_revert(data_dir):
    """Aplica el revert de costo Beta/Burst a SkillTable.uasset de un staging.

    El shipped (1.31.1/1.5.0) = staging interno + este revert (Jul-19). Restaura
    UseEnergyAmount vanilla en filas Beta/Burst; deja intacto el nerf de dano.
    Tras esto, los 10 hashes de paquete coinciden con el pak publico.
    """
    import json
    revert = _load_script_module("revert_beta_burst_cost.py")
    van = json.loads((_SSMOD / "SkillTable_v.json").read_text(encoding="utf-8"))
    return _edit_uasset(data_dir, "SkillTable", [
        lambda doc: {"betaRevert": revert.apply_to_doc(doc, van)}
    ], repair=False)


def _apply_disable_skinsuit(data_dir):
    """Desactiva el swap Skin-Suit-on-break en EffectTable.uasset (variante sin
    outfit). Neutraliza nanosuit_break.Action1/ActionValue1 dejando el resto
    intacto. (El pak 'NoOutfit' publico NO tiene esto aplicado — bug shipped.)"""
    script = _load_script_module("disable_skinsuit_on_break.py")
    return _edit_uasset(data_dir, "EffectTable", [
        lambda doc: {"disableSkinSuit": script.apply_to_doc(doc)}
    ], repair=False)


def compile_from_staging(variant, work_dir, verify_result=True, beta_revert=True,
                         outfit=True, extras=None, gear_mult=2.0,
                         world_extra_ids=None, world_values=None):
    """Repackea un staging legacy fijo (ej First Run) a su pak Zen.

    outfit=False -> desactiva Skin-Suit-on-break en la EffectTable y usa el nombre
    de pak de la variante sin outfit.
    """
    import shutil
    import toolchain
    spec = _STAGINGS[variant]
    work_dir = Path(work_dir)
    pkg = work_dir / "package"
    if pkg.exists():
        shutil.rmtree(pkg)
    shutil.copytree(spec["dir"], pkg)
    data = pkg / "SB" / "Content" / "Local" / "Data"
    if beta_revert:
        _apply_beta_revert(data)
    if not outfit:
        _apply_disable_skinsuit(data)
    if extras:
        _apply_effect_extras_pass(data, extras, gear_mult)
        _apply_skill_extras_pass(data, extras)
    world_report = _apply_world_extras_pass(data, world_extra_ids, world_values)
    pak = spec["pak"] if outfit else spec.get("pakNoOutfit", spec["pak"] + "NoOutfit")
    spec = dict(spec, pak=pak)
    utoc = work_dir / f'{spec["pak"]}_P.utoc'
    toolchain.to_zen(pkg, utoc)
    ok = toolchain.verify(utoc) if verify_result else None
    return {"pak": utoc.with_suffix(".pak"), "ucas": utoc.with_suffix(".ucas"),
            "utoc": utoc, "verified": ok, "pakName": spec["pak"],
            "report": {"worldExtras": world_report} if world_report else {}}


def _repair_namemap(doc):
    """Registra en NameMap todo FName referenciado que falte, antes de fromjson.

    Implementacion unica en table_compiler: tener dos copias fue justo lo que
    dejo el path de mini-boss sin la reparacion.
    """
    import table_compiler
    return table_compiler.repair_namemap(doc)


def compile_miniboss(work_dir, density="p20", region="allRegions", verify_result=True,
                     outfit=True, difficulty="flat", faithful=False, variety=False,
                     extras=None, harder_mult=2.0, toml_dir=None, gear_mult=2.0,
                     tumbler_value=60.0, area_densities=None, miniboss_config=None,
                     just_mult=1.5, air_count=2, combat_transform_ids=None,
                     world_extra_ids=None, world_values=None, overworld_config=None):
    """Compila el pak mini-boss completo (10 tablas) a work_dir. Devuelve dict.

    Por defecto usa build_core -> incluye el fix anti-farm (excluye spawns
    respawneables) y difficulty (progressive = zonas tardias mas densas). Con
    faithful=True (solo p20/allRegions/flat) repackea el staging shipped tal cual
    (byte-parity exacta al pak publico 1.31.1, PARA VALIDACION; conserva el
    exploit del Lurker respawneable). outfit=False -> desactiva Skin-Suit-on-break.
    """
    import shutil
    import toolchain
    work_dir = Path(work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)
    pak_name = "StellarSouls-MiniBossNGPlus-Combat" if outfit else "StellarSouls-MiniBossNGPlus-CombatNoOutfit"

    # Camino fiel (opt-in): reproduce el pak publico exacto para validacion.
    if (faithful and not variety and not extras and not toml_dir
            and not (overworld_config or {}).get("enabled") and density == "p20"
            and region == "allRegions" and difficulty == "flat"):
        pkg = work_dir / "package"
        if pkg.exists():
            shutil.rmtree(pkg)
        data = pkg / "SB" / "Content" / "Local" / "Data"
        data.mkdir(parents=True)
        for f in _STAGING.glob("*"):
            shutil.copy2(f, data / f.name)
        _apply_beta_revert(data)
        if not outfit:
            _apply_disable_skinsuit(data)
        utoc = work_dir / f"{pak_name}_P.utoc"
        toolchain.to_zen(pkg, utoc)
        ok = toolchain.verify(utoc) if verify_result else None
        return {"pak": utoc.with_suffix(".pak"), "ucas": utoc.with_suffix(".ucas"),
                "utoc": utoc, "verified": ok, "pakName": pak_name,
                "report": {"mode": "faithful-staging+betaRevert", "outfit": outfit}}

    import table_compiler
    combat_transform_ids = list(combat_transform_ids or [])
    ctd, character_combat_report = table_compiler.apply_transforms(
        "CharacterTable", combat_transform_ids, base="vanilla")
    esd = json.loads((_SSMOD / "EventSpawnTable.json").read_text(encoding="utf-8"))
    report = build_core(ctd, esd, density=density, region=region, difficulty=difficulty,
                        variety=variety, extras=extras, harder_mult=harder_mult,
                        area_densities=area_densities, miniboss_config=miniboss_config,
                        overworld_config=overworld_config)
    report["combatTransforms"] = {"CharacterTable": character_combat_report}

    import shutil
    uassets = []
    # Tablas fijas: copiar los .uasset/.uexp legacy del staging. Va primero
    # porque la copia de SkillTable la pisa despues su propio fromjson.
    for name in _STATIC_TABLES:
        for ext in (".uasset", ".uexp"):
            src = _STAGING / f"{name}{ext}"
            if src.exists():
                shutil.copy2(src, work_dir / f"{name}{ext}")
    # SkillTable del staging contiene un preset fijo. Se reemplaza por vanilla +
    # exactamente los transforms elegidos, incluyendo cooldown y Blaster.
    skill_doc, skill_combat_report = table_compiler.apply_transforms(
        "SkillTable", combat_transform_ids, base="vanilla")
    # Los extras de SkillTable se aplican al doc en memoria: escribir el uasset
    # para volver a leerlo con tojson costaba un roundtrip entero de balde.
    import skill_extras
    sk_sel = [e for e in (extras or []) if e in skill_extras.SKILL_EXTRAS]
    skx = skill_extras.apply_skill_extras(skill_doc, sk_sel, just_mult, air_count) if sk_sel else {}
    report["combatTransforms"]["SkillTable"] = skill_combat_report

    # Una tabla por vez: UAssetGUI no tolera instancias concurrentes (ver el
    # lock en toolchain), asi que paralelizar esto no acelera nada.
    # Los transforms pueden introducir FNames nuevos: sin reparar el NameMap
    # fromjson no escribe nada (gotcha, ver toolchain).
    written = []
    for name, doc in (("CharacterTable", ctd), ("EventSpawnTable", esd),
                      ("SkillTable", skill_doc)):
        report.setdefault("nameMapAdded", {})[name] = _repair_namemap(doc)
        oj = work_dir / f"{name}.json"
        oj.write_text(json.dumps(doc), encoding="utf-8")
        written.append(toolchain.fromjson(oj, work_dir / f"{name}.uasset"))

    # EffectTable: desactivar el swap de outfit y los extras son ediciones del
    # mismo archivo. En un solo roundtrip: cada tojson/fromjson de esa tabla
    # cuesta ~25s, y hacer uno por pase era la mitad del tiempo de compilacion.
    import effect_extras
    et_mutators = []
    if not outfit:
        et_mutators.append(
            lambda doc: {"disableSkinSuit":
                         _load_script_module("disable_skinsuit_on_break.py").apply_to_doc(doc)})
    et_sel = [e for e in (extras or []) if e in effect_extras.EFFECT_EXTRAS]
    if et_sel:
        et_mutators.append(lambda doc: effect_extras.apply_effect_extras(
            doc, et_sel, gear_mult, tumbler_value))

    et_report = _edit_uasset(work_dir, "EffectTable", et_mutators)
    # Orden fijo: CharacterTable, EventSpawnTable y despues las fijas.
    uassets.extend(written[:2])
    uassets.extend(work_dir / f"{name}.uasset" for name in _STATIC_TABLES)
    eff = {k: v for k, v in et_report.items() if k != "disableSkinSuit"}
    if eff:
        report["effectExtras"] = eff
    if skx:
        report["skillExtras"] = skx
    # Mundo: solo las tablas que este pak ya trae (RewardGroupTable); el resto va
    # en el pak StellarSouls-World que compila build_custom.
    wx = _apply_world_extras_pass(work_dir, world_extra_ids, world_values)
    if wx:
        report["worldExtras"] = wx
    if toml_dir:  # patches TOML del usuario (opcional) sobre los uassets
        import toml_patch
        report["tomlPatches"] = toml_patch.apply_toml_dir(toml_dir, work_dir)

    res = toolchain.stage_and_pack(uassets, pak_name, work_dir, verify_result)
    res["report"] = report
    res["pakName"] = pak_name
    return res


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Compila el pak mini-boss NG+.")
    ap.add_argument("--out", required=True)
    ap.add_argument("--density", default="p20")
    ap.add_argument("--region", default="allRegions")
    args = ap.parse_args()
    r = compile_miniboss(Path(args.out), args.density, args.region)
    print("verified:", r["verified"], "| report:", r["report"])
    print("pak:", r["pak"])
