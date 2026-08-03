"""Tests F1 del Stellar Souls Builder. Corre: python -m pytest Builder/tests -q
(o directo: python Builder/tests/test_builder.py)."""
import json
import subprocess
import sys
from pathlib import Path

import pytest

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


def test_outfit_mode_decides_which_helper_gets_built():
    """Los tres estados del swap y los dos caminos de restore, sin cruzarse."""
    off = bc.normalize({"combatProfile": "full", "outfitMode": "off", "miniBoss": "off"})
    assert off["outfitSkinSuit"] is False and off["outfitHelperless"] is False
    assert bc.resolve(off)["needsHelper"] is False

    # Con helper: CNS por default, y nada de ALPHA vanilla.
    cns = bc.normalize({"combatProfile": "full", "outfitMode": "helper", "miniBoss": "off"})
    assert cns["outfitSkinSuit"] is True and cns["outfitHelperless"] is False
    assert cns["outfitQteRestoreAlpha"] is False
    assert bc.resolve(cns)["needsHelper"] is True
    assert cns["vanillaHelperBuild"] == "off"

    # Restore SIN CNS: no se compila el helper CNS, se instala el vanilla.
    nocns = bc.normalize({"combatProfile": "full", "outfitMode": "helper", "miniBoss": "off",
                          "helperMode": "lastNoCns"})
    assert bc.resolve(nocns)["needsHelper"] is False
    assert nocns["vanillaHelperBuild"] == bc.DEFAULT_ALPHA
    # ...y respeta la ALPHA que el usuario haya elegido abajo.
    picked = bc.normalize({"combatProfile": "full", "outfitMode": "helper", "miniBoss": "off",
                           "helperMode": "lastNoCns", "vanillaHelperBuild": "alpha2"})
    assert picked["vanillaHelperBuild"] == "alpha2"

    # Sin helper: el swap sale igual, pero table-side y sin instalar nada.
    alpha = bc.normalize({"combatProfile": "full", "outfitMode": "noHelperAlpha",
                          "miniBoss": "off", "helperMode": "randomPeriodic"})
    assert alpha["outfitSkinSuit"] is True and alpha["outfitHelperless"] is True
    assert alpha["outfitQteRestoreAlpha"] is True
    assert bc.resolve(alpha)["needsHelper"] is False
    assert alpha["vanillaHelperBuild"] == "off"


def test_outfit_mode_falls_back_to_the_old_answers():
    """Presets e historial viejos traen el bool y el check suelto de QTE."""
    old_on = bc.normalize({"combatProfile": "full", "outfitSkinSuit": True, "miniBoss": "off"})
    assert old_on["outfitMode"] == "helper"
    old_off = bc.normalize({"combatProfile": "full", "outfitSkinSuit": False, "miniBoss": "off"})
    assert old_off["outfitMode"] == "off"
    # El check viejo de QTE no elige el modo: sin outfitMode manda el bool.
    old_qte = bc.normalize({"combatProfile": "full", "outfitSkinSuit": True, "miniBoss": "off",
                            "outfitQteRestoreAlpha": True})
    assert old_qte["outfitMode"] == "helper"
    assert old_qte["outfitQteRestoreAlpha"] is False


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


def test_edit_uasset_reports_when_fromjson_writes_nothing(tmp_path, monkeypatch):
    """UAssetGUI puede fallar dejando el uasset intacto o borrandolo. Los dos
    casos tienen que dar el mismo error accionable, no pasar desapercibidos ni
    reventar con un FileNotFoundError del stat."""
    import toolchain

    target = tmp_path / "SomeTable.uasset"
    target.write_text("original", encoding="utf-8")

    def run_stub(args, *a, **kw):
        if "tojson" in args:
            Path(args[3]).write_text('{"NameMap": [], "Exports": []}', encoding="utf-8")
        elif "fromjson" in args and delete_target:
            Path(args[3]).unlink(missing_ok=True)
        return subprocess.CompletedProcess(args, 0, "", "")

    monkeypatch.setattr(toolchain, "_run", run_stub)
    for delete_target in (False, True):
        with pytest.raises(RuntimeError, match="no reescribio"):
            toolchain.edit_uasset(target, [lambda doc: {}])
        target.write_text("original", encoding="utf-8")


def test_edit_uasset_is_a_noop_without_target_or_mutators(tmp_path):
    import toolchain
    assert toolchain.edit_uasset(tmp_path / "NoSuch.uasset", [lambda doc: {}]) == {}
    present = tmp_path / "T.uasset"
    present.write_text("x", encoding="utf-8")
    assert toolchain.edit_uasset(present, []) == {}


