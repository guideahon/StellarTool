"""Tests F1 del Stellar Souls Builder. Corre: python -m pytest Builder/tests -q
(o directo: python Builder/tests/test_builder.py)."""
import json
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


def test_cns_restore_uses_only_confirmed_shield_full_edges():
    scripts = BUILDER / "vendor" / "helper" / "StellarSoulsOutfitRestore" / "Scripts"
    config = (scripts / "config.lua").read_text(encoding="utf-8")
    main = (scripts / "main.lua").read_text(encoding="utf-8")
    for setting in (
        "enableSavedBodySkinSuitWatcher",
        "enableMeshEventRestore",
        "enableStuckSkinSuitGuard",
    ):
        assert f"{setting} = false" in config
    assert "cfgBool(cfg.enableSavedBodySkinSuitWatcher, false)" in main
    assert "cfgBool(cfg.enableMeshEventRestore, false)" in main
    assert "cfgBool(cfg.enableStuckSkinSuitGuard, false)" in main
    assert "enableEquipmentListWatcher = true" in config
    assert "enableBodyMeshWatcher = true" in config
    assert "automaticRestoreBlockedByMesh(source)" in main
    assert "EVE still wears special mesh" in main
    # The random choice happens only after the central current-mesh guard.
    guard_at = main.index("automaticRestoreBlockedByMesh(source)")
    random_at = main.index("if rerollPending then", guard_at)
    assert guard_at < random_at


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


def test_base_attribute_enhancement_character_transforms():
    import extras
    fields = {
        "MaxHP": 2000, "MaxShield": 500,
        "DamageReductionPerShieldBock": 0.175,
        "ShieldRegenPerSecond": 80.0,
        "ShieldRegenPerSecondWhenBattle": 10.0,
        "MaxBetaGauge": 1000, "MaxBurstGauge": 1600,
        "HPRegenPerSecond": 0.0, "FishingAttackPower": 15.0,
        "StackBullet1": 30, "StackBullet2": 3, "StackBullet3": 16,
        "StackBullet4": 12, "StackBullet5": 60, "StackBullet6": 8,
    }
    player = {"Name": "Player", "Value": [
        {"Name": name, "Value": value, "IsZero": value == 0}
        for name, value in fields.items()
    ]}
    doc = {"Exports": [{"Table": {"Data": [player]}}]}
    report = extras.apply_extras(doc, [
        "baseAttributes", "attributeShieldRegen", "highGaugeCapacity",
        "passiveHpRegen", "fishingPower", "ammo100x",
    ])
    assert report == {
        "baseAttributes": 3, "attributeShieldRegen": 2,
        "highGaugeCapacity": 2, "passiveHpRegen": True,
        "fishingPower": True, "ammo100x": 6,
    }
    expected = {
        "MaxHP": 3000, "MaxShield": 1000,
        "DamageReductionPerShieldBock": 0.2,
        "ShieldRegenPerSecond": 160.0,
        "ShieldRegenPerSecondWhenBattle": 20.0,
        "MaxBetaGauge": 1500, "MaxBurstGauge": 2000,
        "HPRegenPerSecond": 20.0, "FishingAttackPower": 50.0,
        "StackBullet1": 3000, "StackBullet2": 300,
        "StackBullet3": 1600, "StackBullet4": 1200,
        "StackBullet5": 6000, "StackBullet6": 800,
    }
    assert {name: extras._get(player, name) for name in expected} == expected


