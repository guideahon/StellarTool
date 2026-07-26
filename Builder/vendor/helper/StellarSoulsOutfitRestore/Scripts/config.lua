return {
    -- 1.2.33 - random rolls ONLY after a real SkinSuit phase (no combat-end rolls).
    -- Recommended with Semantic Minimal BreakDispel TEST 1.2.32G (keeper table).
    --
    -- ESE-Cosmetic puts EVE in SkinSuit around shield break; this helper restores
    -- the LAST REMEMBERED CNS body automatically when combat ends
    -- (bBattleState true -> false). It writes LastActiveOutfitMap["EVE-Body-"]
    -- before ReloadDataFromLastSave, avoiding BS_102/default/random CNS reloads.

    -- Auto-restore CNS when the player leaves combat.
    enableAutoRestoreOnBattleEnd = false,
    -- Fallback watcher: reads only SBCharacter.bBattleState, not shield/outfit objects.
    -- This catches combat-end edges if Event_ChangeBattleState does not fire.
    enableBattleStateWatcher = false,
    battleStateWatchIntervalMs = 750,
    startupWatchGraceMs = 8000,
    -- BS_102 is written when the shield BREAKS, not only when it refills.
    -- Restoring from this value therefore fires too early. Keep this diagnostic
    -- watcher disabled; the equipment/body-mesh detach edge below is the
    -- confirmed shield-full signal.
    enableSavedBodySkinSuitWatcher = false,
    savedBodyWatchIntervalMs = 10,
    savedBodyRestoreDelayMs = 0,
    -- Burst repaint shortly after the immediate restore. This attempts to hide
    -- the table-side visible intermediate BS_102 restore frame.
    -- 1.2.36: random modes load a COLD (uncached) outfit on each pick; every
    -- extra ReloadDataFromLastSave re-stalls the game thread. The 5-reload burst
    -- turned the shield-full moment into a 1-2 s freeze. Trimmed to a single
    -- follow-up repaint (empty burst list + one followup below). If you see a
    -- brief default-outfit flash before the random outfit, add back one entry
    -- e.g. { 120 }.
    savedBodyBurstRepaintDelaysMs = { },
    -- Legacy single follow-up fallback, used only if burst list is empty.
    savedBodyFollowupRepaintDelayMs = 120,
    -- Failed experiment in 1.2.15 for ESE-Cosmetic. Keep disabled by default.
    -- Repeated SkinSuit breaks need ESE-Shield's table-side restore loop.
    enablePostRestoreResetPulse = false,
    resetPulseStartDelayMs = 900,
    resetPulseSkinSuitHoldMs = 120,
    -- Wait this long after combat ends before restoring (lets the shield settle).
    restoreDelayMs = 1500,
    -- Minimum gap between auto restores, to avoid repeated reloads / visual pop.
    restoreCooldownMs = 4000,
    -- 1.2.41: skip a repeat reload of the SAME outfit within this many ms. Several
    -- watchers force a reload for one shield-full; the outfit is already set after
    -- the first, so the rest are redundant and, on fault-prone outfits, spam UE4SS
    -- crash .dmp files. Lower = more repaints (safer against flash), higher = fewer
    -- dumps. Alt+S always forces.
    forcedReloadDedupMs = 300,
    -- Disabled by default: on builds where EquipmentDataList does not expose
    -- BS_102, absence is not proof of shield-full and caused early restores.
    enableStuckSkinSuitGuard = false,
    stuckSkinSuitCheckIntervalMs = 1000,
    stuckSkinSuitGraceMs = 2500,
    bodySlotKey = "EVE-Body-",
    skinSuitAlias = "BS_102",

    -- Mesh events remain enabled for diagnostics, but must not restore merely
    -- because the saved CNS body is BS_102: that state begins at shield break.
    enableMeshEventHooks = true,
    enableMeshEventRestore = false,
    meshEventLogThrottleMs = 250,
    -- meshEventHookNames = { "/Script/SB.SBCharacter:NotifyBP_SetMesh",
    --                        "/Script/SB.SBCharacter:ApplyMeshInfo" },

    -- 1.2.21: watch SBCharacter.OverrideEffectAlias for the break-effect alias.
    -- The 1.2.32G table dispels nanosuit_break exactly at shield 100%; the
    -- present -> absent edge of the alias triggers the CNS restore at that
    -- moment (and at Tachy exit from SkinSuit), instead of at combat end.
    -- Array contents are logged on change ("EFFECT aliases changed") so a
    -- different alias name can be added to breakEffectAliases if needed.
    -- 1.2.21 log proved this array stays empty in this build; default off.
    enableEffectAliasWatcher = false,
    effectAliasWatchIntervalMs = 100,
    breakEffectAliases = { "nanosuit_break" },
    -- If true, the edge only restores while still in battle.
    effectAliasRestoreOnlyInBattle = false,

    -- 1.2.22: poll player.EquipmentDataList and player:GetBodyMeshName().
    -- nanosuit_break attaches BS_102 equipment; its removal at shield 100% is
    -- the wanted restore moment. Changes are logged ("EQUIP list changed",
    -- "BODYMESH changed"); the marker substring present -> absent edge fires
    -- the immediate CNS restore.
    enableEquipmentListWatcher = true,
    equipmentWatchIntervalMs = 100,
    enableBodyMeshWatcher = true,
    -- Experimental zero-flash path: catch the detach edge within the same frame.
    bodyMeshWatchIntervalMs = 1,
    signalMarkerSubstring = "BS_102",
    enableSignalEdgeRestore = true,
    -- 1.2.23: real mesh names from the 1.2.22 log. The body-mesh watcher
    -- restores last CNS the instant the mesh LEAVES SkinSuit/Tachy onto a mesh
    -- that is not the remembered good CNS mesh (= the default repaint,
    -- CH_P_EVE_09 in the log). Covers shield 100% in combat, combat end and
    -- Tachy exit.
    -- 1.2.26: unexpected-repaint guard. The 23:31 log showed a LATE engine
    -- repaint (~4 s after the combat-end restore) painting the default outfit
    -- again. If the mesh leaves the good CNS mesh toward a non-SkinSuit/Tachy
    -- mesh, wait unexpectedRepaintVerifyMs and check the CNS saved alias: if it
    -- did not change (not a user outfit change) and the mesh is still wrong,
    -- restore immediately.
    -- 1.2.27: restore mode.
    --   "last"           -> restore the last CNS outfit you wore (default).
    --   "randomFavorite" -> pick a RANDOM outfit from your CNS favourites
    --                       (read automatically from CNS FavouritesMap) on
    --                       every restore (shield 100%, Tachy exit, combat end).
    -- favoriteOutfits below is only a manual fallback/override, used when the
    -- CNS FavouritesMap is empty or unreadable. Alt+X dumps your favourites.
    -- 1.2.29: after you change outfit in CNS, all automatic restores are
    -- suppressed for this long so the helper never reverts your choice.
    userOutfitChangeTruceMs = 4000,
    -- 1.2.30: folder scanned for *.dekcns.json outfit descriptors (UniqueFitID
    -- + FitMeshType). Relative to the game Win64 folder; override if needed:
    -- modsFolderPath = "C:\...\StellarBlade\SB\Content\Paks\~mods",
    -- 1.2.32: one random pick per restore event; reused this long (10 s covers
    -- late post-combat repaint triggers so the outfit does not change twice).
    randomPickStickyMs = 10000,
    -- "randomAny": pick a RANDOM outfit from EVERY installed CNS body outfit
    -- (all *.dekcns.json with FitMeshType "Body" in ~mods), not just favourites.
    -- Needs the dekcns registry scan to find your outfits; if empty it falls
    -- back to the CNS FavouritesMap, then the manual favoriteOutfits list.
    restoreMode = "randomAny",
    favoriteOutfits = {
        -- "NG2_Black_FiendRachel_8417",
        -- "BS_30_Var2",
    },
    enableUnexpectedRepaintRestore = true,
    unexpectedRepaintVerifyMs = 250,
    skinSuitMeshName = "CH_P_EVE_InnerSuit",
    tachyMeshNames = { "CH_P_EVE_10" },
    -- 1.2.35: guard against entering Tachy (with a broken shield) painting a
    -- random/default CNS outfit. When the mesh leaves SkinSuit/Tachy, wait this
    -- long and re-check: if it settled on a Tachy/SkinSuit mesh it was an ENTER,
    -- not a repaint, so no restore/re-roll fires. Raise if Tachy still re-rolls.
    tachyEnterGuardMs = 90,

    -- 30s variant: on top of everything the Random CNS helper does, roll a NEW
    -- random CNS body every periodicRandomCnsIntervalMs. The tick fires ONLY
    -- while EVE is currently wearing a normal (random) CNS body mesh; if the
    -- current mesh is SkinSuit, Tachy or any special/unknown mesh the tick is
    -- skipped. Respects the user outfit-change truce and skips while a
    -- shield-break restore is in flight. Set enablePeriodicRandomCns = false to
    -- get the plain Random CNS behavior.
    enablePeriodicRandomCns = true,
    periodicRandomCnsIntervalMs = 30000,

    -- Alt+S: restore CNS now (manual, ignores cooldown).
    enableHotkey = true,
    -- Alt+X: minimal diagnostic (player + CNS presence + current bBattleState).
    enableDiagnosticHotkey = true,
    -- Alt+P: compact player-state snapshot.
    enableStateSnapshotHotkey = true,
    snapshotMaxArrayItems = 20,
}