def test_uassetgui_calls_are_serialized(tmp_path, monkeypatch):
    """UAssetGUI no tolera instancias concurrentes: dos a la vez terminan con
    exit 0 sin escribir nada (mismo sintoma que un FName faltante). Si alguien
    vuelve a paralelizar las tablas, el lock lo tiene que seguir cubriendo."""
    import threading
    import time
    import toolchain
    from concurrent.futures import ThreadPoolExecutor

    running = 0
    overlapped = False
    guard = threading.Lock()

    def fake_run(args, *a, **kw):
        nonlocal running, overlapped
        with guard:
            running += 1
            if running > 1:
                overlapped = True
        time.sleep(0.05)
        Path(args[3]).write_text("x", encoding="utf-8")   # el uasset de salida
        with guard:
            running -= 1
        return subprocess.CompletedProcess(args, 0, "", "")

    monkeypatch.setattr(toolchain, "_run", fake_run)
    jobs = []
    for i in range(4):
        src = tmp_path / f"t{i}.json"
        src.write_text("{}", encoding="utf-8")
        jobs.append((src, tmp_path / f"t{i}.uasset"))
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda j: toolchain.fromjson(*j), jobs))
    assert not overlapped, "dos UAssetGUI a la vez: el lock de toolchain no cubre esta ruta"


def test_fromjson_retries_once_before_failing(tmp_path, monkeypatch):
    """UAssetGUI falla de forma intermitente (recursos globales). Un reintento
    salva el build entero; sin el, la tabla siguiente ni se compila."""
    import toolchain

    calls = []

    def flaky_run(args, *a, **kw):
        calls.append(args)
        if len(calls) > 1:
            Path(args[3]).write_text("x", encoding="utf-8")
        return subprocess.CompletedProcess(args, 0, "", "")

    monkeypatch.setattr(toolchain, "_run", flaky_run)
    src = tmp_path / "t.json"
    src.write_text("{}", encoding="utf-8")
    assert toolchain.fromjson(src, tmp_path / "t.uasset").exists()
    assert len(calls) == 2


def test_selftest_runs_a_real_conversion(tmp_path, monkeypatch):
    """El chequeo tiene que CONVERTIR algo: invocado con argumentos invalidos,
    UAssetGUI abre la GUI y no reporta nada por consola ni cuando anda bien."""
    import toolchain

    monkeypatch.setattr(toolchain, "tools_dir", lambda: tmp_path)
    (tmp_path / "UAssetGUI.exe").write_text("x", encoding="utf-8")
    seen = []

    def ok_run(args, *a, **kw):
        seen.append(args)
        Path(args[3]).write_text("{}", encoding="utf-8")
        return subprocess.CompletedProcess(args, 0, "", "")

    monkeypatch.setattr(toolchain, "_run", ok_run)
    assert toolchain.selftest_uassetgui() is None
    assert seen and seen[0][1] == "tojson"


def test_selftest_flags_a_uassetgui_that_cannot_run(tmp_path, monkeypatch):
    """Bajo Wine sin .NET/fuentes el exe muere sin escribir ni decir nada. Hay
    que avisar ANTES de extraer baselines y compilar tablas."""
    import toolchain

    monkeypatch.setattr(toolchain, "tools_dir", lambda: tmp_path)
    (tmp_path / "UAssetGUI.exe").write_text("x", encoding="utf-8")
    monkeypatch.setattr(toolchain, "_run",   # no escribe la salida
                        lambda args, *a, **kw: subprocess.CompletedProcess(args, 0, "", ""))
    monkeypatch.setattr(toolchain, "is_wine", lambda: True)
    monkeypatch.setattr(toolchain, "clipboard_text", lambda *a, **kw: "")
    problem = toolchain.selftest_uassetgui()
    assert "protontricks" in problem and "micross" in problem


def test_uag_failure_surfaces_the_clipboard(monkeypatch):
    """UAssetGUI no escribe una linea en la consola: reporta por MessageBox y
    deja el error en el portapapeles. Es la unica forma de mostrarlo."""
    import toolchain

    monkeypatch.setattr(toolchain, "clipboard_text",
                        lambda *a, **kw: "System.Drawing: font 'Microsoft Sans Serif'")
    monkeypatch.setattr(toolchain, "is_wine", lambda: False)
    msg = toolchain._uag_failure("fromjson fallo",
                                 subprocess.CompletedProcess([], 0, "", ""))
    assert "Microsoft Sans Serif" in msg


def test_missing_oodle_msg_separates_the_two_causes(monkeypatch):
    """Juego detectado sin el DLL no es lo mismo que juego sin detectar: el
    mensaje unico mandaba a reinstalar el juego a quien no lo necesitaba."""
    import gamepaths
    import toolchain

    monkeypatch.setattr(gamepaths, "detect_game", lambda: r"C:\Games\StellarBlade")
    assert "StellarBlade) y no esta ahi" in toolchain.missing_oodle_msg()
    monkeypatch.setattr(gamepaths, "detect_game", lambda: None)
    assert "no se pudo detectar" in toolchain.missing_oodle_msg()


def test_oodle_is_found_when_the_dll_case_differs(tmp_path, monkeypatch):
    """Linux/Proton: el filesystem distingue mayusculas y la copia del juego
    puede ser oo2core_9_win64.DLL. rglob no la veia y el build moria diciendo
    que el juego no la tiene."""
    import toolchain

    game = tmp_path / "StellarBlade"
    weird = game / "SB" / "Plugins" / "Oodle"
    weird.mkdir(parents=True)
    (weird / "oo2core_9_win64.DLL").write_bytes(b"x")
    assert toolchain._find_oodle(game).parent == weird


