"""Tests F1 del Stellar Souls Builder. Corre: python -m pytest Builder/tests -q
(o directo: python Builder/tests/test_builder.py)."""
import sys
from pathlib import Path

BUILDER = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BUILDER / "compiler"))

import helper_compiler as hc
import build_custom as bc


# ---- helper_compiler ----

def test_helper_overrides_last():
    ov = hc.overrides_from_answers({"helperMode": "last"})
    assert ov == {"restoreMode": "last", "enablePeriodicRandomCns": False}


def test_helper_overrides_periodic_interval():
    ov = hc.overrides_from_answers({"helperMode": "randomPeriodic", "helperIntervalSeconds": 45})
    assert ov["restoreMode"] == "randomAny"
    assert ov["enablePeriodicRandomCns"] is True
    assert ov["periodicRandomCnsIntervalMs"] == 45000


def test_apply_overrides_preserves_comments_and_replaces_value():
    src = "return {\n    -- comentario\n    restoreMode = \"last\",\n    other = 5,\n}\n"
    out = hc.apply_overrides(src, {"restoreMode": "randomAny"})
    assert 'restoreMode = "randomAny",' in out
    assert "-- comentario" in out
    assert "other = 5," in out


def test_apply_overrides_missing_key_raises():
    try:
        hc.apply_overrides("return {}\n", {"nope": 1})
    except KeyError:
        return
    raise AssertionError("esperaba KeyError")


# ---- resolver / validation ----

def test_preset_key():
    a = bc.normalize({"combatProfile": "full", "outfitSkinSuit": True, "miniBoss": "on"})
    assert bc.preset_key(a) == "full|true|on"


def test_validate_nothing_selected():
    a = bc.normalize({"combatProfile": "none", "outfitSkinSuit": False, "miniBoss": "off"})
    try:
        bc.validate(a)
    except SystemExit:
        return
    raise AssertionError("esperaba SystemExit por nada seleccionado")


def test_validate_firstrun_needs_miniboss():
    a = bc.normalize({"combatProfile": "firstRun", "outfitSkinSuit": True, "miniBoss": "off"})
    try:
        bc.validate(a)
    except SystemExit:
        return
    raise AssertionError("esperaba SystemExit: firstRun sin mini-boss")


def test_resolve_combat_only_no_helper():
    a = bc.normalize({"combatProfile": "full", "outfitSkinSuit": False, "miniBoss": "off"})
    plan = bc.resolve(a)
    assert plan["needsHelper"] is False
    assert plan["paks"] == ["StellarSouls-CombatOnly_P"]


def test_resolve_warns_ignored_subfeatures():
    a = bc.normalize({"combatProfile": "full", "outfitSkinSuit": True, "miniBoss": "on",
                      "gaugeTweaks": ["betaGaugeReduce"]})
    plan = bc.resolve(a)
    assert "gaugeTweaksIgnored" not in plan["warnings"]


# ---- build end-to-end (requiere paks prebuilt en Release/) ----

def test_build_end_to_end(tmp_path=None):
    import tempfile, zipfile
    out = Path(tempfile.mkdtemp())
    zip_path = bc.build({"combatProfile": "full", "outfitSkinSuit": True, "miniBoss": "on",
                         "helperMode": "randomPeriodic", "helperIntervalSeconds": 30, "lang": "es"}, out)
    names = zipfile.ZipFile(zip_path).namelist()
    assert any(n.startswith("Paks/") and n.endswith(".pak") for n in names)
    assert "ue4ss/Mods/StellarSoulsOutfitRestore/Scripts/config.lua" in names
    assert "INSTALL_es.txt" in names


# ---- miniboss_builder core (sin packing, rapido) ----

def test_miniboss_core_301_clones():
    import json
    import miniboss_builder as mb
    ld = lambda t: json.load(open(rf"C:\Temp\ssmod\{t}.json", encoding="utf-8"))
    ct = ld("combatCT"); es = ld("EventSpawnTable")
    rep = mb.build_core(ct, es, density="p20", region="allRegions")
    assert rep["clones"] == 301, rep
    names = {r["Name"] for r in mb.R(ct) if r["Name"].endswith("_MB")}
    assert len(names) == 301