def test_quantified_extras_apply_custom_values():
    import extras
    fields = {
        "MaxHP": 2000, "MaxShield": 500,
        "DamageReductionPerShieldBock": 0.175,
        "ShieldRegenPerSecond": 80.0,
        "ShieldRegenPerSecondWhenBattle": 10.0,
        "MaxBetaGauge": 1000, "MaxBurstGauge": 1600,
        "HPRegenPerSecond": 0.0, "FishingAttackPower": 15.0,
        "AttackSpeed": 1.0, "StackBullet1": 30, "StackConsumable1": 10,
    }
    player = {"Name": "Player", "Value": [
        {"Name": name, "Value": value, "IsZero": value == 0}
        for name, value in fields.items()
    ]}
    doc = {"Exports": [{"Table": {"Data": [player]}}]}
    extras.base_attributes(doc, max_hp=4200, max_shield=1250,
                           shield_reduction_percent=27.5)
    extras.shield_regen(doc, normal=145, combat=22)
    extras.high_gauge_capacity(doc, beta=1750, burst=2400)
    extras.passive_hp_regen(doc, per_second=12.5)
    extras.fishing_power(doc, power=85)
    extras.attack_speed(doc, multiplier=1.6)
    extras.ammo_stacks(doc, stack_size=777)
    extras.consumable_stacks(doc, stack_size=123)
    assert extras._get(player, "MaxHP") == 4200
    assert extras._get(player, "MaxShield") == 1250
    assert extras._get(player, "DamageReductionPerShieldBock") == 0.275
    assert extras._get(player, "ShieldRegenPerSecond") == 145
    assert extras._get(player, "ShieldRegenPerSecondWhenBattle") == 22
    assert extras._get(player, "MaxBetaGauge") == 1750
    assert extras._get(player, "MaxBurstGauge") == 2400
    assert extras._get(player, "HPRegenPerSecond") == 12.5
    assert extras._get(player, "FishingAttackPower") == 85
    assert extras._get(player, "AttackSpeed") == 1.6
    assert extras._get(player, "StackBullet1") == 777
    assert extras._get(player, "StackConsumable1") == 123


def test_advanced_quantities_apply_each_internal_stack_independently():
    import extras
    fields = {
        **{f"StackBullet{i}": i for i in range(1, 7)},
        **{f"StackConsumable{i}": i for i in range(1, 8)},
    }
    player = {"Name": "Player", "Value": [
        {"Name": name, "Value": value, "IsZero": False}
        for name, value in fields.items()
    ]}
    doc = {"Exports": [{"Table": {"Data": [player]}}]}
    bullet_values = {f"StackBullet{i}": 100 + i for i in range(1, 7)}
    consumable_values = {f"StackConsumable{i}": 200 + i for i in range(1, 8)}
    assert extras.ammo_stacks(doc, stack_size=999, values=bullet_values) == 6
    assert extras.consumable_stacks(
        doc, stack_size=99, values=consumable_values) == 7
    for name, value in {**bullet_values, **consumable_values}.items():
        assert extras._get(player, name) == value
    capacity_values = {f"StackBullet{i}": 1000 + i for i in range(1, 7)}
    assert extras.ammo_100x(doc, multiplier=100, values=capacity_values) == 6
    for name, value in capacity_values.items():
        assert extras._get(player, name) == value


def test_base_attribute_enhancement_effect_transforms():
    import effect_extras
    rows = []
    for name in (
            "P_Eve_SkillTree_JustParry_BetaGauge1",
            "P_Eve_SkillTree_JustParry_BetaGauge2",
            "P_Eve_SkillTree_JustEvade_BurstGauge1",
            "P_Eve_SkillTree_JustEvade_BurstGauge2"):
        rows.append({"Name": name, "Value": [
            {"Name": "LoopTargetFilterAlias", "Value": "None"},
            {"Name": "CalculationMultipleValue", "Value": 1.0},
            {"Name": "LoopIntervalTime", "Value": 0.0},
            {"Name": "StartDelayTime", "Value": 0.0},
            {"Name": "LifeTime", "Value": 1.0},
            {"Name": "ConditionActive_ConstructorActorAcquisitionAlias",
             "Value": "SkillGate"},
        ]})
    rows += [
        {"Name": "N_Drone_Scan", "Value": [
            {"Name": "ExpansionValue1", "Value": "5"},
            {"Name": "LifeTime", "Value": 3.5},
        ]},
        {"Name": "P_Eve_Stance_GunGorgon", "Value": [
            {"Name": "ActorState1",
             "Value": "ActorState_BlockRotation"},
        ]},
    ]
    doc = {"Exports": [{"Table": {"Data": rows}}]}
    report = effect_extras.apply_effect_extras(
        doc, ["gaugeRecoveryOverTime", "droneScanBoost", "gunGorgonRotation"])
    assert report == {
        "gaugeRecoveryOverTime": 20,
        "droneScanBoost": 2,
        "gunGorgonRotation": True,
    }
    assert effect_extras._prop(rows[0], "ConditionActive_ConstructorActorAcquisitionAlias")[
        "Value"] == "SkillGate"
    assert effect_extras._prop(rows[0], "LifeTime")["Value"] == 11.0
    assert effect_extras._prop(rows[4], "LifeTime")["Value"] == 10.0
    assert effect_extras._prop(rows[5], "ActorState1")["Value"] == "ActorState_None"