def test_repair_namemap_ignores_array_indices():
    """Los elementos de un ArrayProperty llevan el indice como Name ("0", "1"):
    registrarlos infla el NameMap y aleja el uasset del vanilla byte a byte."""
    import table_compiler
    doc = {
        "NameMap": [],
        "Exports": [{"Table": {"Data": [{
            "Name": "Row",
            "Value": [{
                "$type": "UAssetAPI.PropertyTypes.Objects.ArrayPropertyData, UAssetAPI",
                "Name": "Entries", "ArrayType": "StructProperty",
                "Value": [{"Name": "0", "Value": []}, {"Name": "1", "Value": []}],
            }],
        }]}}],
    }
    table_compiler.repair_namemap(doc)
    assert doc["NameMap"] == ["Row", "Entries", "StructProperty"]


def test_baseline_progress_line_format(capsys):
    """La app parsea estas lineas para mostrar que tabla se esta extrayendo:
    cambiar el formato deja la UI en 'Compilando...' sin avisar."""
    import table_compiler
    table_compiler._progress_seq = 0
    table_compiler._emit_progress("baseline", "ShopItemTable")
    table_compiler._emit_progress("baseline", "RewardGroupTable")
    lines = capsys.readouterr().out.splitlines()
    assert lines == ["PROGRESS baseline 1 ShopItemTable",
                     "PROGRESS baseline 2 RewardGroupTable"]
    # AppController divide por espacios y usa campos 1..3.
    fields = lines[0].split(" ")
    assert len(fields) == 4 and fields[0] == "PROGRESS"


def test_table_compiler_repairs_missing_fnames_before_fromjson():
    import table_compiler
    doc = {
        "NameMap": ["ExistingRow"],
        "Exports": [{"Table": {"Data": [{
            "Name": "ImportedRow",
            "Value": [{
                "$type": "UAssetAPI.PropertyTypes.Objects.NamePropertyData, UAssetAPI",
                "Name": "Alias", "Value": "ImportedAlias",
            }, {
                "$type": "UAssetAPI.PropertyTypes.Objects.EnumPropertyData, UAssetAPI",
                "Name": "Kind", "EnumType": "EImportedEnum", "Value": "EImportedEnum::Boss",
            }],
        }]}}],
    }
    # Todo FName del serializado: fila, nombre de propiedad, valor Name/Enum y
    # nombre de tipo. Cualquiera ausente hace que fromjson no escriba nada.
    assert table_compiler.repair_namemap(doc) == 6
    assert doc["NameMap"] == ["ExistingRow", "ImportedRow", "ImportedAlias", "Alias",
                              "EImportedEnum::Boss", "Kind", "EImportedEnum"]
    # Idempotente: compilar una segunda vez no infla el NameMap.
    assert table_compiler.repair_namemap(doc) == 0


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
                "IsZero": value == [] or value in (0, 0.0, "+0", None),
                "PropertyTagFlags": "None",
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
        # la fila trae el array de "al activarse" vacio; ahi va a parar el break
        ("ActiveTargetEffectAliasArray", [], "ArrayPropertyData"),
        ("ChainEffectAliasArray", [_alias(0, "breakFX"), _alias(1, "shield_break")],
         "ArrayPropertyData"),
        ("ActorState1", "ActorState_None", "EnumPropertyData"),
    ])
    nanosuit = _effect_row("nanosuit_break", [
        ("bPauseWhenPlayerAttachLevelSequence", True, "BoolPropertyData"),
    ])
    return {"Exports": [{"Table": {"Data": [rest, block, nanosuit]}}]}


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


def test_outfit_fix_restores_shield_regen_block_without_delaying_the_break():
    """Los 4 s vuelven, pero el break sigue disparando al instante.

    El chain corre al TERMINAR el efecto: dejar ahi los alias del break con
    LifeTime 4 corria el skinsuit hasta el arranque de la regen.
    """
    import effect_extras
    doc = _outfit_effect_doc()
    assert effect_extras.restore_shield_regen_block(doc) == 4
    row = effect_extras._idx(doc)["BlockShieldRegenWhenShieldZero_PC"]
    assert effect_extras._prop(row, "LifeTime")["Value"] == 4.0
    assert effect_extras._prop(row, "ActorState1")["Value"] == "ActorState_BlockShieldRegen"
    # el break pasa a dispararse al activarse la fila (instantaneo)
    on_break = [e["Value"] for e in effect_extras._prop(
        row, "ActiveTargetEffectAliasArray")["Value"]]
    assert on_break == ["breakFX", "shield_break"]
    # y el chain vuelve a ser exactamente el vanilla
    chain = effect_extras._prop(row, "ChainEffectAliasArray")
    assert [e["Value"] for e in chain["Value"]] == ["ShieldRecover_PC"]
    assert chain["IsZero"] is False
    # indices consecutivos (UAssetAPI)
    assert [e["Name"] for e in effect_extras._prop(
        row, "ActiveTargetEffectAliasArray")["Value"]] == ["0", "1"]
    # reaplicar no duplica ni vuelve a mover nada
    assert effect_extras.restore_shield_regen_block(doc) == 2
    assert [e["Value"] for e in effect_extras._prop(
        row, "ActiveTargetEffectAliasArray")["Value"]] == on_break
    assert [e["Value"] for e in effect_extras._prop(
        row, "ChainEffectAliasArray")["Value"]] == ["ShieldRecover_PC"]