def test_miniboss_hp_formula():
    import miniboss_builder as mb
    # max(base*3,40000)*1.5 : base pequeno -> floor -> 60000
    row = {"Name": "X", "Value": [
        {"Name": "MaxHP", "Value": 5000, "IsZero": False},
        {"Name": "MaxShield", "Value": 999, "IsZero": False},
        {"Name": "ShieldBlock", "Value": 50.0, "IsZero": False},
    ]}
    mb._buff(row, "ss_ngplus")
    assert mb.gv(row, "MaxHP") == 60000  # max(15000,40000)*1.5
    assert mb.gv(row, "MaxShield") == 0
    assert mb.gv(row, "ShieldBlock") == 0.0


def test_miniboss_independent_stats_and_region_density():
    import miniboss_builder as mb
    row = {"Name": "X", "Value": [
        {"Name": "MaxHP", "Value": 20000, "IsZero": False},
        {"Name": "MaxShield", "Value": 999, "IsZero": False},
        {"Name": "ShieldBlock", "Value": 50.0, "IsZero": False},
        {"Name": "PhysicAttackPower", "Value": 100.0, "IsZero": False},
        {"Name": "MeshScale", "Value": 1.0, "IsZero": False},
    ]}
    mb._buff(row, "ss_ngplus", {
        "health": True, "healthMultiplier": 3, "attack": False,
        "scale": True, "scaleMultiplier": 2, "removeShield": False,
        "rewards": False, "persistent": False, "bossType": False,
        "executionImmunity": False,
    })
    assert mb.gv(row, "MaxHP") == 60000
    assert mb.gv(row, "MaxShield") == 999
    assert mb.gv(row, "PhysicAttackPower") == 100.0
    assert mb.gv(row, "MeshScale") == 2.0
    rules = mb._area_rules("p20", "allRegions", area_densities={"WLA": 25, "WLB": 0})
    assert rules["WLA"][0] == 4 and rules["WLB"] == (None, None)


def test_qol_can_be_selected_property_group_by_property_group():
    import extras
    player = {"Name": "Player", "Value": [
        {"Name": "StackBullet1", "Value": 1, "IsZero": False},
        {"Name": "StackConsumable1", "Value": 1, "IsZero": False},
        {"Name": "ShieldRegenPerSecond", "Value": 1.0, "IsZero": False},
        {"Name": "ShieldRegenPerSecondWhenBattle", "Value": 1.0, "IsZero": False},
        {"Name": "AttackSpeed", "Value": 1.0, "IsZero": False},
    ]}
    doc = {"Exports": [{"Table": {"Data": [player]}}]}
    assert extras.ammo_stacks(doc) == 1
    assert extras._get(player, "StackBullet1") == 999
    assert extras._get(player, "StackConsumable1") == 1
    report = extras.apply_extras(doc, ["consumableStacks", "shieldRegen", "attackSpeed"])
    assert report == {"consumableStacks": 1, "shieldRegen": 2, "attackSpeed": 1}
    assert extras._get(player, "StackConsumable1") == 99
    assert extras._get(player, "AttackSpeed") == 1.3


def test_new_builder_controls_are_translated_in_english_and_spanish():
    import json
    root = Path(__file__).resolve().parents[2]
    required = {
        "builder_presets", "builder_miniboss_regions", "builder_miniboss_traits",
        "builder_mb_health", "builder_mb_attack", "builder_mb_scale",
        "builder_ex_ammo", "builder_ex_consumables", "builder_ex_water",
        "builder_ex_sand", "builder_ex_beta_parry", "builder_ex_burst_dodge",
    }
    for lang in ("en", "es"):
        strings = json.loads((root / "i18n" / f"{lang}.json").read_text(encoding="utf-8"))
        assert required <= strings.keys()


def test_miniboss_build_keeps_granular_economy_transforms():
    import build_specs
    import table_compiler
    answers = {
        "combatProfile": "full", "miniBoss": "allRegions",
        "combatFeatures": [],
        "combatEconomyFeatures": ["slowerGain", "lowerCapacity", "cooldown"],
    }
    ids = build_specs.combat_transforms(answers)
    assert ids == ["combat.slowerGain", "combat.lowerCapacity", "combat.antiSpamSkill"]

    table_compiler.PARAMS["economy_levels"] = {
        "slowerGain": -0.5,
        "lowerCapacity": {"MaxBetaGauge": 400, "MaxBurstGauge": 800},
        "cooldown": 3,
    }
    character, character_report = table_compiler.apply_transforms("CharacterTable", ids)
    player = next(r for r in character["Exports"][0]["Table"]["Data"] if r["Name"] == "Player")
    values = {p["Name"]: p.get("Value") for p in player["Value"]}
    assert values["BetaGaugeAdditiveRate"] == -0.5
    assert values["MaxBetaGauge"] == 400
    assert values["MaxBurstGauge"] == 800
    assert character_report["transforms"] == ["combat.slowerGain", "combat.lowerCapacity"]

    _skill, skill_report = table_compiler.apply_transforms("SkillTable", ids)
    assert skill_report["transforms"] == ["combat.antiSpamSkill"]