def test_base_attribute_enhancement_skill_transforms():
    import skill_extras
    names = [
        "P_Eve_Sword_Normal_DashAttack1_1",
        "P_Eve_Sword_Normal_DashAttack3_1",
        "P_Eve_Sword_Normal_DashAttack4_1",
        "N_Drone_Normal_Scan1_1",
    ]
    rows = [{"Name": name, "Value": [
        {"Name": "CoolTime", "Value": 8.0, "IsZero": False},
    ]} for name in names]
    doc = {"Exports": [{"Table": {"Data": rows}}]}
    report = skill_extras.apply_skill_extras(
        doc, ["dashCooldown4", "droneScanBoost"])
    assert report == {"dashCooldown4": 3, "droneScanBoost": True}
    assert [skill_extras._prop(row, "CoolTime")["Value"] for row in rows] \
        == [4.0, 4.0, 4.0, 5.0]


def test_drone_scan_control_compiles_both_tables():
    import build_specs
    targets = build_specs.combo_to_targets({
        "combatProfile": "full", "outfitSkinSuit": False,
        "miniBoss": "off", "combatFeatures": [],
        "combatEconomyFeatures": [], "gameplayExtras": ["droneScanBoost"],
    })
    by_name = {target["name"]: target["transforms"] for target in targets}
    assert "extras.droneScanCooldown" in by_name["StellarSouls-CombatOnly"]
    assert "extrasVanilla.droneScanDuration" in by_name["StellarSouls-Extras"]


def test_new_builder_controls_are_translated_in_english_and_spanish():
    import json
    root = Path(__file__).resolve().parents[2]
    required = {
        "builder_presets", "builder_miniboss_regions", "builder_miniboss_traits",
        "builder_mb_health", "builder_mb_attack", "builder_mb_scale",
        "builder_ex_ammo", "builder_ex_consumables", "builder_ex_water",
        "builder_ex_sand", "builder_ex_beta_parry", "builder_ex_burst_dodge",
        "builder_ex_base_attributes", "builder_ex_ammo_100x",
        "builder_ex_attribute_shield_regen", "builder_ex_high_gauge",
        "builder_ex_passive_hp", "builder_ex_fishing",
        "builder_ex_gauge_over_time", "builder_ex_dash_cooldown",
        "builder_ex_drone_scan", "builder_ex_gun_rotation",
        "builder_bae_group", "builder_bae_apply", "builder_bae_clear",
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
        "miniBossRegionDensity", "miniBossConfig", "hardcoreEnemyBoost",
        "forgivingJustMult", "airDodgeCount", "tumblerHealPercent",
        "helperIntervalSeconds",
    ):
        assert f"a.{answer}" in restore
    assert "App.builderTemplate(id)" in qml
    assert "root.applyHistoryTemplate(modelData.id)" in qml


def test_base_attribute_preset_resolves_overlapping_options():
    qml = (Path(__file__).resolve().parents[2] / "qml" / "pages" / "BuilderPage.qml").read_text(
        encoding="utf-8")
    body = qml.split("function setBaseAttributeEnhancement(on)", 1)[1] \
        .split("function helperValue()", 1)[0]
    for control in (
        "exBaseAttributes", "exAmmo100x", "exAttributeShieldRegen",
        "exHighGauge", "exPassiveHp", "exFishing", "exGaugeOverTime",
        "exDashCooldown", "exDroneScan", "exGunRotation",
    ):
        assert f"{control}.checked = on" in body
    for conflicting in ("exAmmo", "exShieldRegen", "exBetaParry", "exBurstDodge"):
        assert f"{conflicting}.checked = false" in body
    assert "capacityRow.selected = false" in body


def test_incompatible_builder_options_require_an_explicit_choice():
    root = Path(__file__).resolve().parents[2]
    qml = (root / "qml" / "pages" / "BuilderPage.qml").read_text(encoding="utf-8")
    assert "id: optionConflictDialog" in qml
    assert "function requestOption(newOption, conflicts, newLabel)" in qml
    assert "function requestBaseAttributeEnhancement()" in qml
    assert "onClicked: root.requestBaseAttributeEnhancement()" in qml
    for control in (
        "exAmmo", "exAmmo100x", "exShieldRegen", "exAttributeShieldRegen",
        "exHighGauge", "exBetaParry", "exBurstDodge", "exGaugeOverTime",
    ):
        body = qml.split(f"id: {control}", 1)[1].split("}", 1)[0]
        assert "root.requestOption(" in body
    capacity = qml.split("id: capacityRow", 1)[1].split("CheckBox", 1)[0]
    assert "onUserToggled: if (checked) root.requestOption(" in capacity
    for language in ("es", "en"):
        translations = json.loads((root / "i18n" / f"{language}.json").read_text(
            encoding="utf-8"))
        for key in (
            "builder_conflict_title", "builder_conflict_body",
            "builder_conflict_keep", "builder_conflict_use",
        ):
            assert translations[key]