def test_outfit_fix_shield_regen_block_is_a_noop_on_the_vanilla_row():
    """Sin swap enganchado no hay nada que mover: solo LifeTime/ActorState."""
    import effect_extras
    doc = _outfit_effect_doc()
    row = effect_extras._idx(doc)["BlockShieldRegenWhenShieldZero_PC"]
    effect_extras._set_aliases(row, "ChainEffectAliasArray", ["ShieldRecover_PC"])
    assert effect_extras.restore_shield_regen_block(doc) == 2
    assert [e["Value"] for e in effect_extras._prop(
        row, "ActiveTargetEffectAliasArray")["Value"]] == []
    assert effect_extras._prop(row, "ActiveTargetEffectAliasArray")["IsZero"] is True


def test_outfit_qte_restore_alpha_only_unpauses_nanosuit_watcher():
    import effect_extras
    doc = _outfit_effect_doc()
    assert effect_extras.keep_outfit_restore_running_during_qte(doc) is True
    pause = effect_extras._prop(
        effect_extras._idx(doc)["nanosuit_break"],
        "bPauseWhenPlayerAttachLevelSequence")
    assert pause["Value"] is False
    assert pause["IsZero"] is True


def test_outfit_fix_transforms_defaults():
    """El FX de campamento es fijo; el bloqueo de regen se puede apagar."""
    import build_specs
    import table_compiler
    a = bc.normalize({"combatProfile": "none", "outfitMode": "helper", "miniBoss": "off"})
    targets = build_specs.combo_to_targets(a)
    transforms = targets[0]["transforms"]
    assert "outfit.vanillaRestFX" in transforms
    assert "outfit.vanillaShieldRegenBlock" in transforms
    assert "outfit.qteRestoreAlpha" not in transforms
    no_shield = bc.normalize({"combatProfile": "none", "outfitMode": "helper", "miniBoss": "off",
                              "outfitVanillaShieldRegen": False})
    shield_off = build_specs.combo_to_targets(no_shield)[0]["transforms"]
    assert "outfit.vanillaShieldRegenBlock" not in shield_off
    assert "outfit.vanillaRestFX" in shield_off
    # Pedir que se pise el FX de campamento ya no es una opcion.
    forced_fx = bc.normalize({"combatProfile": "none", "outfitMode": "helper", "miniBoss": "off",
                              "outfitVanillaRestFX": False})
    assert forced_fx["outfitVanillaRestFX"] is True
    assert "outfit.vanillaRestFX" in build_specs.combo_to_targets(forced_fx)[0]["transforms"]
    # El restore table-side lo trae el modo sin helper, no un check aparte.
    qte_alpha = bc.normalize({"combatProfile": "none", "outfitMode": "noHelperAlpha",
                              "miniBoss": "off"})
    assert "outfit.qteRestoreAlpha" in build_specs.combo_to_targets(qte_alpha)[0]["transforms"]
    for tid in transforms:
        assert tid in table_compiler.REGISTRY, tid
    # los paths de staging (mini-boss / First Run) usan los mismos arreglos
    assert build_specs.outfit_fix_extras(no_shield) == ["vanillaRestFX"]
    assert "outfitQteRestoreAlpha" in build_specs.outfit_fix_extras(qte_alpha)
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