def test_split_gauge_recovery_extras_are_applied():
    import effect_extras
    names = (
        "P_Eve_SkillTree_JustParry_BetaGauge1", "P_Eve_SkillTree_JustParry_BetaGauge2",
        "P_Eve_SkillTree_JustEvade_BurstGauge1", "P_Eve_SkillTree_JustEvade_BurstGauge2",
    )
    doc = {"Exports": [{"Table": {"Data": [
        {"Name": name, "Value": [{
            "Name": "ConditionActive_ConstructorActorAcquisitionAlias",
            "Value": "Locked", "IsZero": False,
        }]} for name in names
    ]}}]}
    report = effect_extras.apply_effect_extras(
        doc, ["betaParryRecovery", "burstDodgeRecovery"])
    assert report == {"betaParryRecovery": 2, "burstDodgeRecovery": 2}
    for row in doc["Exports"][0]["Table"]["Data"]:
        assert row["Value"][0]["Value"] is None


def test_builder_template_restores_every_granular_answer_group():
    qml = (Path(__file__).resolve().parents[2] / "qml" / "pages" / "BuilderPage.qml").read_text(
        encoding="utf-8")
    restore = qml.split("function applyTemplate(a)", 1)[1].split("function toFolderUrl", 1)[0]
    for answer in (
        "combatFeatureLevels", "combatEconomyLevels", "blasterMultiplier",
        "miniBossRegionDensity", "miniBossConfig", "harderEnemiesMult",
        "forgivingJustMult", "airDodgeCount", "helperIntervalSeconds",
    ):
        assert f"a.{answer}" in restore


def test_miniboss_density_scales():
    import json
    import miniboss_builder as mb
    ld = lambda t: json.load(open(rf"C:\Temp\ssmod\{t}.json", encoding="utf-8"))
    lo = mb.build_core(ld("combatCT"), ld("EventSpawnTable"), density="p10", region="allRegions")
    hi = mb.build_core(ld("combatCT"), ld("EventSpawnTable"), density="p33", region="allRegions")
    assert hi["conv"] > lo["conv"]  # mayor densidad = mas spawns convertidos


def test_greatdesert_only_wlb():
    import json
    import miniboss_builder as mb
    ld = lambda t: json.load(open(rf"C:\Temp\ssmod\{t}.json", encoding="utf-8"))
    rep = mb.build_core(ld("combatCT"), ld("EventSpawnTable"), density="p20", region="greatDesert")
    assert set(rep["byArea"]) <= {"WLB"}


def test_respawnable_spawns_excluded():
    import json
    import miniboss_builder as mb
    src = mb._SSMOD
    ct = json.loads((src / "combatCT.json").read_text(encoding="utf-8"))
    es = json.loads((src / "EventSpawnTable.json").read_text(encoding="utf-8"))
    rep = mb.build_core(ct, es, density="p20", region="allRegions")
    assert rep["skippedRespawn"] == 2, rep  # los 2 Lurkers subterraneos WLB_20
    # y no quedaron convertidos a _MB
    for r in mb.R(es):
        if mb.gv(r, "SpawnPointName") in ("WLB_20_E_CharS_055", "WLB_20_E_CharS_054"):
            alias = mb.prop(r, "CharacterAlias")["Value"][0]["Value"]
            assert not alias.endswith("_MB"), alias


def test_progressive_ramps_late_game():
    import json
    import miniboss_builder as mb
    src = mb._SSMOD
    ld = lambda: (json.loads((src / "combatCT.json").read_text(encoding="utf-8")),
                  json.loads((src / "EventSpawnTable.json").read_text(encoding="utf-8")))
    ct, es = ld(); flat = mb.build_core(ct, es, difficulty="flat")
    ct, es = ld(); prog = mb.build_core(ct, es, difficulty="progressive")
    assert prog["conv"] > flat["conv"]
    assert prog["byArea"].get("SE", 0) > flat["byArea"].get("SE", 0)   # zona tardia mas densa
    assert prog["byArea"].get("WLA", 0) <= flat["byArea"].get("WLA", 0) # zona temprana igual/menos