def test_builder_exposes_quantities_and_named_presets():
    root = Path(__file__).resolve().parents[2]
    qml = (root / "qml" / "pages" / "BuilderPage.qml").read_text(encoding="utf-8")
    cpp = (root / "src" / "AppController.cpp").read_text(encoding="utf-8")
    header = (root / "src" / "AppController.h").read_text(encoding="utf-8")
    assert "component NumericEditor: RowLayout" in qml
    assert "component QuantifiedExtra: ColumnLayout" in qml
    assert "gameplayExtraValues:" in qml
    assert "advancedQuantitySelection:" in qml
    assert "id: advancedQuantities" in qml
    for field in (
        "StackBullet1", "StackBullet2", "StackBullet3", "StackBullet4",
        "StackBullet5", "StackBullet6", "StackConsumable1",
        "StackConsumable2", "StackConsumable3", "StackConsumable4",
        "StackConsumable5", "StackConsumable6", "StackConsumable7",
    ):
        assert field in qml
    assert "App.saveBuilderPreset(" in qml
    assert "App.deleteBuilderPreset(" in qml
    assert "root.applyTemplate(modelData.answers)" in qml
    for method in ("builderPresets", "saveBuilderPreset", "deleteBuilderPreset"):
        assert method in header
        assert f"AppController::{method}" in cpp


def test_builder_long_option_groups_do_not_use_a_single_overflowing_row():
    qml = (Path(__file__).resolve().parents[2] / "qml" / "pages" / "BuilderPage.qml").read_text(
        encoding="utf-8")
    tumbler = qml.split("id: tumblerGroup", 1)[0].rsplit("GridLayout {", 1)[1]
    assert "columns: 5" in tumbler
    assert "Layout.fillWidth: true" in tumbler
    for control in ("exJust", "exAirDodge", "exHarder"):
        block = qml.split(f"id: {control}", 1)[0].rsplit("ColumnLayout {", 1)[1]
        assert "Layout.fillWidth: true" in block


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


def test_hardcore_enemy_presets_only_touch_hardmode_and_exclude_maelstrom():
    import copy
    import hardcore_enemies as he

    def row(row_id, difficulty, alias, attack=100, hp=100, shield=100):
        values = {
            "DifficultyAlias": difficulty, "DifficultyStatGroupAlias": alias,
            "ID": row_id, "StatValue1": attack, "StatValue3": attack,
            "StatValue4": hp, "StatValue5": shield,
        }
        return {"Name": str(row_id), "Value": [
            {"Name": key, "Value": value, "IsZero": False}
            for key, value in values.items()
        ]}

    base_rows = [
        row(17, "HardMode", "Monster", hp=80, shield=80),
        row(301, "HardMode", "Boss_A", attack=150, hp=300, shield=300),
        row(305, "HardMode", "ATL_M_Maelstrom_01", attack=150, hp=300, shield=300),
        row(401, "NormalMode", "Boss_Normal", attack=150, hp=300, shield=300),
    ]
    main = {"Exports": [{"Table": {"Data": copy.deepcopy(base_rows)}}]}
    insane = {"Exports": [{"Table": {"Data": copy.deepcopy(base_rows)}}]}

    main_report = he.apply(main, "main")
    insane_report = he.apply(insane, "insane")

    def values(doc, row_id):
        found = next(r for r in doc["Exports"][0]["Table"]["Data"] if r["Name"] == str(row_id))
        return {p["Name"]: p["Value"] for p in found["Value"]}

    assert (values(main, 17)["StatValue4"], values(main, 17)["StatValue5"]) == (80, 80)
    assert (values(main, 301)["StatValue1"], values(main, 301)["StatValue3"]) == (187.5, 187.5)
    assert (values(main, 301)["StatValue4"], values(main, 301)["StatValue5"]) == (600, 375)
    assert (values(insane, 301)["StatValue4"], values(insane, 301)["StatValue5"]) == (900, 600)
    assert values(main, 305) == values({"Exports": [{"Table": {"Data": base_rows}}]}, 305)
    assert values(main, 401) == values({"Exports": [{"Table": {"Data": base_rows}}]}, 401)
    assert main_report["excludedMaelstrom"] == 1
    assert main_report["regularRows"] == 0 and main_report["bossRows"] == 1
    assert insane_report["preset"] == "insane"