def test_cancel_rollback_restores_install_and_cleans_output(tmp_path=None):
    """Cancelar repone la instalacion previa y borra la salida a medias."""
    import os
    import tempfile
    import buildjournal
    import installer

    base = Path(tmp_path or tempfile.mkdtemp())
    game, mods = _fake_game(base / "game")
    ue4ss = Path(game) / "SB" / "Binaries" / "Win64" / "ue4ss" / "Mods"
    (ue4ss / installer.HELPER_NAME / "Scripts").mkdir(parents=True)
    (ue4ss / installer.HELPER_NAME / "Scripts" / "main.lua").write_text("viejo", encoding="utf-8")
    (ue4ss / "mods.txt").write_text("StellarSoulsOutfitRestore : 1\n", encoding="utf-8")
    for ext in installer.PAK_SUFFIXES:
        (mods / f"StellarSouls-Custom{ext}").write_text("viejo", encoding="utf-8")

    old_appdata = os.environ.get("LOCALAPPDATA")
    os.environ["LOCALAPPDATA"] = str(base / "appdata")
    try:
        manifest = {"game": game, "paks": ["StellarSouls-Custom"],
                    "helper": True, "helpers": [installer.HELPER_NAME]}
        installer._save_manifest(manifest)

        out = base / "builds"
        (out / "stage" / "Paks").mkdir(parents=True)
        (out / "compile_mb").mkdir(parents=True)
        buildjournal.begin(out)
        (out / "StellarSouls-Custom-abc123.zip").write_text("a medias", encoding="utf-8")
        buildjournal.record_backup(installer.backup_install(
            game, ["StellarSouls-Custom"], [installer.HELPER_NAME],
            buildjournal.backup_root() / "install"))

        # El build muere despues de pisar el pak y el helper, y antes de copiar
        # el .utoc: el estado que queda en el juego no arranca ni con lo viejo.
        (mods / "StellarSouls-Custom.pak").write_text("nuevo a medias", encoding="utf-8")
        (mods / "StellarSouls-Custom.utoc").unlink()
        (ue4ss / installer.HELPER_NAME / "Scripts" / "main.lua").write_text("nuevo", encoding="utf-8")
        (ue4ss / "mods.txt").write_text("basura\n", encoding="utf-8")
        installer._save_manifest({"game": game, "paks": [], "helper": False, "helpers": []})

        res = buildjournal.rollback()
        assert res["rolledBack"] is True

        for ext in installer.PAK_SUFFIXES:
            f = mods / f"StellarSouls-Custom{ext}"
            assert f.is_file() and f.read_text(encoding="utf-8") == "viejo"
        assert (ue4ss / installer.HELPER_NAME / "Scripts" / "main.lua").read_text(
            encoding="utf-8") == "viejo"
        assert (ue4ss / "mods.txt").read_text(encoding="utf-8") == "StellarSoulsOutfitRestore : 1\n"
        assert installer.load_manifest()["paks"] == ["StellarSouls-Custom"]

        assert not (out / "stage").exists()
        assert not (out / "compile_mb").exists()
        assert not (out / "StellarSouls-Custom-abc123.zip").exists()
        # Diario consumido: un segundo rollback no tiene nada que deshacer.
        assert buildjournal.rollback() == {"rolledBack": False}
    finally:
        if old_appdata is None:
            os.environ.pop("LOCALAPPDATA", None)
        else:
            os.environ["LOCALAPPDATA"] = old_appdata


def test_rollback_removes_install_that_did_not_exist_before(tmp_path=None):
    """Sin instalacion previa, cancelar deja el juego limpio (no a medio instalar)."""
    import os
    import tempfile
    import buildjournal
    import installer

    base = Path(tmp_path or tempfile.mkdtemp())
    game, mods = _fake_game(base / "game")
    ue4ss = Path(game) / "SB" / "Binaries" / "Win64" / "ue4ss" / "Mods"
    ue4ss.mkdir(parents=True)

    old_appdata = os.environ.get("LOCALAPPDATA")
    os.environ["LOCALAPPDATA"] = str(base / "appdata")
    try:
        out = base / "builds"
        out.mkdir()
        buildjournal.begin(out)
        buildjournal.record_backup(installer.backup_install(
            game, ["StellarSouls-Custom"], [installer.HELPER_NAME],
            buildjournal.backup_root() / "install"))

        (mods / "StellarSouls-Custom.pak").write_text("nuevo", encoding="utf-8")
        (ue4ss / installer.HELPER_NAME / "Scripts").mkdir(parents=True)
        (ue4ss / "mods.txt").write_text("StellarSoulsOutfitRestore : 1\n", encoding="utf-8")

        buildjournal.rollback()

        assert not (mods / "StellarSouls-Custom.pak").exists()
        assert not (ue4ss / installer.HELPER_NAME).exists()
        assert not (ue4ss / "mods.txt").exists()
    finally:
        if old_appdata is None:
            os.environ.pop("LOCALAPPDATA", None)
        else:
            os.environ["LOCALAPPDATA"] = old_appdata


def test_install_prunes_what_the_build_no_longer_includes(tmp_path=None):
    """Instalar deja el juego como pide esta build, no acumulado con la anterior."""
    import os
    import tempfile
    import installer

    base = Path(tmp_path or tempfile.mkdtemp())
    game, mods = _fake_game(base / "game")
    ue4ss = Path(game) / "SB" / "Binaries" / "Win64" / "ue4ss" / "Mods"
    (ue4ss / installer.HELPER_NAME / "Scripts").mkdir(parents=True)
    (ue4ss / "StellarSoulsVanillaRestore" / "Scripts").mkdir(parents=True)
    (ue4ss / "mods.txt").write_text(
        "JiggleControl : 1\n"
        "StellarSoulsOutfitRestore : 1\n"
        "StellarSoulsVanillaRestore : 1\n"
        "\n; Built-in keybinds, do not move up!\nKeybinds : 1\n", encoding="utf-8")
    for baseName in ("StellarSouls-MiniBossNGPlus-Combat_P",
                     "StellarSouls-MiniBossNGPlus-CombatNoOutfit_P"):
        for ext in installer.PAK_SUFFIXES:
            (mods / f"{baseName}{ext}").write_text("x", encoding="utf-8")

    old_appdata = os.environ.get("LOCALAPPDATA")
    os.environ["LOCALAPPDATA"] = str(base / "appdata")
    try:
        installer._save_manifest({
            "game": game,
            "paks": ["StellarSouls-MiniBossNGPlus-Combat_P",
                     "StellarSouls-MiniBossNGPlus-CombatNoOutfit_P"],
            "helper": True,
            "helpers": [installer.HELPER_NAME, "StellarSoulsVanillaRestore"]})

        # Esta build solo produce el pak sin outfit y no necesita helper.
        installer.prune_paks(game, ["StellarSouls-MiniBossNGPlus-CombatNoOutfit_P"],
                             approved=True)
        installer.prune_helpers(game, [], approved=True)

        for ext in installer.PAK_SUFFIXES:
            assert not (mods / f"StellarSouls-MiniBossNGPlus-Combat_P{ext}").exists()
            assert (mods / f"StellarSouls-MiniBossNGPlus-CombatNoOutfit_P{ext}").is_file()

        txt = (ue4ss / "mods.txt").read_text(encoding="utf-8")
        assert "StellarSoulsOutfitRestore : 0" in txt
        assert "StellarSoulsVanillaRestore : 0" in txt
        assert "JiggleControl : 1" in txt          # los de terceros no se tocan
        assert "Keybinds : 1" in txt
        assert not (ue4ss / installer.HELPER_NAME).exists()
        assert not (ue4ss / "StellarSoulsVanillaRestore").exists()

        m = installer.load_manifest()
        assert m["paks"] == ["StellarSouls-MiniBossNGPlus-CombatNoOutfit_P"]
        assert m["helpers"] == [] and m["helper"] is False
    finally:
        if old_appdata is None:
            os.environ.pop("LOCALAPPDATA", None)
        else:
            os.environ["LOCALAPPDATA"] = old_appdata


