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
}
_ET_EXTRAS = {
    "noFallDamage", "noEnvDeath", "tachyReduce", "strongerGear",
    "autoGaugeRecovery", "noWaterDeath", "noSandDeath", "betaParryRecovery",
    "burstDodgeRecovery", "tumblerHeal",
}
_SK_EXTRAS = {"forgivingJust", "extraAirDodge"}   # SkillTable -> pak de combate


def combo_to_targets(a: dict):
    """a = respuestas normalizadas. Devuelve [ {name, transforms} ] o None."""
    combat = a.get("combatProfile", "full")
    outfit = a.get("outfitSkinSuit", True)
    mb_on = a.get("miniBoss", "off") not in ("off", False, None)

    # Mini-boss / First Run todavia no compilables por feature.
    if mb_on or combat == "firstRun":
        return None

    extras = set(a.get("gameplayExtras") or [])
    ct_extras = sorted(extras & _CT_EXTRAS)
    et_extras = sorted(extras & _ET_EXTRAS)
    sk_extras = sorted(extras & _SK_EXTRAS)

    targets = []
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
        # Extras de CharacterTable van sobre el pak de combate.
        transforms += [f"extras.{e}" for e in ct_extras] + [f"extras.{e}" for e in sk_extras]
        if transforms:
            targets.append({"name": "StellarSouls-CombatOnly", "transforms": transforms})
    if outfit:
        # Extras de EffectTable se componen sobre el pak de outfit (base SkinSuit).
        targets.append({"name": "StellarSouls-DirectRestore-NoRestFX",
                        "transforms": list(OUTFIT) + [f"extras.{e}" for e in et_extras]})
    elif et_extras:
        # Sin outfit: pak propio con EffectTable VANILLA + extras (no agrega SkinSuit).
        targets.append({"name": "StellarSouls-Extras",
                        "transforms": [f"extrasVanilla.{e}" for e in et_extras]})
    if ct_extras and combat != "full":
        # Extras de CharacterTable necesitan el pak de combate (base editable).
        pass  # se reportan como no aplicados por build_custom (warning)
    return targets or None