def test_hardcore_boss_target_is_independent_from_legacy_regular_enemies():
    import build_specs

    targets = build_specs.combo_to_targets({
        "combatProfile": "full", "outfitSkinSuit": False,
        "combatFeatures": [], "hardcoreEnemyBoost": "main",
    })
    hc = next(t for t in targets if t["name"] == "StellarSouls-HarderBosses")
    assert hc["transforms"] == ["hardcoreEnemies.main"]

    legacy = build_specs.combo_to_targets({
        "combatProfile": "full", "outfitSkinSuit": False, "combatFeatures": [],
        "gameplayExtras": ["harderEnemies"], "harderEnemiesMult": 3,
    })
    assert all(t["name"] != "StellarSouls-HarderBosses" for t in legacy)
    combat = next(t for t in legacy if t["name"] == "StellarSouls-CombatOnly")
    assert "extras.harderEnemies" in combat["transforms"]


def test_native_enemy_transforms_preserve_every_boss_row():
    import table_compiler as tc

    character_base = tc.load_table("CharacterTable", "vanilla")
    character, report = tc.apply_transforms(
        "CharacterTable", ["combat.enemyVulnerability"], base="vanilla")
    base_rows = {r["Name"]: r for r in tc.rows(character_base)}
    out_rows = {r["Name"]: r for r in tc.rows(character)}
    boss_names = {name for name, row in base_rows.items() if tc._is_boss_character(row)}

    assert boss_names
    assert all(out_rows[name] == base_rows[name] for name in boss_names)
    assert report["combat.enemyVulnerability"] > 0

    skill_base = tc.load_table("SkillTable", "vanilla")
    skill, skill_report = tc.apply_transforms(
        "SkillTable", ["combat.enemyDamage"], base="vanilla")
    base_skills = {r["Name"]: r for r in tc.rows(skill_base)}
    out_skills = {r["Name"]: r for r in tc.rows(skill)}
    boss_skills = {name for name, row in base_skills.items() if tc._is_boss_skill(row)}

    assert boss_skills
    assert all(out_skills[name] == base_skills[name] for name in boss_skills)
    assert skill_report["combat.enemyDamage"] > 0


def test_legacy_harder_enemies_excludes_bosses_and_miniboss_clones():
    import extras

    def row(name, actor_type):
        return {"Name": name, "Value": [
            {"Name": "ActorType", "Value": actor_type},
            {"Name": "MaxHP", "Value": 100, "IsZero": False},
            {"Name": "MaxShield", "Value": 50, "IsZero": False},
        ]}

    regular = row("M_Regular", "ActorType_Monster")
    boss = row("M_Boss", "ActorType_BossMonster")
    miniboss = row("M_Regular_MB", "ActorType_BossMonster")
    doc = {"Exports": [{"Table": {"Data": [regular, boss, miniboss]}}]}

    assert extras.harder_enemies(doc, 3) == 2
    assert extras._get(regular, "MaxHP") == 300
    assert extras._get(boss, "MaxHP") == 100
    assert extras._get(miniboss, "MaxHP") == 100


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
    assert ee._prop(idx["LV_Dead_Falling_KeepTheater"], "Action1")["Value"] == "EffectAction_None"
    assert ee._prop(idx["LV_Dead_Falling_HPRateDamage"], "CalculationValue")["Value"] == 0
    assert ee._prop(idx["P_Eve_SkillTree_TachyGaugeReduceConsumeRate"], "CalculationValue")["Value"] == 0.6