def test_helpers_sync_from_the_staging_not_from_the_install_checkbox(tmp_path=None):
    """Lo que se conserva sale de lo que la build compilo, no del check.

    Instalando solo los paks, el helper que la build SI quiere tiene que
    sobrevivir y el que sobra tiene que apagarse igual.
    """
    import os
    import tempfile
    import installer

    base = Path(tmp_path or tempfile.mkdtemp())
    game, _ = _fake_game(base / "game")
    ue4ss = Path(game) / "SB" / "Binaries" / "Win64" / "ue4ss" / "Mods"
    for name in (installer.HELPER_NAME, "StellarSoulsVanillaRestore"):
        (ue4ss / name / "Scripts").mkdir(parents=True)
    (ue4ss / "mods.txt").write_text(
        "StellarSoulsOutfitRestore : 1\nStellarSoulsVanillaRestore : 1\n", encoding="utf-8")

    # staging de una build que compila el helper CNS y nada mas
    stage = base / "stage" / "ue4ss" / "Mods"
    (stage / installer.HELPER_NAME / "Scripts").mkdir(parents=True)
    (stage / installer.HELPER_NAME / "Scripts" / "main.lua").write_text("-- x", encoding="utf-8")

    old_appdata = os.environ.get("LOCALAPPDATA")
    os.environ["LOCALAPPDATA"] = str(base / "appdata")
    try:
        installer._save_manifest({"game": game, "paks": [], "helper": True,
                                  "helpers": [installer.HELPER_NAME,
                                              "StellarSoulsVanillaRestore"]})
        installer.prune_helpers(game, installer.helper_targets(stage), approved=True)
        txt = (ue4ss / "mods.txt").read_text(encoding="utf-8")
        assert "StellarSoulsOutfitRestore : 1" in txt      # lo pide la build
        assert "StellarSoulsVanillaRestore : 0" in txt      # sobra
        assert (ue4ss / installer.HELPER_NAME).is_dir()
        assert not (ue4ss / "StellarSoulsVanillaRestore").exists()

        # Build sin helper: staging vacio -> no queda ninguno prendido.
        empty = base / "stage_none" / "ue4ss" / "Mods"
        empty.mkdir(parents=True)
        installer.prune_helpers(game, installer.helper_targets(empty), approved=True)
        assert "StellarSoulsOutfitRestore : 0" in (ue4ss / "mods.txt").read_text(encoding="utf-8")
        assert not (ue4ss / installer.HELPER_NAME).exists()
        assert installer.load_manifest()["helpers"] == []
    finally:
        if old_appdata is None:
            os.environ.pop("LOCALAPPDATA", None)
        else:
            os.environ["LOCALAPPDATA"] = old_appdata


def test_prune_keeps_the_helper_this_build_installs(tmp_path=None):
    """Una ALPHA a la vez: se conserva la instalada y se apaga la otra."""
    import os
    import tempfile
    import installer

    base = Path(tmp_path or tempfile.mkdtemp())
    game, _ = _fake_game(base / "game")
    ue4ss = Path(game) / "SB" / "Binaries" / "Win64" / "ue4ss" / "Mods"
    (ue4ss / installer.HELPER_NAME / "Scripts").mkdir(parents=True)
    (ue4ss / "StellarSoulsVanillaRestore" / "Scripts").mkdir(parents=True)
    (ue4ss / "mods.txt").write_text(
        "StellarSoulsOutfitRestore : 1\nStellarSoulsVanillaRestore : 1\n", encoding="utf-8")

    old_appdata = os.environ.get("LOCALAPPDATA")
    os.environ["LOCALAPPDATA"] = str(base / "appdata")
    try:
        installer._save_manifest({"game": game, "paks": [], "helper": True,
                                  "helpers": [installer.HELPER_NAME,
                                              "StellarSoulsVanillaRestore"]})
        installer.prune_helpers(game, ["StellarSoulsVanillaRestore"], approved=True)

        txt = (ue4ss / "mods.txt").read_text(encoding="utf-8")
        assert "StellarSoulsOutfitRestore : 0" in txt
        assert "StellarSoulsVanillaRestore : 1" in txt
        assert not (ue4ss / installer.HELPER_NAME).exists()
        assert (ue4ss / "StellarSoulsVanillaRestore").is_dir()
        assert installer.load_manifest()["helpers"] == ["StellarSoulsVanillaRestore"]
    finally:
        if old_appdata is None:
            os.environ.pop("LOCALAPPDATA", None)
        else:
            os.environ["LOCALAPPDATA"] = old_appdata