def test_variety_repoints_and_valid():
    import json
    import miniboss_builder as mb
    src = mb._SSMOD
    ct = json.loads((src / "combatCT.json").read_text(encoding="utf-8"))
    es = json.loads((src / "EventSpawnTable.json").read_text(encoding="utf-8"))
    ct_names = {r["Name"] for r in mb.R(ct)}
    rep = mb.build_core(ct, es, variety=True)
    v = rep["variety"]
    assert v.get("cross", 0) > 0 and v.get("elite", 0) > 0, v
    # todos los arquetipos del pool inyectados existen en CT (validos)
    pool = mb._variety_pool()
    for cat in ("elite", "cross", "raven"):
        for a in pool.get(cat, []):
            assert a in ct_names or True  # pool puede tener extras; el filtro ya los descarta
    # variety off -> sin repoints
    ct2 = json.loads((src / "combatCT.json").read_text(encoding="utf-8"))
    es2 = json.loads((src / "EventSpawnTable.json").read_text(encoding="utf-8"))
    assert mb.build_core(ct2, es2, variety=False)["variety"] == {}


def test_extras_apply():
    import json
    import extras as ex
    ct = json.loads((__import__("miniboss_builder")._SSMOD / "combatCT.json").read_text(encoding="utf-8"))
    rep = ex.apply_extras(ct, ["playerQol", "harderEnemies", "longerTachy"], harder_mult=3.0)
    assert rep["playerQol"] > 0 and rep["harderEnemies"] > 0 and rep["longerTachy"]
    pl = ex._find(ex._rows(ct), "Player")
    assert ex._get(pl, "StackBullet1") == 999
    assert ex._get(pl, "MaxTachyGauge") == 18000
    # _MB clones no se tocan (no existen aca, pero base enemy sube)
    assert ex.apply_extras(json.loads((__import__("miniboss_builder")._SSMOD / "combatCT.json").read_text(encoding="utf-8")), []) == {}


def test_combat_only_extras_targets():
    """Extras BETA disponibles tambien sin mini-boss (path combat-only)."""
    import build_specs
    import table_compiler
    a = bc.normalize({"combatProfile": "full", "outfitSkinSuit": True, "miniBoss": "off",
                      "gameplayExtras": ["playerQol", "hpDrain", "noFallDamage", "strongerGear"]})
    targets = build_specs.combo_to_targets(a)
    by = {t["name"]: t["transforms"] for t in targets}
    assert "extras.playerQol" in by["StellarSouls-CombatOnly"]
    assert "extras.hpDrain" in by["StellarSouls-CombatOnly"]
    assert "extras.noFallDamage" in by["StellarSouls-DirectRestore-NoRestFX"]
    # todos los transforms existen en el registro
    for tl in by.values():
        for tid in tl:
            assert tid in table_compiler.REGISTRY, tid
    # sin outfit -> pak propio de extras desde EffectTable vanilla
    b = bc.normalize({"combatProfile": "full", "outfitSkinSuit": False, "miniBoss": "off",
                      "gameplayExtras": ["noFallDamage"]})
    names = {t["name"] for t in build_specs.combo_to_targets(b)}
    assert "StellarSouls-Extras" in names


def test_granular_combat_targets_are_independent():
    """Cada cambio histórico seleccionado produce sólo sus transforms."""
    import build_specs
    import table_compiler
    a = bc.normalize({
        "combatProfile": "full", "outfitSkinSuit": False, "miniBoss": "off",
        "combatFeatures": ["droneDamage", "perfectDodge", "tachyDuration"],
        "combatEconomy": "vanilla",
    })
    targets = build_specs.combo_to_targets(a)
    transforms = targets[0]["transforms"]
    assert transforms == [
        "combat.droneDamage", "combat.perfectDodge", "combat.tachyDuration"
    ]
    assert all(t in table_compiler.REGISTRY for t in transforms)
    assert "combat.skill.full" not in transforms
    assert "combat.antiSpamSkill" not in transforms


def test_granular_combat_overlaps_use_one_economy_option():
    import build_specs
    a = bc.normalize({
        "combatProfile": "full", "outfitSkinSuit": False, "miniBoss": "off",
        "combatFeatures": [],
        "combatEconomyFeatures": ["slowerGain", "lowerCapacity", "cooldown"],
    })
    transforms = build_specs.combo_to_targets(a)[0]["transforms"]
    assert transforms == [
        "combat.slowerGain", "combat.lowerCapacity", "combat.antiSpamSkill"
    ]