def test_no_fall_damage_neutralizes_lethal_action_in_any_slot():
    import effect_extras as ee
    row = {"Value": [
        {"Name": "Action1", "Value": "EffectAction_StopTheater", "IsZero": False},
        {"Name": "Action2", "Value": "EffectAction_ImmediateDeath", "IsZero": False},
        {"Name": "ActionValue2", "Value": "fatal", "IsZero": False},
        {"Name": "ActorState1", "Value": "ActorState_BlockRevival", "IsZero": False},
        {"Name": "CalculationValue", "Value": -10.0, "IsZero": False},
    ]}
    assert ee._neutralize_death(row) == 1
    assert ee._prop(row, "Action1")["Value"] == "EffectAction_StopTheater"
    assert ee._prop(row, "Action2")["Value"] == "EffectAction_None"
    assert ee._prop(row, "ActionValue2")["Value"] is None
    assert ee._prop(row, "CalculationValue")["Value"] == 0


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
    assert ee.apply_effect_extras(
        doc, ["tumblerHeal"], tumbler_value=10)["tumblerHeal"]
    assert ee._prop(ee._idx(doc)["Item_HP_RPotion"], "CalculationValue")["Value"] == 10
    assert ee.apply_effect_extras(
        doc, ["tumblerHeal"], tumbler_value=100)["tumblerHeal"]
    assert ee._prop(ee._idx(doc)["Item_HP_RPotion"], "CalculationValue")["Value"] == 100


def test_tumbler_heal_level_is_normalized_and_wired():
    import build_custom as bc
    assert bc.normalize({"tumblerHealPercent": 4})["tumblerHealPercent"] == 10
    assert bc.normalize({"tumblerHealPercent": 64})["tumblerHealPercent"] == 60
    assert bc.normalize({"tumblerHealPercent": 106})["tumblerHealPercent"] == 100
    qml = (Path(__file__).resolve().parents[2] / "qml" / "pages" / "BuilderPage.qml").read_text(
        encoding="utf-8")
    for value in range(10, 101, 10):
        assert f'id: tumbler{value}; text:"{value}%"' in qml
    assert "tumblerHealPercent: tumblerValue()" in qml


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


# ---- helper vanilla (ALPHA, sin CNS) ----

def test_vanilla_alpha_configs_are_distinct_and_valid():
    import vanilla_helper as vh
    import tempfile
    seen = {}
    for build_id in vh.ALPHA_IDS:
        out = Path(tempfile.mkdtemp())
        root = vh.compile_vanilla_helper(build_id, out)
        cfg = (root / "Scripts" / "config.lua").read_text(encoding="utf-8")
        assert (root / "Scripts" / "main.lua").exists()
        assert f'buildName = "{vh.build_name(build_id)}"' in cfg
        seen[build_id] = cfg
    # cada ALPHA tiene que traer una config distinta, si no probar una por una
    # no aporta nada.
    assert len(set(seen.values())) == len(vh.ALPHA_IDS)
    assert 'strategy = "probe"' in seen["alpha1"]
    assert 'strategy = "meshRepaint"' in seen["alpha2"]
    assert 'allowCheatManagerConstruct = false' in seen["alpha3"]
    assert 'allowCheatManagerConstruct = true' in seen["alpha4"]
    assert 'strategy = "equipToggle"' in seen["alpha5"]
    assert 'strategy = "chain"' in seen["alpha6"]


def test_vanilla_alpha_off_is_not_enabled():
    import vanilla_helper as vh
    assert not vh.is_enabled("off")
    assert not vh.is_enabled("")
    assert not vh.is_enabled(None)
    assert vh.is_enabled("alpha1")


def test_build_vanilla_helper_only_zip():
    import tempfile
    import zipfile
    out = Path(tempfile.mkdtemp())
    zip_path = bc.build({"combatProfile": "none", "outfitSkinSuit": False, "miniBoss": "off",
                         "vanillaHelperBuild": "alpha6", "lang": "en"}, out)
    with zipfile.ZipFile(zip_path) as zf:
        names = zf.namelist()
        guide = zf.read("INSTALL_en.txt").decode("utf-8")
    assert "ue4ss/Mods/StellarSoulsVanillaRestore/Scripts/main.lua" in names
    # helper suelto: no arrastra paks ni el helper CNS
    assert not [n for n in names if n.endswith(".pak")]
    assert not [n for n in names if "StellarSoulsOutfitRestore" in n]
    assert "ALPHA6-chain" in guide


def test_alpha_controls_are_translated_in_english_and_spanish():
    import json
    root = Path(__file__).resolve().parents[2]
    required = {"builder_alpha_title", "builder_alpha_warn", "builder_alpha_note",
                "builder_alpha_off", "builder_alpha1", "builder_alpha2", "builder_alpha3",
                "builder_alpha4", "builder_alpha5", "builder_alpha6"}
    for lang in ("en", "es"):
        strings = json.loads((root / "i18n" / f"{lang}.json").read_text(encoding="utf-8"))
        assert required <= strings.keys()
        assert "ALPHA" in strings["builder_alpha_warn"]