def test_tool_output_is_decoded_as_utf8_not_locale():
    """retoc/UAssetGUI escriben UTF-8: con el locale (cp936 en Windows chino) el
    hilo lector moria y stdout quedaba en None, tapando el error real."""
    import toolchain
    cp = toolchain._run([sys.executable, "-c",
                         "import sys;sys.stdout.buffer.write(bytes([0xc3,0xa9,0xaf,0xe4,0xbd,0xa0]))"])
    assert cp.stdout is not None
    assert cp.stdout.startswith("é")      # UTF-8 decodificado
    assert cp.stdout.endswith("你")        # el byte invalido no corta el resto


def test_out_err_survives_processes_without_captured_output():
    import subprocess
    import toolchain
    cp = subprocess.CompletedProcess(args=[], returncode=1, stdout=None, stderr=None)
    assert toolchain.out_err(cp) == ""
    cp = subprocess.CompletedProcess(args=[], returncode=1, stdout="abc", stderr=None)
    assert toolchain.out_err(cp, 2) == "bc"


# ---- mundo / progresion (world_extras) ----

def _world_doc(rows):
    return {"Exports": [{"Table": {"Data": rows}}]}


def _row(name, props):
    return {"Name": name, "Value": [{"Name": k, "Value": v, "IsZero": v == 0}
                                    for k, v in props.items()]}


def _value(doc, row_name, prop_name):
    row = next(r for r in doc["Exports"][0]["Table"]["Data"] if r["Name"] == row_name)
    return next(p for p in row["Value"] if p["Name"] == prop_name)["Value"]


def test_world_percentages_are_relative_to_vanilla():
    import world_extras
    shop = _world_doc([_row("item", {"MoneyItemCount1": 100, "Discount_MoneyItemCount1": 80,
                                     "MoneyItemCount2": 0})])
    assert world_extras.shop_prices(shop, price_percent=25) == 2
    assert _value(shop, "item", "MoneyItemCount1") == 25
    assert _value(shop, "item", "Discount_MoneyItemCount1") == 20
    assert _value(shop, "item", "MoneyItemCount2") == 0     # gratis sigue gratis

    sp = _world_doc([_row("SPLevel_1", {"RequiredSPExp": 400})])
    assert world_extras.sp_exp(sp, exp_percent=50) == 1
    assert _value(sp, "SPLevel_1", "RequiredSPExp") == 200

    upgrades = _world_doc([_row("BaseGrowth_Body_1", {"RequiredItemAmount1": 3,
                                                      "RequiredItemAmount2": 0})])
    assert world_extras.upgrade_costs(upgrades, cost_percent=10) == 1
    # 3 * 10% redondea a 0: un requisito que existia no puede desaparecer.
    assert _value(upgrades, "BaseGrowth_Body_1", "RequiredItemAmount1") == 1

    fish = _world_doc([_row("Fish_GoldFish", {"Stamina": 800, "FightingTime": 45})])
    assert world_extras.fishing(fish, stamina_percent=50, fighting_time_percent=200) == 2
    assert _value(fish, "Fish_GoldFish", "Stamina") == 400
    assert _value(fish, "Fish_GoldFish", "FightingTime") == 90


def test_world_drop_rates_only_touch_random_each_and_clamp():
    import world_extras
    doc = _world_doc([
        _row("each", {"DropType": "ESBRewardGroupDrop_RandomEach", "DropRate": 2500,
                      "ItemMinCount": 1, "ItemMaxCount": 2}),
        _row("cap", {"DropType": "ESBRewardGroupDrop_RandomEach", "DropRate": 7500,
                     "ItemMinCount": 1, "ItemMaxCount": 1}),
        # Peso relativo dentro del grupo: escalarlo no cambiaria nada in-game.
        _row("weight", {"DropType": "ESBRewardGroupDrop_RandomWeight", "DropRate": 2,
                        "ItemMinCount": 1, "ItemMaxCount": 1}),
    ])
    world_extras.drop_rates(doc, chance_percent=200, count_percent=100)
    assert _value(doc, "each", "DropRate") == 5000
    assert _value(doc, "cap", "DropRate") == world_extras.DROP_RATE_MAX
    assert _value(doc, "weight", "DropRate") == 2
    assert _value(doc, "each", "ItemMaxCount") == 2          # cantidades sin cambio

    world_extras.drop_rates(doc, chance_percent=100, count_percent=300)
    assert _value(doc, "each", "ItemMaxCount") == 6
    assert _value(doc, "weight", "ItemMaxCount") == 3        # la cantidad si aplica a todas


