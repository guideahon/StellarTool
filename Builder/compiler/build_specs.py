"""build_specs — mapea respuestas del cuestionario a targets de compilacion real.

Devuelve lista de paks (name + transforms) que TableCompiler puede compilar, o
None si el combo aun no tiene transforms (build_custom cae a preset prebuilt).

Cobertura actual (byte/content-parity probada vs shipped 1.2.19):
  - Combat full  -> CombatOnly_P  (SkillTable + CharacterTable + tuning publica)
  - Outfit break -> DirectRestore-NoRestFX_P (EffectTable + tuning publica)
Pendiente (cae a preset): mini-boss, first run.
"""
from __future__ import annotations

# Transforms que reproducen el combate publico 1.2.19 (paridad byte-identica).
COMBAT_FULL = [
    "combat.skill.full", "combat.tuning.public1219.skill",
    "combat.character.full", "combat.tuning.public1219.character",
]
# First Run y Mini-Boss se compilan fuera de este mapa (build_custom llama a
# miniboss_builder.compile_from_staging / compile_miniboss directamente).

OUTFIT = ["outfit.effectTable.skinSuitOnBreak", "outfit.tuning.public1219"]

# gaugeTweaks opcionales, componibles sobre el combate.
GAUGE_TRANSFORMS = {
    "blasterCellX2": "combat.blasterCellDamageX2",
}

COMBAT_FEATURE_TRANSFORMS = {
    "betaBurstDamage": "combat.betaBurstDamage",
    "droneDamage": "combat.droneDamage",
    "dashDamage": "combat.dashDamage",
    "eveDamage": "combat.eveDamage",
    "enemyDamage": "combat.enemyDamage",
    "perfectDodge": "combat.perfectDodge",
    "tachyDuration": "combat.tachyDuration",
    "enemyVulnerability": "combat.enemyVulnerability",
}


# Extras BETA disponibles en el path combat-only (sin mini-boss).
_CT_EXTRAS = {
    "playerQol", "ammoStacks", "consumableStacks", "shieldRegen", "attackSpeed",
    "longerTachy", "hpDrain", "harderEnemies",
    "baseAttributes", "attributeShieldRegen", "highGaugeCapacity",
    "passiveHpRegen", "fishingPower", "ammo100x",
}
_ET_EXTRAS = {
    "noFallDamage", "noEnvDeath", "tachyReduce", "strongerGear",
    "autoGaugeRecovery", "noWaterDeath", "noSandDeath", "betaParryRecovery",
    "burstDodgeRecovery", "tumblerHeal",
    "gaugeRecoveryOverTime", "droneScanBoost", "gunGorgonRotation",
}
_SK_EXTRAS = {
    "forgivingJust", "extraAirDodge", "dashCooldown4", "droneScanBoost",
}   # SkillTable -> pak de combate


def combat_transforms(a: dict) -> list[str]:
    """Transforms de combate/economía elegidos, independientes del tipo de pak."""
    combat = a.get("combatProfile", "full")
    if combat != "full":
        return []
    if combat == "full":
        selected_features = a.get("combatFeatures")
        if selected_features is None:
            transforms = list(COMBAT_FULL)
        else:
            transforms = [COMBAT_FEATURE_TRANSFORMS[x] for x in selected_features
                          if x in COMBAT_FEATURE_TRANSFORMS]
            economy = a.get("combatEconomyFeatures")
            if economy is None:
                economy = [] if a.get("combatEconomy") == "vanilla" else [
                    "slowerGain", "lowerCapacity", "cooldown"
                ]
            economy_transforms = {
                "slowerGain": "combat.slowerGain",
                "lowerCapacity": "combat.lowerCapacity",
                "cooldown": "combat.antiSpamSkill",
            }
            transforms += [economy_transforms[x] for x in economy if x in economy_transforms]
        for tw in a.get("gaugeTweaks", []) or []:
            tid = GAUGE_TRANSFORMS.get(tw)
            if tid:
                transforms.append(tid)
        return transforms
    return []


def combo_to_targets(a: dict):
    """a = respuestas normalizadas. Devuelve [ {name, transforms} ] o None."""
    combat = a.get("combatProfile", "full")
    outfit = a.get("outfitSkinSuit", True)
    mb_on = a.get("miniBoss", "off") not in ("off", False, None)

    # Mini-boss / First Run se compilan en build_custom.
    if mb_on or combat == "firstRun":
        return None

    extras = set(a.get("gameplayExtras") or [])
    ct_extras = sorted(extras & _CT_EXTRAS)
    et_extras = sorted(extras & _ET_EXTRAS)
    sk_extras = sorted(extras & _SK_EXTRAS)
    # Un control toca dos tablas; usa IDs distintos para no colisionar en el
    # registro global de transforms.
    drone_scan = "droneScanBoost" in extras
    et_extras = [e for e in et_extras if e != "droneScanBoost"]
    sk_extras = [e for e in sk_extras if e != "droneScanBoost"]

    targets = []
    hardcore = a.get("hardcoreEnemyBoost", "off")
    if hardcore in ("main", "insane"):
        targets.append({
            "name": "StellarSouls-HarderBosses",
            "transforms": [f"hardcoreEnemies.{hardcore}"],
        })
    if combat == "full":
        transforms = combat_transforms(a)
        # Extras de CharacterTable van sobre el pak de combate.
        transforms += [f"extras.{e}" for e in ct_extras] + [f"extras.{e}" for e in sk_extras]
        if drone_scan:
            transforms.append("extras.droneScanCooldown")
        if transforms:
            targets.append({"name": "StellarSouls-CombatOnly", "transforms": transforms})
    if outfit:
        # Extras de EffectTable se componen sobre el pak de outfit (base SkinSuit).
        targets.append({"name": "StellarSouls-DirectRestore-NoRestFX",
                        "transforms": list(OUTFIT) + [f"extras.{e}" for e in et_extras]
                                      + (["extras.droneScanDuration"] if drone_scan else [])})
    elif et_extras or drone_scan:
        # Sin outfit: pak propio con EffectTable VANILLA + extras (no agrega SkinSuit).
        targets.append({"name": "StellarSouls-Extras",
                        "transforms": [f"extrasVanilla.{e}" for e in et_extras]
                                      + (["extrasVanilla.droneScanDuration"] if drone_scan else [])})
    if ct_extras and combat != "full":
        # Extras de CharacterTable necesitan el pak de combate (base editable).
        pass  # se reportan como no aplicados por build_custom (warning)
    return targets or None