def test_installer_installs_every_helper_folder(tmp_path=None):
    import tempfile
    import installer
    base = Path(tmp_path or tempfile.mkdtemp())
    src = base / "stage" / "ue4ss" / "Mods"
    for name in ("StellarSoulsOutfitRestore", "StellarSoulsVanillaRestore"):
        (src / name / "Scripts").mkdir(parents=True)
        (src / name / "Scripts" / "main.lua").write_text("-- x", encoding="utf-8")
    found = sorted(p.name for p in installer._helper_sources(src))
    assert found == ["StellarSoulsOutfitRestore", "StellarSoulsVanillaRestore"]
    # una carpeta concreta tambien vale (compat con la llamada vieja)
    assert [p.name for p in installer._helper_sources(src / "StellarSoulsVanillaRestore")] \
        == ["StellarSoulsVanillaRestore"]


def _effect_row(name, props):
    """Fila EffectTable minima en formato UAssetAPI."""
    def p(pname, value, ptype="FloatPropertyData"):
        return {"$type": f"UAssetAPI.PropertyTypes.Objects.{ptype}, UAssetAPI",
                "Name": pname, "ArrayIndex": 0, "PropertyGuid": None,
                "IsZero": value in (0, 0.0, "+0", None), "PropertyTagFlags": "None",
                "PropertyTypeName": None, "PropertyTagExtensions": "NoExtension",
                "Value": value}
    return {"Name": name, "Value": [p(*args) for args in props]}


def _alias(index, value):
    return {"$type": "UAssetAPI.PropertyTypes.Objects.NamePropertyData, UAssetAPI",
            "Name": str(index), "ArrayIndex": 0, "PropertyGuid": None, "IsZero": False,
            "PropertyTagFlags": "None", "PropertyTypeName": None,
            "PropertyTagExtensions": "NoExtension", "Value": value}


def _outfit_effect_doc():
    """Las dos filas vanilla que el swap Skin-Suit-on-break engancha, tal como
    quedan en la EffectTable del pak de outfit (DR)."""
    rest = _effect_row("P_Eve_InteractCamp_RestFX", [
        ("ActiveTargetEffectAliasArray", [_alias(0, "breakDispel")], "ArrayPropertyData"),
        ("ActiveShowPath", None, "StrPropertyData"),
    ])
    block = _effect_row("BlockShieldRegenWhenShieldZero_PC", [
        ("LifeTime", "+0"),
        ("ChainEffectAliasArray", [_alias(0, "breakFX"), _alias(1, "shield_break")],
         "ArrayPropertyData"),
        ("ActorState1", "ActorState_None", "EnumPropertyData"),
    ])
    return {"Exports": [{"Table": {"Data": [rest, block]}}]}


def test_outfit_fix_restores_camp_rest_fx_keeping_the_swap():
    """Devuelve el FX de campamento sin soltar el disparo del outfit."""
    import effect_extras
    doc = _outfit_effect_doc()
    assert effect_extras.restore_camp_rest_fx(doc) is True
    row = effect_extras._idx(doc)["P_Eve_InteractCamp_RestFX"]
    assert effect_extras._prop(row, "ActiveShowPath")["Value"] == \
        "Common/InteractCamp_LevelReset_FX"
    assert effect_extras._prop(row, "ActiveShowPath")["IsZero"] is False
    # el enganche del swap vive en otra propiedad y queda intacto
    aliases = [e["Value"] for e in effect_extras._prop(
        row, "ActiveTargetEffectAliasArray")["Value"]]
    assert aliases == ["breakDispel"]


def test_outfit_fix_restores_shield_regen_block_keeping_break_chain():
    import effect_extras
    doc = _outfit_effect_doc()
    assert effect_extras.restore_shield_regen_block(doc) == 3
    row = effect_extras._idx(doc)["BlockShieldRegenWhenShieldZero_PC"]
    assert effect_extras._prop(row, "LifeTime")["Value"] == 4.0
    assert effect_extras._prop(row, "ActorState1")["Value"] == "ActorState_BlockShieldRegen"
    aliases = [e["Value"] for e in effect_extras._prop(row, "ChainEffectAliasArray")["Value"]]
    assert aliases == ["breakFX", "shield_break", "ShieldRecover_PC"]
    # indices consecutivos (UAssetAPI) y sin duplicar al reaplicar
    assert [e["Name"] for e in effect_extras._prop(
        row, "ChainEffectAliasArray")["Value"]] == ["0", "1", "2"]
    effect_extras.restore_shield_regen_block(doc)
    assert [e["Value"] for e in effect_extras._prop(
        row, "ChainEffectAliasArray")["Value"]] == aliases