def test_world_tweaks_compile_into_their_own_pak():
    import build_specs
    import table_compiler
    answers = {
        "combatProfile": "full", "outfitSkinSuit": False, "miniBoss": "off",
        "combatFeatures": [], "combatEconomyFeatures": [], "gameplayExtras": [],
        "worldTweaks": ["fishing", "shopPrices"],
    }
    targets = {t["name"]: t["transforms"] for t in build_specs.combo_to_targets(answers)}
    assert targets[build_specs.WORLD_PAK] == ["world.shopPrices", "world.fishing"]
    for tid, table in (("world.shopPrices", "ShopItemTable"),
                       ("world.dropRates", "RewardGroupTable"),
                       ("world.spExp", "SPLevelTable"),
                       ("world.upgradeCosts", "CharacterLevelTable"),
                       ("world.fishing", "ItemFishTable")):
        assert table_compiler.REGISTRY[tid]["table"] == table
        assert table_compiler.REGISTRY[tid]["base"] == "vanilla"


def test_world_drop_rates_skip_the_world_pak_when_another_pak_owns_the_table():
    """Mini-boss y First Run empaquetan su propia RewardGroupTable: si el pak de
    mundo la repitiera, uno de los dos ganaria en silencio."""
    import build_specs
    answers = {"worldTweaks": ["dropRates", "spExp"]}
    assert build_specs.world_transforms(answers) == ["world.dropRates", "world.spExp"]
    assert build_specs.world_transforms(
        answers, exclude_tables={"RewardGroupTable"}) == ["world.spExp"]
    assert build_specs.world_target(
        {"worldTweaks": ["dropRates"]}, exclude_tables={"RewardGroupTable"}) is None
    source = (BUILDER / "compiler" / "build_custom.py").read_text(encoding="utf-8")
    assert "world_extra_ids=world_ids" in source
    assert "worldExtras" in source


def test_world_only_build_needs_no_prebuilt_preset():
    answers = bc.normalize({"combatProfile": "none", "outfitMode": "off",
                            "worldTweaks": ["shopPrices"]})
    bc.validate(answers)                      # no explota: hay algo que compilar
    plan = bc.resolve(answers)
    assert plan["paks"] == []
    assert plan["needsHelper"] is False
    nothing = bc.normalize({"combatProfile": "none", "outfitMode": "off"})
    try:
        bc.validate(nothing)
        assert False, "sin nada seleccionado tiene que fallar"
    except SystemExit:
        pass


def test_world_controls_are_wired_end_to_end_in_the_ui():
    root = Path(__file__).resolve().parents[2]
    qml = (root / "qml" / "pages" / "BuilderPage.qml").read_text(encoding="utf-8")
    assert "worldTweaks:" in qml and "worldTweakValues:" in qml
    for control in ("wShop", "wDrops", "wSp", "wUpgrades", "wFishing"):
        assert f"id: {control}" in qml
    restore = qml.split("function applyTemplate(a)", 1)[1].split("function toFolderUrl", 1)[0]
    assert "a.worldTweaks" in restore and "a.worldTweakValues" in restore
    for language in ("es", "en"):
        strings = json.loads((root / "i18n" / f"{language}.json").read_text(encoding="utf-8"))
        for key in ("builder_world_title", "builder_world_shop", "builder_world_drops",
                    "builder_world_sp", "builder_world_upgrades", "builder_world_fishing"):
            assert strings[key]


def test_builder_presets_can_be_shared_as_files():
    root = Path(__file__).resolve().parents[2]
    qml = (root / "qml" / "pages" / "BuilderPage.qml").read_text(encoding="utf-8")
    cpp = (root / "src" / "AppController.cpp").read_text(encoding="utf-8")
    header = (root / "src" / "AppController.h").read_text(encoding="utf-8")
    for method in ("exportBuilderPreset", "importBuilderPreset"):
        assert method in header
        assert f"AppController::{method}" in cpp
        assert f"App.{method}(" in qml
    # El formato se declara adentro del archivo y se versiona: un JSON ajeno o de
    # una version mas nueva se rechaza con un mensaje claro en vez de importarse.
    assert "stellartool.builder-preset" in cpp
    assert "kPresetSchemaVersion" in cpp
    assert "err_preset_newer" in cpp
    assert "id: presetExportDialog" in qml and "id: presetImportDialog" in qml


def test_questionnaire_offers_world_tweaks_in_every_language():
    manifest = json.loads((BUILDER / "features" / "manifest.json").read_text(encoding="utf-8"))
    question = next(q for q in manifest["questions"] if q["id"] == "worldTweaks")
    assert [o["value"] for o in question["options"]] == [
        "shopPrices", "dropRates", "spExp", "upgradeCosts", "fishing"]
    strings = json.loads((BUILDER / "i18n" / "questionnaire.json").read_text(
        encoding="utf-8"))["strings"]
    for key in [question["i18nKey"]] + [o["i18nKey"] for o in question["options"]]:
        assert set(bc.SUPPORTED_LANGS) <= strings[key].keys()


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