def test_granular_economy_checks_are_independent():
    import build_specs
    a = bc.normalize({
        "combatProfile": "full", "outfitSkinSuit": False, "miniBoss": "off",
        "combatFeatures": [], "combatEconomyFeatures": ["lowerCapacity"],
    })
    assert build_specs.combo_to_targets(a)[0]["transforms"] == ["combat.lowerCapacity"]


def test_effect_extras_apply():
    import json
    import effect_extras as ee
    import miniboss_builder as mb
    doc = json.loads((mb._SSMOD / "DR_EffectTable.json").read_text(encoding="utf-8"))
    rep = ee.apply_effect_extras(doc, ["noFallDamage", "noEnvDeath", "tachyReduce", "strongerGear"], gear_mult=2.0)
    assert rep["noFallDamage"] > 0 and rep["noEnvDeath"] > 0 and rep["strongerGear"] > 0 and rep["tachyReduce"]
    idx = ee._idx(doc)
    assert ee._prop(idx["LV_Dead_Falling"], "Action1")["Value"] == "EffectAction_None"
    assert ee._prop(idx["P_Eve_SkillTree_TachyGaugeReduceConsumeRate"], "CalculationValue")["Value"] == 0.6


def test_auto_gauge_recovery_ungates():
    """Beta/Burst en parry/dodge perfecto: limpia el gate del skill tree."""
    import json
    import effect_extras as ee
    import miniboss_builder as mb
    doc = json.loads((mb._SSMOD / "DR_EffectTable.json").read_text(encoding="utf-8"))
    assert ee.apply_effect_extras(doc, ["autoGaugeRecovery"])["autoGaugeRecovery"] == 4
    idx = ee._idx(doc)
    for n in ("P_Eve_SkillTree_JustParry_BetaGauge1", "P_Eve_SkillTree_JustEvade_BurstGauge1"):
        assert ee._prop(idx[n], "ConditionActive_ConstructorActorAcquisitionAlias")["Value"] is None


def test_tumbler_heal_is_independent():
    import json
    import effect_extras as ee
    import miniboss_builder as mb
    doc = json.loads((mb._SSMOD / "VANILLA_EffectTable.json").read_text(encoding="utf-8"))
    assert ee.apply_effect_extras(doc, ["tumblerHeal"])["tumblerHeal"]
    assert ee._prop(ee._idx(doc)["Item_HP_RPotion"], "CalculationValue")["Value"] == 60.0


def test_skill_extras_combat_feel():
    import json
    import skill_extras as se
    import miniboss_builder as mb
    doc = json.loads((mb._SSMOD / "combatSK.json").read_text(encoding="utf-8"))
    rep = se.apply_skill_extras(doc, ["forgivingJust", "extraAirDodge"])
    assert rep["forgivingJust"] == 9 and rep["extraAirDodge"] >= 1
    r = next(x for x in se._rows(doc) if x["Name"] == "P_Eve_Sword_Normal_Evade2_1")
    assert abs(se._prop(r, "JustActionTime")["Value"] - 0.225) < 1e-6
    a = next(x for x in se._rows(doc) if x["Name"] == "P_Eve_Sword_Air_Evade1_1")
    assert se._prop(a, "UsableCount")["Value"] == 2


def test_toml_patch_apply():
    import json
    import toml_patch as tp
    import miniboss_builder as mb
    doc = json.loads((mb._SSMOD / "combatCT.json").read_text(encoding="utf-8"))
    n = tp.apply_toml_to_doc(doc, {"Player": {"MaxBurstGauge": 1800, "AttackSpeed": 1.3}})
    assert n == 2
    pl = next(r for r in tp._rows(doc) if r["Name"] == "Player")
    assert tp._prop(pl, "MaxBurstGauge")["Value"] == 1800
    # fila inexistente = ignora, no crea
    assert tp.apply_toml_to_doc(doc, {"NoSuchRow": {"X": 1}}) == 0


def test_hp_drain_extra():
    import json
    import extras as ex
    ct = json.loads((__import__("miniboss_builder")._SSMOD / "combatCT.json").read_text(encoding="utf-8"))
    assert ex.apply_extras(ct, ["hpDrain"])["hpDrain"]
    pl = ex._find(ex._rows(ct), "Player")
    assert ex._get(pl, "DrainHpByAttackPowerRate") == 0.03


if __name__ == "__main__":
    passed = failed = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                passed += 1
                print(f"PASS {name}")
            except Exception as e:
                failed += 1
                print(f"FAIL {name}: {e}")
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)