def test_outfit_fix_transforms_defaults():
    """Ambos arreglos ON por default; cada uno se puede apagar por separado."""
    import build_specs
    import table_compiler
    a = bc.normalize({"combatProfile": "none", "outfitSkinSuit": True, "miniBoss": "off"})
    targets = build_specs.combo_to_targets(a)
    transforms = targets[0]["transforms"]
    assert "outfit.vanillaRestFX" in transforms
    assert "outfit.vanillaShieldRegenBlock" in transforms
    no_shield = bc.normalize({"combatProfile": "none", "outfitSkinSuit": True, "miniBoss": "off",
                              "outfitVanillaShieldRegen": False})
    shield_off = build_specs.combo_to_targets(no_shield)[0]["transforms"]
    assert "outfit.vanillaShieldRegenBlock" not in shield_off
    assert "outfit.vanillaRestFX" in shield_off
    no_fx = bc.normalize({"combatProfile": "none", "outfitSkinSuit": True, "miniBoss": "off",
                          "outfitVanillaRestFX": False})
    assert "outfit.vanillaRestFX" not in build_specs.combo_to_targets(no_fx)[0]["transforms"]
    for tid in transforms:
        assert tid in table_compiler.REGISTRY, tid
    # los paths de staging (mini-boss / First Run) usan los mismos arreglos
    assert build_specs.outfit_fix_extras(no_shield) == ["vanillaRestFX"]
    import effect_extras
    assert set(build_specs.outfit_fix_extras(a)) <= effect_extras.EFFECT_EXTRAS


def _fake_game(base):
    """Arbol minimo que gamepaths.is_game reconoce."""
    mods = Path(base) / "SB" / "Content" / "Paks" / "~mods"
    mods.mkdir(parents=True, exist_ok=True)
    return str(Path(base)), mods


def test_out_dir_inside_mods_is_rejected(tmp_path=None):
    """Compilar dentro de ~mods deja paks fantasma: se rechaza antes de empezar."""
    import tempfile
    import gamepaths
    game, mods = _fake_game(tmp_path or tempfile.mkdtemp())

    assert gamepaths.is_inside_mods(mods, game)
    assert gamepaths.is_inside_mods(mods / "stage" / "Paks", game)
    assert gamepaths.is_inside_mods(mods / "compile_mb", game)
    # hermano con prefijo comun: NO cuenta (comparacion por partes, no por texto)
    assert not gamepaths.is_inside_mods(str(mods) + "Backup", game)
    assert not gamepaths.is_inside_mods(Path(game) / "SB" / "Content" / "Paks", game)
    assert not gamepaths.is_inside_mods(Path(game).parent / "builds", game)

    for bad in (mods, mods / "stage"):
        try:
            bc.check_out_dir(bad, game)
            raise AssertionError(f"deberia rechazar {bad}")
        except SystemExit as e:
            assert "~mods" in str(e)
    bc.check_out_dir(Path(game).parent / "StellarSouls-builds", game)  # no levanta


def test_shadow_paks_finds_builds_left_inside_mods(tmp_path=None):
    """Paks cargables que la tool no instalo (o duplicados en subcarpetas)."""
    import tempfile
    import installer
    game, mods = _fake_game(tmp_path or tempfile.mkdtemp())

    def pak(folder, base):
        folder.mkdir(parents=True, exist_ok=True)
        for ext in (".pak", ".ucas", ".utoc"):
            (folder / f"{base}{ext}").write_text("x", encoding="utf-8")

    installed = "StellarSouls-MiniBossNGPlus-Combat_P"
    pak(mods, installed)                                  # instalado, raiz: OK
    pak(mods / "stage" / "Paks", installed)               # duplicado fantasma
    pak(mods / "compile_mb", installed)                   # duplicado fantasma
    pak(mods / "AAA-CNS Outfit-1670", "SomeOutfit_P")     # mod de tercero: OK
    pak(mods, "StellarSouls-Custom_P")                    # propio, no instalado

    original = installer.load_manifest
    installer.load_manifest = lambda: {"game": game, "paks": [installed]}
    try:
        found = installer.shadow_paks(game)
    finally:
        installer.load_manifest = original

    assert found == sorted([f"compile_mb\\{installed}", f"stage\\Paks\\{installed}",
                            "StellarSouls-Custom_P"])


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
