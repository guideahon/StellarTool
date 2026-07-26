-- Stellar Souls Outfit Restore - 1.2.33 (random rolls only after a SkinSuit phase)
--
-- Design (matches the table-only + UE4SS split):
--   * The EffectTable mod puts EVE in the BS_102 skin suit around shield break.
--   * This helper restores the LAST REMEMBERED CNS BODY automatically when
--     COMBAT ENDS, i.e. when SBCharacter.bBattleState goes true -> false, and
--     also when CNS save data briefly changes the body slot to BS_102.
--     Before calling ReloadDataFromLastSave(), it writes the remembered body
--     alias back into BP_CNS_SaveData_C.LastActiveOutfitMap["EVE-Body-"].
--
-- Why this approach: this build exposes no readable shield value and probing the
-- outfit objects from Lua crashes the game. bBattleState is a real, readable
-- SBCharacter property (confirmed by the Alt+P snapshot). The hook alone can
-- miss the edge in some runs, so 1.2.13 adds a lightweight bBattleState watcher
-- as a fallback trigger. 1.2.14 adds a narrower watcher over
-- BP_CNS_SaveData_C.LastActiveOutfitMap["EVE-Body-"] because user testing
-- showed Alt+S restores correctly, but the combat-end/shield-full trigger can
-- leave BS_102 active. This watcher uses the same restore path as Alt+S.
-- 1.2.15 tried an optional post-restore reset pulse. User testing showed it did
-- not make ESE-Cosmetic re-arm the second shield-break SkinSuit trigger.
-- 1.2.16 keeps the safe CNS restore path and is intended for ESE-Shield, whose
-- own EffectTable restore loop should reset ESE's SkinSuit state for repeated
-- shield breaks, while this helper restores CNS over ESE's game-outfit restore.
-- 1.2.18 makes the BS_102 interception more aggressive because user testing
-- showed the chain works but is visually SkinSuit -> BS_102 -> CNS. It restores
-- immediately and performs a short follow-up repaint to reduce the visible BS_102
-- window.
-- 1.2.19 keeps the same safe CNS-save-data signal but runs a short burst repaint
-- sequence when BS_102 is detected. This is for the 1.2.28C table path where
-- the table-side re-arm is correct, but the table still paints BS_102 before
-- the helper can repaint last CNS.
-- 1.2.20 adds NARROW hooks on SBCharacter mesh functions (NotifyBP_SetMesh,
-- ApplyMeshInfo). These are event-driven trigger points for the same safe
-- restore path: when a mesh event fires and the CNS saved body is BS_102, the
-- helper restores immediately instead of waiting for the polling watcher or
-- combat end. Each mesh event is also logged (throttled) with battle state and
-- saved body, so test logs can identify which event corresponds to the
-- table-side BS_102 repaint. No object probing: the hook callback only reads
-- the same CNS save-data string the Alt+S path already uses.
-- 1.2.21 targets restore AT SHIELD 100% instead of combat end. The 1.2.32G
-- table dispels `nanosuit_break` exactly when shield refills (that is what
-- causes the BS_102 repaint). SBCharacter exposes the ArrayProperty
-- `OverrideEffectAlias` (already read safely by the Alt+P snapshot). A new
-- watcher polls that array for the break-effect alias: when the alias was
-- present in combat and disappears (present -> absent edge, battle still true),
-- that IS the shield-full moment, and the helper runs the proven Alt+S restore
-- path immediately plus the existing burst repaints. Same logic covers Tachy
-- exit from SkinSuit. Array contents are logged on change so a wrong alias
-- name can be corrected from the log.
-- 1.2.22: the 1.2.21 log proved OverrideEffectAlias stays empty and the mesh
-- hooks never fire. New watchers over EquipmentDataList (where
-- EffectAction_AttachEquipment(BS_102) should appear/disappear; the disappear
-- edge at shield-full is the wanted moment) and GetBodyMeshName() (plain
-- getter). Both log changes and a marker-substring edge ("BS_102" present ->
-- absent) triggers the proven restore path. The dead OverrideEffectAlias
-- watcher is now default-off.
-- 1.2.23: the 1.2.22 log revealed the real mesh names (GetBodyMeshName works):
-- CNS body mesh e.g. CH_P_EVE_21_TypeB, SkinSuit = CH_P_EVE_InnerSuit,
-- Tachy = CH_P_EVE_10, default repaint = CH_P_EVE_09. The BS_102 marker never
-- appears, so the body-mesh watcher now uses transition logic: leaving
-- SkinSuit/Tachy onto a mesh that is not the remembered good CNS mesh = the
-- default repaint moment -> immediate restore (works at shield 100% in combat,
-- at combat end and at Tachy exit, reacting in <=100 ms).
--
-- 1.2.35 fixes entering Tachy mode with a random CNS outfit while the shield is
-- broken. The SkinSuit -> intermediate -> Tachy transition briefly left the
-- SkinSuit mesh onto a non-good mesh, which the body-mesh watcher treated as the
-- shield-full repaint edge and (in randomFavorite mode) rolled a random outfit
-- baked into Tachy. The leaving-SkinSuit/Tachy edge now waits tachyEnterGuardMs
-- and re-reads the mesh: if it settled on a special (Tachy/SkinSuit) mesh it was
-- an ENTER, not a repaint, so no restore/re-roll fires.
--
-- 1.2.34 fixes the combat-exit stutter: the combat-end restore used to call
-- ReloadDataFromLastSave() on EVERY combat exit (each reload is a visible
-- hitch), even when no SkinSuit happened that fight. skinSuitPhaseSeen is now
-- reset to false at each battle start and the combat-end reload is skipped
-- unless a SkinSuit mesh was actually seen during the fight. Shield-full /
-- mesh-repaint / saved-body watchers still handle the real SkinSuit case in
-- real time, so nothing is lost.
--
-- Safety: reads player.bBattleState and CNS save-data map only. No shield
-- polling, no outfit object probing, no broad discovery. Restore fires only on
-- the observed true->false battle edge, so it should not run during Tachy/Fusion
-- (those stay inside combat, bBattleState stays true).

math.randomseed(os.time())
local ok_cfg, cfg = pcall(require, "config")
if not ok_cfg then cfg = {} end

local HELPER_BUILD = "1.2.42-shieldFullEdgeOnly (2026-07-26)"

local function cfgBool(v, d) if v == nil then return d end return v end
local ENABLE_HOTKEY = cfgBool(cfg.enableHotkey, true)
local ENABLE_DIAG_HOTKEY = cfgBool(cfg.enableDiagnosticHotkey, true)
local ENABLE_STATE_SNAPSHOT_HOTKEY = cfgBool(cfg.enableStateSnapshotHotkey, true)
local ENABLE_AUTO_RESTORE = cfgBool(cfg.enableAutoRestoreOnBattleEnd, true)
local RESTORE_DELAY_MS = tonumber(cfg.restoreDelayMs or 1500) or 1500
local RESTORE_COOLDOWN_MS = tonumber(cfg.restoreCooldownMs or 4000) or 4000
-- 1.2.41: skip a repeat forced reload of the SAME outfit within this window.
-- Multiple watchers force a reload for one shield-full; each ReloadDataFromLastSave
-- can make UE4SS write a crash .dmp on outfits where the engine call faults.
local FORCED_RELOAD_DEDUP_MS = tonumber(cfg.forcedReloadDedupMs or 300) or 300
local MAX_ARRAY_ITEMS = tonumber(cfg.snapshotMaxArrayItems or 20) or 20
local BODY_SLOT_KEY = cfg.bodySlotKey or "EVE-Body-"
local SKIN_SUIT_ALIAS = cfg.skinSuitAlias or "BS_102"
local ENABLE_BATTLE_STATE_WATCHER = cfgBool(cfg.enableBattleStateWatcher, true)
local BATTLE_WATCH_INTERVAL_MS = tonumber(cfg.battleStateWatchIntervalMs or 750) or 750
local STARTUP_WATCH_GRACE_MS = tonumber(cfg.startupWatchGraceMs or 8000) or 8000
local ENABLE_SAVED_BODY_SKINSUIT_WATCHER = cfgBool(cfg.enableSavedBodySkinSuitWatcher, false)
local SAVED_BODY_WATCH_INTERVAL_MS = tonumber(cfg.savedBodyWatchIntervalMs or 50) or 50
local SAVED_BODY_RESTORE_DELAY_MS = tonumber(cfg.savedBodyRestoreDelayMs or 0) or 0
local SAVED_BODY_FOLLOWUP_REPAINT_DELAY_MS = tonumber(cfg.savedBodyFollowupRepaintDelayMs or 120) or 120
local SAVED_BODY_BURST_REPAINT_DELAYS_MS = cfg.savedBodyBurstRepaintDelaysMs or { 16, 33, 66, 120, 250 }
local ENABLE_POST_RESTORE_RESET_PULSE = cfgBool(cfg.enablePostRestoreResetPulse, false)
local RESET_PULSE_START_DELAY_MS = tonumber(cfg.resetPulseStartDelayMs or 900) or 900
local RESET_PULSE_SKINSUIT_HOLD_MS = tonumber(cfg.resetPulseSkinSuitHoldMs or 120) or 120
local ENABLE_MESH_EVENT_HOOKS = cfgBool(cfg.enableMeshEventHooks, true)
local ENABLE_MESH_EVENT_RESTORE = cfgBool(cfg.enableMeshEventRestore, false)
local MESH_EVENT_LOG_THROTTLE_MS = tonumber(cfg.meshEventLogThrottleMs or 250) or 250
local MESH_EVENT_HOOK_NAMES = cfg.meshEventHookNames or {
    "/Script/SB.SBCharacter:NotifyBP_SetMesh",
    "/Script/SB.SBCharacter:ApplyMeshInfo",
}
local ENABLE_EFFECT_ALIAS_WATCHER = cfgBool(cfg.enableEffectAliasWatcher, false)
local EFFECT_ALIAS_WATCH_INTERVAL_MS = tonumber(cfg.effectAliasWatchIntervalMs or 100) or 100
local BREAK_EFFECT_ALIASES = cfg.breakEffectAliases or { "nanosuit_break" }
local EFFECT_ALIAS_RESTORE_ONLY_IN_BATTLE = cfgBool(cfg.effectAliasRestoreOnlyInBattle, false)
local ENABLE_EQUIPMENT_LIST_WATCHER = cfgBool(cfg.enableEquipmentListWatcher, true)
local EQUIPMENT_WATCH_INTERVAL_MS = tonumber(cfg.equipmentWatchIntervalMs or 100) or 100
local ENABLE_BODY_MESH_WATCHER = cfgBool(cfg.enableBodyMeshWatcher, true)
local BODY_MESH_WATCH_INTERVAL_MS = tonumber(cfg.bodyMeshWatchIntervalMs or 16) or 16
local SIGNAL_MARKER_SUBSTRING = cfg.signalMarkerSubstring or "BS_102"
local ENABLE_SIGNAL_EDGE_RESTORE = cfgBool(cfg.enableSignalEdgeRestore, true)
local SKIN_SUIT_MESH_NAME = cfg.skinSuitMeshName or "CH_P_EVE_InnerSuit"
local TACHY_MESH_NAMES = cfg.tachyMeshNames or { "CH_P_EVE_10" }
local FUSION_MESH_NAMES = cfg.fusionMeshNames or { "CH_P_EVE_Fusion", "CH_P_EVE_Fusion_NoAcc" }
-- 1.2.35: when the mesh LEAVES SkinSuit/Tachy the watcher waits this long and
-- re-reads the mesh before restoring. If it settled on a special (Tachy/SkinSuit)
-- mesh, this was a Tachy ENTER (SkinSuit -> intermediate -> Tachy), not a repaint,
-- so no restore/random re-roll fires. Genuine shield-full repaints settle on a
-- non-special default mesh and still restore, just delayed by this much.
local TACHY_ENTER_GUARD_MS = tonumber(cfg.tachyEnterGuardMs or 90) or 90
local USER_OUTFIT_CHANGE_TRUCE_MS = tonumber(cfg.userOutfitChangeTruceMs or 4000) or 4000
-- 1.2.20: red de seguridad "SkinSuit atascado". Si la malla sigue siendo SkinSuit
-- pero el juego YA NO tiene el equipamiento BS_102 puesto (escudo reparado), es un
-- estado inconsistente: el repintado se perdio. Pasa cuando el escudo se llena
-- durante un QTE/cinematica de jefe (reporte de latedozer), donde el watcher de
-- malla nunca ve el flanco. Tras GRACE ms en ese estado, se fuerza la restauracion.
local ENABLE_STUCK_SKINSUIT_GUARD = cfgBool(cfg.enableStuckSkinSuitGuard, false)
local STUCK_SKINSUIT_CHECK_INTERVAL_MS = tonumber(cfg.stuckSkinSuitCheckIntervalMs or 1000) or 1000
local STUCK_SKINSUIT_GRACE_MS = tonumber(cfg.stuckSkinSuitGraceMs or 2500) or 2500
local MODS_FOLDER = cfg.modsFolderPath or "..\\..\\Content\\Paks\\~mods"
local RANDOM_PICK_STICKY_MS = tonumber(cfg.randomPickStickyMs or 10000) or 10000
local RESTORE_MODE = cfg.restoreMode or "last"
local FAVORITE_OUTFITS = cfg.favoriteOutfits or {}
local ENABLE_UNEXPECTED_REPAINT_RESTORE = cfgBool(cfg.enableUnexpectedRepaintRestore, true)
local UNEXPECTED_REPAINT_VERIFY_MS = tonumber(cfg.unexpectedRepaintVerifyMs or 250) or 250
-- 30s variant: on top of everything the Random CNS helper already does, roll a
-- new random CNS body on a fixed interval, but ONLY while EVE is currently
-- wearing a normal (random) CNS body mesh. If the current mesh is SkinSuit,
-- Tachy, or any special/unknown mesh, the tick is skipped (no re-roll into or
-- out of those states).
local ENABLE_PERIODIC_RANDOM_CNS = cfgBool(cfg.enablePeriodicRandomCns, true)
local PERIODIC_RANDOM_CNS_INTERVAL_MS = tonumber(cfg.periodicRandomCnsIntervalMs or 30000) or 30000

local LOG_PATH
do
    local cands = {}
    local up = os.getenv("USERPROFILE")
    if up then cands[#cands + 1] = up .. "\\StellarSoulsOutfitRestore.log" end
    local tmp = os.getenv("TEMP")
    if tmp then cands[#cands + 1] = tmp .. "\\StellarSoulsOutfitRestore.log" end
    cands[#cands + 1] = "StellarSoulsOutfitRestore.log"
    for _, p in ipairs(cands) do
        local f = io.open(p, "a")
        if f then f:close(); LOG_PATH = p; break end
    end
end

local function log(msg)
    local line = "[StellarSoulsOutfitRestore] " .. tostring(msg)
    print(line .. "\n")
    if LOG_PATH then
        local f = io.open(LOG_PATH, "a")
        if f then f:write(os.date("%Y-%m-%d %H:%M:%S ") .. line .. "\n"); f:close() end
    end
end

local function nowMs() return os.clock() * 1000 end

local function isValid(o)
    if not o then return false end
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v
end

local function safeFullName(o)
    local out = nil
    pcall(function() if o and o:IsValid() then out = o:GetFullName() end end)
    return out or tostring(o)
end

local function safeClassName(o)
    local out = nil
    pcall(function()
        if o and o:IsValid() then
            local c = o:GetClass()
            if c and c:IsValid() then out = c:GetFullName() end
        end
    end)
    return out or "?"
end

local function valueToString(v)
    if v == nil then return "nil" end
    local tv = type(v)
    if tv == "string" or tv == "number" or tv == "boolean" then return tostring(v) end
    local converted = nil
    pcall(function() if v.ToString then converted = v:ToString() end end)
    if converted ~= nil then return tostring(converted) end
    pcall(function() if v.GetFName then converted = v:GetFName():ToString() end end)
    if converted ~= nil then return tostring(converted) end
    return tostring(v)
end

local function findCnsModActor()
    local insts = FindAllOf("ModActor_C")
    if not insts then return nil end
    for _, i in ipairs(insts) do
        if isValid(i) then return i end
    end
    return nil
end

local function findCnsSaveData()
    local sd
    pcall(function() sd = FindFirstOf("BP_CNS_SaveData_C") end)
    if isValid(sd) then return sd end
    return nil
end

-- 1.2.28: read the CNS favourites straight from BP_CNS_SaveData_C.FavouritesMap
-- (property name confirmed by unpacking DekCNS_P: FavouritesMap). Same safe
-- map-iteration pattern as cleanActiveOutfitMap. Entries whose key carries a
-- slot prefix (e.g. "EVE-Body-...") are normalized; if any entry mentions
-- "Body", the pick pool is restricted to those so hair/accessory favourites
-- are not written into the body slot.
local function readCnsFavourites()
    local saveData = findCnsSaveData()
    if not saveData then return nil end
    local list = {}
    pcall(function()
        local m = saveData.FavouritesMap
        if not m then return end
        m:ForEach(function(k, v)
            local key, val
            pcall(function()
                local kr = k.get and k:get() or k
                key = type(kr) == "string" and kr or valueToString(kr)
            end)
            pcall(function()
                local vr = v.get and v:get() or v
                val = type(vr) == "string" and vr or valueToString(vr)
            end)
            if key and key ~= "" then list[#list + 1] = { key = key, val = tostring(val) } end
        end)
    end)
    return list
end

-- 1.2.30: outfit-type registry built from the *.dekcns.json files that every
-- CNS outfit ships in ~mods. Each entry has "UniqueFitID" and "FitMeshType"
-- ("Body", "Hair", ...). FavouritesMap keys are UniqueFitIDs, so this gives
-- exact body filtering instead of the failed name-substring heuristic (1.2.29
-- log: pool collapsed to 1 because only one favourite had "Body" in its NAME).
local outfitTypeRegistry = nil

local function scanOutfitRegistry()
    local reg, files, entries = {}, 0, 0
    local ok = pcall(function()
        local p = io.popen('dir /b /s "' .. MODS_FOLDER .. '\\*.dekcns.json" 2>nul')
        if not p then return end
        for line in p:lines() do
            local fh = io.open(line, "r")
            if fh then
                files = files + 1
                local txt = fh:read("*a")
                fh:close()
                local pos = 1
                while true do
                    local s2, e2 = txt:find('"UniqueFitID"%s*:%s*"', pos)
                    if not s2 then break end
                    local idEnd = txt:find('"', e2 + 1)
                    if not idEnd then break end
                    local id = txt:sub(e2 + 1, idEnd - 1)
                    local nextU = txt:find('"UniqueFitID"', idEnd) or (#txt + 1)
                    local seg = txt:sub(idEnd, nextU)
                    local mt = seg:match('"FitMeshType"%s*:%s*"([^"]-)"')
                    if id and id ~= "" then
                        reg[id] = mt or "?"
                        entries = entries + 1
                    end
                    pos = idEnd
                end
            end
        end
        p:close()
    end)
    if not ok then return nil end
    log("Outfit registry: scanned " .. files .. " dekcns.json files, " .. entries .. " outfits.")
    return reg
end

local function getOutfitRegistry()
    if outfitTypeRegistry == nil then
        outfitTypeRegistry = scanOutfitRegistry() or {}
    end
    return outfitTypeRegistry
end

local function cnsFavouriteBodyPool()
    local favs = readCnsFavourites()
    if not favs or #favs == 0 then return nil, favs end
    local reg = getOutfitRegistry()
    local hasReg = next(reg) ~= nil
    local pool, unknown = {}, {}
    for _, e in ipairs(favs) do
        local alias = e.key
        local slotted = alias:match("^EVE%-Body%-(.+)$")
        if slotted then alias = slotted end
        if alias and alias ~= "" then
            local t = reg[alias]
            if t == "Body" then
                pool[#pool + 1] = alias
            elseif hasReg and t == nil then
                -- Not a CNS outfit (likely a vanilla suit): include, log it.
                pool[#pool + 1] = alias
                unknown[#unknown + 1] = alias
            elseif not hasReg then
                pool[#pool + 1] = alias
            end
        end
    end
    if #unknown > 0 then
        log("Favourites not in dekcns registry (assumed body/vanilla): " .. table.concat(unknown, ", "))
    end
    if #pool > 0 then return pool, favs end
    return nil, favs
end

-- Random-CNS variant: pool = EVERY installed CNS body outfit (FitMeshType
-- "Body" in the *.dekcns.json registry), NOT just the ones marked favourite.
-- Requires the registry scan to succeed; if it is empty (no dekcns.json found)
-- the caller falls back to favourites/manual list.
-- Forward declaration: cnsAllBodyPool is defined before the alias validator.
-- Without this, Lua resolves the reference below as a nil global.
local isGoodBodyOutfitAlias

local function cnsAllBodyPool()
    local reg = getOutfitRegistry()
    if not reg then return nil end
    local pool = {}
    for id, t in pairs(reg) do
        if t == "Body" and isGoodBodyOutfitAlias(id) then
            pool[#pool + 1] = id
        end
    end
    if #pool > 0 then return pool end
    return nil
end

local function findPlayer()
    local p
    pcall(function() p = FindFirstOf("CH_P_EVE_01_Blueprint_C") end)
    if not isValid(p) then pcall(function() p = FindFirstOf("SBCharacter") end) end
    return p
end

local lastRestoreAt = -1e9
-- 1.2.12: the last body alias the helper itself wrote to the CNS slot. The
-- saved-body watcher uses it to NOT open a user-change truce for the helper's own
-- restores (which would suppress the shield-full correction and leave default).
local lastHelperWrittenBody = nil
-- 1.2.41: last target actually reloaded + when, for the forced-reload dedup.
local lastReloadTarget = nil
local lastReloadTargetAt = -1e9
local lastKnownGoodBodyOutfit = nil
-- 1.2.31: sticky random pick shared by all triggers of one restore event.
local lastRandomPick = nil
local lastRandomPickAt = -1e9
-- 1.2.10: a re-roll is ARMED (cheap boolean, no CNS reads) the moment the SkinSuit
-- is entered, and CONSUMED by the first restore after it. The pick itself is still
-- computed inside reloadCns on the game thread (where reading the CNS maps / dekcns
-- registry is safe). Whichever restore trigger fires first (shield-full OR
-- combat-end) does the roll and applies it; the racing one reuses the sticky pick.
local rerollPending = false
-- 1.2.33: the random roll is only allowed after a real SkinSuit phase. Set by
-- the body-mesh watcher when the mesh enters the SkinSuit, cleared when a pick
-- is made. Without it, the routine combat-end restore re-rolled a random on
-- EVERY combat exit (even without a shield break) and again after the sticky
-- window expired.
local skinSuitPhaseSeen = false
-- 1.2.14: after the helper applies an outfit, adopt the resulting mesh as the new
-- good CNS mesh. Otherwise rememberedGoodBodyMesh stays the PREVIOUS outfit's mesh,
-- and when the game reverts to it the helper accepts it (random -> default/previous).
local adoptNextGoodMesh = false
-- 1.2.29: timestamp of the last user-driven CNS outfit change. While recent,
-- ALL automatic restores are suppressed so the helper never fights the CNS
-- wardrobe menu (the 13:53 log showed the helper reverting every user change).
local lastUserOutfitChangeAt = -1e9

local function isSkinSuitAlias(alias)
    return alias == SKIN_SUIT_ALIAS or alias == "BS_102" or alias == "SkinSuit_Common"
end

isGoodBodyOutfitAlias = function(alias)
    return alias ~= nil and alias ~= "" and not isSkinSuitAlias(alias)
end

local function getSavedBodyOutfit(saveData)
    if not saveData then return nil end
    local body = nil
    pcall(function()
        local m = saveData.LastActiveOutfitMap
        if not m then return end
        local val = m:Find(BODY_SLOT_KEY)
        if val ~= nil then
            local raw = val.get and val:get() or val
            body = type(raw) == "string" and raw or (raw and valueToString(raw))
        end
    end)
    return body
end

local function setSavedBodyOutfit(saveData, alias)
    local ok = false
    pcall(function()
        local m = saveData.LastActiveOutfitMap
        if not m then return end
        m:Add(BODY_SLOT_KEY, alias)
        ok = true
    end)
    return ok
end

local function cleanActiveOutfitMap(saveData)
    pcall(function()
        local m = saveData.LastActiveOutfitMap
        if not m then return end
        local keep, dropped = {}, {}
        m:ForEach(function(k, v)
            local key, val
            pcall(function()
                local kr = k.get and k:get() or k
                key = type(kr) == "string" and kr or valueToString(kr)
            end)
            pcall(function()
                local vr = v.get and v:get() or v
                val = type(vr) == "string" and vr or valueToString(vr)
            end)
            if (not key) or key == "" or (not val) or val == "" or key:find("%-Body%-Body$") then
                if key and key ~= "" then dropped[#dropped + 1] = key end
            else
                keep[key] = val
            end
        end)
        if #dropped == 0 then return end
        m:Empty()
        for key, val in pairs(keep) do pcall(function() m:Add(key, val) end) end
        log("Cleaned LastActiveOutfitMap: dropped {" .. table.concat(dropped, ", ") .. "}")
    end)
end

local function rememberCurrentSavedBody(source, allowReplace)
    local saveData = findCnsSaveData()
    if not saveData then return nil end
    cleanActiveOutfitMap(saveData)
    local savedBody = getSavedBodyOutfit(saveData)
    if isGoodBodyOutfitAlias(savedBody) and (allowReplace or lastKnownGoodBodyOutfit == nil) then
        if savedBody ~= lastKnownGoodBodyOutfit then
            lastKnownGoodBodyOutfit = savedBody
            log("Remembered CNS body outfit '" .. tostring(savedBody) .. "' (source=" .. tostring(source) .. ")")
        end
    elseif savedBody ~= nil then
        log("Ignored non-restorable body alias '" .. tostring(savedBody) .. "' (source=" .. tostring(source) .. ")")
    end
    return savedBody
end

-- Restore the CNS body. We explicitly write the remembered body alias before
-- ReloadDataFromLastSave, so ESE/BS_102 cannot make CNS reload a random/default
-- outfit from a polluted LastActiveOutfitMap.
-- `force` bypasses the cooldown (manual Alt+S); auto calls respect it so a noisy
-- run of battle-state changes cannot spam reloads.
-- 1.2.32: assigned later (needs the body-mesh watcher state). Returns true
-- when the visible mesh already matches the remembered good mesh AND the CNS
-- save already points at the target: nothing to fix, skip the reload (each
-- ReloadDataFromLastSave is a visible hitch).
local isBodyStateClean
local runPostRestoreResetPulse
local function reloadCns(source, force, alreadyOnGameThread)
    if not force and (nowMs() - lastUserOutfitChangeAt) < USER_OUTFIT_CHANGE_TRUCE_MS then
        log("Skipped CNS reload (user outfit-change truce), source=" .. tostring(source))
        return
    end
    if not force and (nowMs() - lastRestoreAt) < RESTORE_COOLDOWN_MS then
        log("Skipped CNS reload (cooldown), source=" .. tostring(source))
        return
    end
    lastRestoreAt = nowMs()
    local function doReloadOnGameThread()
        local cns = findCnsModActor()
        local saveData = findCnsSaveData()
        if not cns or not saveData then
            log("CNS ModActor_C or BP_CNS_SaveData_C not found (source=" .. tostring(source) .. ")")
            return
        end

        cleanActiveOutfitMap(saveData)
        local savedBody = getSavedBodyOutfit(saveData)
        if isGoodBodyOutfitAlias(savedBody) and lastKnownGoodBodyOutfit == nil then
            lastKnownGoodBodyOutfit = savedBody
            log("Remembered CNS body outfit '" .. tostring(savedBody) .. "' during restore fallback")
        end

        local targetBody = lastKnownGoodBodyOutfit
        -- 1.2.27: randomFavorite mode picks a random alias from the
        -- user-configured favorites list on every restore.
        -- Random CNS variant: "randomAny" picks from EVERY installed CNS body
        -- outfit, using the same one-pick-per-event / after-SkinSuit-only logic.
        if RESTORE_MODE == "randomFavorite" or RESTORE_MODE == "randomAny" then
            -- 1.2.10: roll a new outfit only when a SkinSuit armed a re-roll
            -- (rerollPending). The first restore after the SkinSuit rolls + applies;
            -- the racing trigger reuses the sticky pick. No SkinSuit since the last
            -- restore -> keep the current outfit. Pool reads run here on the game
            -- thread (safe), not in the mesh watcher.
            if rerollPending then
                local pool = nil
                if RESTORE_MODE == "randomAny" then
                    pool = cnsAllBodyPool()
                    -- registry empty (outfits without dekcns descriptors) -> fall
                    -- back to CNS favourites so randomAny still rolls something.
                    if not (pool and #pool > 0) then pool = cnsFavouriteBodyPool() end
                else
                    pool = cnsFavouriteBodyPool()
                end
                if not (pool and #pool > 0) and type(FAVORITE_OUTFITS) == "table" and #FAVORITE_OUTFITS > 0 then
                    pool = FAVORITE_OUTFITS
                end
                local clean = {}
                if pool then
                    for _, a in ipairs(pool) do
                        if isGoodBodyOutfitAlias(a) then clean[#clean + 1] = a end
                    end
                end
                if #clean > 0 then
                    targetBody = clean[math.random(#clean)]
                    lastRandomPick = targetBody
                    lastRandomPickAt = nowMs()
                    log("Restore mode " .. tostring(RESTORE_MODE) .. ": picked '" .. tostring(targetBody) .. "' from " .. #clean .. " candidates (source=" .. tostring(source) .. ")")
                else
                    log("Restore mode " .. tostring(RESTORE_MODE) .. ": pool empty/unreadable; using last outfit (source=" .. tostring(source) .. ")")
                end
                rerollPending = false
            elseif lastRandomPick ~= nil and (nowMs() - lastRandomPickAt) < RANDOM_PICK_STICKY_MS then
                targetBody = lastRandomPick
            end
        end
        if not isGoodBodyOutfitAlias(targetBody) then
            log("No remembered CNS body outfit to restore (savedBody=" .. tostring(savedBody) .. ", source=" .. tostring(source) .. ")")
            return
        end

        if source ~= "Alt+S" and isBodyStateClean and isBodyStateClean(targetBody, savedBody) then
            log("Skipped reload: body already clean (mesh=good, saved=target) (source=" .. tostring(source) .. ")")
            return
        end

        -- 1.2.12: mark this as the helper's own write so the saved-body watcher does
        -- not mistake it for a user CNS-menu change and open a truce (which would
        -- suppress the shield-full correction and leave Eve in the default outfit).
        lastHelperWrittenBody = targetBody
        if savedBody ~= targetBody then
            if setSavedBodyOutfit(saveData, targetBody) then
                log("Set CNS body slot to remembered outfit '" .. tostring(targetBody) .. "' before reload (was=" .. tostring(savedBody) .. ", source=" .. tostring(source) .. ")")
            else
                log("Failed to set remembered CNS body outfit '" .. tostring(targetBody) .. "' (source=" .. tostring(source) .. ")")
                return
            end
        elseif force then
            log("CNS body slot already equals remembered outfit '" .. tostring(targetBody) .. "' (source=" .. tostring(source) .. ")")
        elseif not force then
            log("CNS body already remembered outfit; still reloading for repaint (source=" .. tostring(source) .. ")")
        end

        -- 1.2.41: dedup. Several watchers + follow-up repaints call reloadCns for
        -- the SAME shield-full event; the outfit is already set after the first
        -- reload, so skip a repeat reload of the same target within the window.
        -- Cuts redundant ReloadDataFromLastSave calls (and their UE4SS crash .dmp
        -- spam on fault-prone outfits). Alt+S always forces a real repaint.
        if source ~= "Alt+S" and targetBody == lastReloadTarget
            and (nowMs() - lastReloadTargetAt) < FORCED_RELOAD_DEDUP_MS then
            log("Skipped duplicate reload of '" .. tostring(targetBody) .. "' within " ..
                FORCED_RELOAD_DEDUP_MS .. " ms (source=" .. tostring(source) .. ")")
            return
        end
        lastReloadTarget = targetBody
        lastReloadTargetAt = nowMs()

        local ok, err = pcall(function() cns:ReloadDataFromLastSave() end)
        if ok then
            log("Reloaded CNS remembered body outfit '" .. tostring(targetBody) .. "' (source=" .. tostring(source) .. ")")
            -- 1.2.40: consume the SkinSuit phase on any successful restore instead
            -- of resetting it at battle start. bBattleState flickers false->true at
            -- combat end; the battle-start reset wiped the SkinSuit memory before
            -- the restore ran, so random mode kept the same outfit ("sometimes it
            -- doesn't change"). Clearing here keeps the stutter gate working (a
            -- fight with no SkinSuit leaves the flag false) without the fragile
            -- battle-start reset.
            skinSuitPhaseSeen = false
            adoptNextGoodMesh = true
            if targetBody ~= lastKnownGoodBodyOutfit then
                lastKnownGoodBodyOutfit = targetBody
                log("Remembered body updated to restored pick '" .. tostring(targetBody) .. "'")
            end
            if runPostRestoreResetPulse then
                runPostRestoreResetPulse(targetBody, source)
            end
            return
        end
        log("ReloadDataFromLastSave failed: " .. tostring(err))
    end
    if alreadyOnGameThread then
        doReloadOnGameThread()
    else
        ExecuteInGameThread(doReloadOnGameThread)
    end
end

-- ---------------------------------------------------------------------------
-- Feature: auto-restore CNS when the player leaves combat.
-- ---------------------------------------------------------------------------

local lastBattle = nil
local battleSeenActive = false
local watcherStartedAt = nowMs()
local savedBodyRestorePending = false
local lastObservedSavedBody = nil
local resetPulseInProgress = false
local readPlayerBattleState

local function forEachBurstDelay(fn)
    if type(SAVED_BODY_BURST_REPAINT_DELAYS_MS) ~= "table" then return end
    for _, d in ipairs(SAVED_BODY_BURST_REPAINT_DELAYS_MS) do
        local n = tonumber(d)
        if n and n >= 0 then fn(n) end
    end
end

local function shouldRunPostRestoreResetPulse(source)
    if not ENABLE_POST_RESTORE_RESET_PULSE then return false end
    if resetPulseInProgress then return false end
    local s = tostring(source or "")
    if s:find("ResetPulse", 1, true) then return false end
    if s == "StartupDelay1500" or s == "StartupDelay6000" then return false end
    -- Keep the pulse outside combat. The reported failure happens after combat
    -- restore; running it mid-combat risks fighting Tachy/Fusion/cutscene states.
    local b = readPlayerBattleState()
    if b == true then return false end
    return true
end

runPostRestoreResetPulse = function(targetBody, source)
    if not isGoodBodyOutfitAlias(targetBody) then return end
    if not shouldRunPostRestoreResetPulse(source) then return end
    resetPulseInProgress = true
    log("Scheduling post-restore ESE reset pulse after source=" .. tostring(source) ..
        " target='" .. tostring(targetBody) .. "'")
    ExecuteWithDelay(RESET_PULSE_START_DELAY_MS, function()
        ExecuteInGameThread(function()
            if readPlayerBattleState() == true then
                log("Cancelled post-restore ESE reset pulse: player is back in combat.")
                resetPulseInProgress = false
                return
            end
            local cns = findCnsModActor()
            local saveData = findCnsSaveData()
            if not cns or not saveData then
                log("Cancelled post-restore ESE reset pulse: CNS actor/save data missing.")
                resetPulseInProgress = false
                return
            end
            log("Post-restore ESE reset pulse: temporary body='" .. tostring(SKIN_SUIT_ALIAS) .. "'")
            setSavedBodyOutfit(saveData, SKIN_SUIT_ALIAS)
            pcall(function() cns:ReloadDataFromLastSave() end)
            ExecuteWithDelay(RESET_PULSE_SKINSUIT_HOLD_MS, function()
                ExecuteInGameThread(function()
                    local cns2 = findCnsModActor()
                    local saveData2 = findCnsSaveData()
                    if cns2 and saveData2 then
                        log("Post-restore ESE reset pulse: restoring body='" .. tostring(targetBody) .. "'")
                        setSavedBodyOutfit(saveData2, targetBody)
                        pcall(function() cns2:ReloadDataFromLastSave() end)
                    else
                        log("Post-restore ESE reset pulse: final CNS actor/save data missing.")
                    end
                    ExecuteWithDelay(1000, function() resetPulseInProgress = false end)
                end)
            end)
        end)
    end)
end

readPlayerBattleState = function()
    local p = findPlayer()
    if not isValid(p) then return nil end
    local b = nil
    local ok = pcall(function() b = p.bBattleState end)
    if not ok then return nil end
    if type(b) == "boolean" then return b end
    return nil
end

-- Event_ChangeBattleState fires for any SBCharacter; we only care about the
-- player's true->false edge, so we read the player's own bBattleState each time.
local function onBattleStateEvent()
    local b = readPlayerBattleState()
    if b == nil then return end
    if lastBattle ~= true and b == true then
        battleSeenActive = true
        rememberCurrentSavedBody("BattleStart", true)
    end
    if (lastBattle == true or battleSeenActive) and b == false then
        battleSeenActive = false
        if not skinSuitPhaseSeen then
            log("Combat ended (Event); no SkinSuit this fight, skipping CNS reload (no stutter).")
            lastBattle = b
            return
        end
        log("Combat ended via Event_ChangeBattleState. Restoring CNS in " .. RESTORE_DELAY_MS .. " ms.")
        ExecuteWithDelay(RESTORE_DELAY_MS, function() reloadCns("BattleEndEvent", false) end)
    end
    lastBattle = b
end

local function checkBattleStateWatcher()
    local b = readPlayerBattleState()
    if b ~= nil then
        if lastBattle ~= true and b == true then
            battleSeenActive = true
            rememberCurrentSavedBody("BattleWatcherStart", true)
        end
        if (lastBattle == true or battleSeenActive) and b == false then
            if not skinSuitPhaseSeen then
                battleSeenActive = false
                log("Combat ended (watcher); no SkinSuit this fight, skipping CNS reload (no stutter).")
            elseif (nowMs() - watcherStartedAt) >= STARTUP_WATCH_GRACE_MS then
                local sd = findCnsSaveData()
                local savedBody = getSavedBodyOutfit(sd)
                log("Combat ended via battle-state watcher. savedBody='" .. tostring(savedBody) ..
                    "' remembered='" .. tostring(lastKnownGoodBodyOutfit) ..
                    "'. Restoring CNS in " .. RESTORE_DELAY_MS .. " ms.")
                battleSeenActive = false
                ExecuteWithDelay(RESTORE_DELAY_MS, function() reloadCns("BattleEndWatcher", false) end)
            else
                log("Ignored early false battle-state watcher edge during startup grace.")
            end
        end
        lastBattle = b
    end
    if ENABLE_BATTLE_STATE_WATCHER then
        ExecuteWithDelay(BATTLE_WATCH_INTERVAL_MS, function() pcall(checkBattleStateWatcher) end)
    end
end

-- ESE/CNS can leave the saved body slot as BS_102 around the shield restore edge.
-- This is the exact bad state reported by the user. We do not inspect mesh/outfit
-- UObjects and we do not read shield. We only watch the CNS save-data string that
-- Alt+S already fixes successfully.
local function checkSavedBodySkinSuitWatcher()
    local saveData = findCnsSaveData()
    local savedBody = getSavedBodyOutfit(saveData)

    if savedBody ~= lastObservedSavedBody then
        log("Saved body watcher observed body='" .. tostring(savedBody) .. "' remembered='" .. tostring(lastKnownGoodBodyOutfit) .. "'")
        lastObservedSavedBody = savedBody
        -- 1.2.29: a NEW good alias in the CNS body slot means the user (or CNS
        -- itself) changed outfit. Adopt it as the remembered outfit instantly
        -- and open a truce window so no auto-restore reverts it.
        if isGoodBodyOutfitAlias(savedBody) and savedBody ~= lastKnownGoodBodyOutfit then
            lastKnownGoodBodyOutfit = savedBody
            if savedBody ~= lastHelperWrittenBody then
                -- Genuine user change from the CNS menu: adopt + truce.
                lastUserOutfitChangeAt = nowMs()
                log("Adopted user CNS outfit change: remembered='" .. tostring(savedBody) .. "' (truce " ..
                    USER_OUTFIT_CHANGE_TRUCE_MS .. " ms)")
            else
                -- 1.2.12: the helper wrote this alias itself (its own restore/pick).
                -- Do NOT open a truce, or it suppresses the shield-full correction
                -- and Eve stays in the default outfit.
                log("Body slot now '" .. tostring(savedBody) .. "' = helper's own restore; no truce.")
            end
        end
    end

    if isSkinSuitAlias(savedBody) and isGoodBodyOutfitAlias(lastKnownGoodBodyOutfit) and not savedBodyRestorePending and not resetPulseInProgress then
        savedBodyRestorePending = true
        log("Saved body is SkinSuit/BS_102. Fast-restoring remembered CNS body '" .. tostring(lastKnownGoodBodyOutfit) .. "' in " .. SAVED_BODY_RESTORE_DELAY_MS .. " ms.")
        pcall(function() setSavedBodyOutfit(saveData, lastKnownGoodBodyOutfit) end)
        local doRestore = function()
            reloadCns("SavedBodySkinSuitWatcher", true)
            local hasBurst = false
            forEachBurstDelay(function(delayMs)
                hasBurst = true
                ExecuteWithDelay(delayMs, function()
                    reloadCns("SavedBodySkinSuitWatcherBurst" .. tostring(delayMs), true)
                end)
            end)
            if not hasBurst and SAVED_BODY_FOLLOWUP_REPAINT_DELAY_MS >= 0 then
                ExecuteWithDelay(SAVED_BODY_FOLLOWUP_REPAINT_DELAY_MS, function()
                    reloadCns("SavedBodySkinSuitWatcherFollowup", true)
                end)
            end
            ExecuteWithDelay(750, function() savedBodyRestorePending = false end)
        end
        if SAVED_BODY_RESTORE_DELAY_MS <= 0 then
            doRestore()
        else
            ExecuteWithDelay(SAVED_BODY_RESTORE_DELAY_MS, doRestore)
        end
    end

    if ENABLE_SAVED_BODY_SKINSUIT_WATCHER then
        ExecuteWithDelay(SAVED_BODY_WATCH_INTERVAL_MS, function() pcall(checkSavedBodySkinSuitWatcher) end)
    end
end

-- ---------------------------------------------------------------------------
-- Feature (1.2.20): narrow mesh-event hooks.
-- The table-side BS_102 repaint necessarily goes through a mesh update. Hooking
-- the two known SBCharacter mesh functions gives an event-driven trigger for the
-- same safe CNS restore path, instead of relying only on the polling watcher or
-- combat end. The callback reads ONLY the CNS save-data body string (same as
-- Alt+S); no mesh/outfit object introspection.
-- ---------------------------------------------------------------------------

local meshEventSeq = 0
local lastMeshLogAt = -1e9

local function onMeshEvent(hookName)
    meshEventSeq = meshEventSeq + 1
    local seq = meshEventSeq
    ExecuteInGameThread(function()
        local saveData = findCnsSaveData()
        local savedBody = getSavedBodyOutfit(saveData)
        local b = readPlayerBattleState()
        if (nowMs() - lastMeshLogAt) >= MESH_EVENT_LOG_THROTTLE_MS then
            lastMeshLogAt = nowMs()
            log("MESH #" .. seq .. " " .. hookName ..
                " battle=" .. tostring(b) ..
                " savedBody='" .. tostring(savedBody) ..
                "' remembered='" .. tostring(lastKnownGoodBodyOutfit) .. "'")
        end
        if ENABLE_MESH_EVENT_RESTORE
            and isSkinSuitAlias(savedBody)
            and isGoodBodyOutfitAlias(lastKnownGoodBodyOutfit)
            and not savedBodyRestorePending
            and not resetPulseInProgress then
            savedBodyRestorePending = true
            log("MESH #" .. seq .. " " .. hookName .. ": saved body is SkinSuit/BS_102. Immediate CNS restore.")
            pcall(function() setSavedBodyOutfit(saveData, lastKnownGoodBodyOutfit) end)
            reloadCns("MeshEvent:" .. hookName, true)
            ExecuteWithDelay(750, function() savedBodyRestorePending = false end)
        end
    end)
end

local function installMeshEventHooks()
    for _, hookName in ipairs(MESH_EVENT_HOOK_NAMES) do
        local shortName = hookName:match("[^:]+$") or hookName
        local ok, err = pcall(RegisterHook, hookName, function(Context, ...)
            pcall(onMeshEvent, shortName)
        end)
        if ok then
            log("Mesh-event hook registered: " .. hookName)
        else
            log("WARNING: could not register mesh-event hook " .. hookName .. ": " .. tostring(err))
        end
    end
end

-- ---------------------------------------------------------------------------
-- Feature (1.2.21): OverrideEffectAlias watcher = restore at shield 100%.
-- The 1.2.32G table dispels `nanosuit_break` exactly at shield-full (that is
-- what triggers the BS_102 repaint). SBCharacter.OverrideEffectAlias is a
-- readable ArrayProperty (the Alt+P snapshot already reads it without crashes).
-- Present -> absent edge of the break alias == shield-full moment (also fires
-- on Tachy exit from SkinSuit, which is equally desired). Uses the same access
-- pattern as dumpMaybeArray; no other objects touched.
-- ---------------------------------------------------------------------------

local effectAliasSeenInCombat = false
local lastEffectAliasListStr = nil
local effectAliasRestorePending = false

local function readOverrideEffectAliases()
    local p = findPlayer()
    if not isValid(p) then return nil end
    local list = nil
    pcall(function()
        local arr = p.OverrideEffectAlias
        if arr == nil then return end
        local count = nil
        pcall(function() if arr.GetArrayNum then count = arr:GetArrayNum() end end)
        pcall(function() if count == nil and arr.Num then count = arr:Num() end end)
        if count == nil then return end
        list = {}
        for idx = 0, math.min(count, MAX_ARRAY_ITEMS) - 1 do
            local item = nil
            pcall(function() if arr.GetArrayItem then item = arr:GetArrayItem(idx) end end)
            pcall(function() if item == nil and arr.Get then item = arr:Get(idx) end end)
            pcall(function() if item == nil then item = arr[idx] end end)
            if item ~= nil then list[#list + 1] = valueToString(item) end
        end
    end)
    return list
end

local function listHasBreakAlias(list)
    if not list then return nil end
    for _, v in ipairs(list) do
        for _, alias in ipairs(BREAK_EFFECT_ALIASES) do
            if v == alias then return true end
        end
    end
    return false
end

local function checkEffectAliasWatcher()
    local list = readOverrideEffectAliases()
    if list ~= nil then
        local listStr = table.concat(list, ", ")
        if listStr ~= lastEffectAliasListStr then
            log("EFFECT aliases changed: [" .. listStr .. "] (seenBreak=" .. tostring(effectAliasSeenInCombat) .. ")")
            lastEffectAliasListStr = listStr
        end
        local has = listHasBreakAlias(list)
        local b = readPlayerBattleState()
        if has == true then
            effectAliasSeenInCombat = true
        elseif has == false and effectAliasSeenInCombat then
            effectAliasSeenInCombat = false
            local inBattleOk = (not EFFECT_ALIAS_RESTORE_ONLY_IN_BATTLE) or (b == true)
            if inBattleOk and isGoodBodyOutfitAlias(lastKnownGoodBodyOutfit)
                and not effectAliasRestorePending and not resetPulseInProgress then
                effectAliasRestorePending = true
                log("SHIELD-FULL edge: break effect alias disappeared (battle=" .. tostring(b) ..
                    "). Restoring remembered CNS body '" .. tostring(lastKnownGoodBodyOutfit) .. "' NOW.")
                local saveData = findCnsSaveData()
                if saveData then pcall(function() setSavedBodyOutfit(saveData, lastKnownGoodBodyOutfit) end) end
                reloadCns("EffectAliasShieldFullEdge", true)
                forEachBurstDelay(function(delayMs)
                    ExecuteWithDelay(delayMs, function()
                        reloadCns("EffectAliasShieldFullBurst" .. tostring(delayMs), true)
                    end)
                end)
                ExecuteWithDelay(750, function() effectAliasRestorePending = false end)
            end
        end
    end
    if ENABLE_EFFECT_ALIAS_WATCHER then
        ExecuteWithDelay(EFFECT_ALIAS_WATCH_INTERVAL_MS, function() pcall(checkEffectAliasWatcher) end)
    end
end

-- ---------------------------------------------------------------------------
-- Feature (1.2.22): EquipmentDataList + GetBodyMeshName watchers.
-- 1.2.21 log proved OverrideEffectAlias stays empty and the mesh hooks never
-- fire. Two remaining player-side signals, both already touched safely by the
-- Alt+P snapshot: EquipmentDataList (nanosuit_break uses
-- EffectAction_AttachEquipment(BS_102); the attach/detach should appear here,
-- and the detach at shield-full is the exact wanted edge) and the plain getter
-- GetBodyMeshName(). Both are polled, changes are logged, and a generic
-- marker-substring edge (default "BS_102": present -> absent) triggers the
-- proven restore path immediately.
-- ---------------------------------------------------------------------------

local signalEdgeRestorePending = false

local function runSignalEdgeRestore(source)
    if not ENABLE_SIGNAL_EDGE_RESTORE then return end
    if (nowMs() - lastUserOutfitChangeAt) < USER_OUTFIT_CHANGE_TRUCE_MS then
        log("SIGNAL edge (" .. tostring(source) .. ") suppressed: user changed CNS outfit " ..
            math.floor(nowMs() - lastUserOutfitChangeAt) .. " ms ago (truce).")
        return
    end
    if signalEdgeRestorePending or resetPulseInProgress then return end
    if not isGoodBodyOutfitAlias(lastKnownGoodBodyOutfit) then
        log("SIGNAL edge (" .. tostring(source) .. ") but no remembered CNS body; skipping restore.")
        return
    end
    signalEdgeRestorePending = true
    log("SIGNAL edge (" .. tostring(source) .. "): marker disappeared. Restoring remembered CNS body '" ..
        tostring(lastKnownGoodBodyOutfit) .. "' NOW.")
    local saveData = findCnsSaveData()
    if saveData then pcall(function() setSavedBodyOutfit(saveData, lastKnownGoodBodyOutfit) end) end
    reloadCns("SignalEdge:" .. tostring(source), true)
    forEachBurstDelay(function(delayMs)
        ExecuteWithDelay(delayMs, function()
            reloadCns("SignalEdgeBurst" .. tostring(delayMs), true)
        end)
    end)
    ExecuteWithDelay(750, function() signalEdgeRestorePending = false end)
end

-- EquipmentDataList: serialize each item to a short string. Struct field names
-- are unknown, so several candidates are tried under pcall; valueToString is
-- the fallback. The item strings are only compared/logged, never kept as
-- object references.
local EQUIP_FIELD_CANDIDATES = { "Alias", "EquipmentAlias", "EquipAlias", "EffectAlias", "Name", "ID", "EquipmentID" }

local function equipItemToString(item)
    if item == nil then return "nil" end
    for _, f in ipairs(EQUIP_FIELD_CANDIDATES) do
        local v = nil
        local ok = pcall(function() v = item[f] end)
        if ok and v ~= nil then
            local s = valueToString(v)
            if s ~= nil and s ~= "" and s ~= "nil" and not s:find("^userdata") then
                return f .. "=" .. s
            end
        end
    end
    return valueToString(item)
end

local function readEquipmentListStr()
    local p = findPlayer()
    if not isValid(p) then return nil end
    local out = nil
    pcall(function()
        local arr = p.EquipmentDataList
        if arr == nil then return end
        local count = nil
        pcall(function() if arr.GetArrayNum then count = arr:GetArrayNum() end end)
        pcall(function() if count == nil and arr.Num then count = arr:Num() end end)
        if count == nil then return end
        local parts = { "n=" .. tostring(count) }
        for idx = 0, math.min(count, MAX_ARRAY_ITEMS) - 1 do
            local item = nil
            pcall(function() if arr.GetArrayItem then item = arr:GetArrayItem(idx) end end)
            pcall(function() if item == nil and arr.Get then item = arr:Get(idx) end end)
            pcall(function() if item == nil then item = arr[idx] end end)
            parts[#parts + 1] = equipItemToString(item)
        end
        out = table.concat(parts, " | ")
    end)
    return out
end

local lastEquipListStr = nil
local equipMarkerSeen = false

local function checkEquipmentListWatcher()
    local s = readEquipmentListStr()
    if s ~= nil then
        if s ~= lastEquipListStr then
            log("EQUIP list changed: [" .. s .. "] battle=" .. tostring(readPlayerBattleState()))
            lastEquipListStr = s
            local has = s:find(SIGNAL_MARKER_SUBSTRING, 1, true) ~= nil
            if has then
                equipMarkerSeen = true
            elseif equipMarkerSeen then
                equipMarkerSeen = false
                runSignalEdgeRestore("EquipmentDataList")
            end
        end
    end
    if ENABLE_EQUIPMENT_LIST_WATCHER then
        ExecuteWithDelay(EQUIPMENT_WATCH_INTERVAL_MS, function() pcall(checkEquipmentListWatcher) end)
    end
end

-- 1.2.23: mesh-transition logic based on the real names captured by the
-- 1.2.22 log:
--   CNS outfit body mesh   e.g. CH_P_EVE_21_TypeB (remembered as "good")
--   SkinSuit               CH_P_EVE_InnerSuit
--   Tachy Mode             CH_P_EVE_10
--   default repaint        CH_P_EVE_09 (appears at shield-full / Tachy exit)
-- Rule: when the mesh LEAVES SkinSuit/Tachy and lands on something that is not
-- the remembered good mesh (i.e. the default repaint), restore last CNS
-- immediately. Landing on the good mesh needs no action. 'None' flickers are
-- ignored. Restore keeps SkinSuit untouched because entering SkinSuit/Tachy
-- never triggers.
local lastBodyMeshName = nil
local rememberedGoodBodyMesh = nil
local bodyMeshRestorePending = false

local function isSpecialBodyMesh(name)
    if name == SKIN_SUIT_MESH_NAME then return true end
    for _, m in ipairs(TACHY_MESH_NAMES) do
        if name == m then return true end
    end
    for _, m in ipairs(FUSION_MESH_NAMES) do
        if name == m then return true end
    end
    return false
end

isBodyStateClean = function(targetBody, savedBody)
    return savedBody == targetBody
        and lastBodyMeshName ~= nil
        and rememberedGoodBodyMesh ~= nil
        and lastBodyMeshName == rememberedGoodBodyMesh
        and not isSpecialBodyMesh(lastBodyMeshName)
end

local function checkBodyMeshWatcher()
    local p = findPlayer()
    if isValid(p) then
        local name = nil
        pcall(function() name = valueToString(p:GetBodyMeshName()) end)
        if name ~= nil and name ~= "None" and name ~= "nil" and name ~= "" and name ~= lastBodyMeshName then
            local prev = lastBodyMeshName
            log("BODYMESH changed: '" .. tostring(prev) .. "' -> '" .. tostring(name) ..
                "' battle=" .. tostring(readPlayerBattleState()) ..
                " goodMesh='" .. tostring(rememberedGoodBodyMesh) .. "'")
            lastBodyMeshName = name
            -- 1.2.14: the helper just applied an outfit; adopt its resulting mesh as
            -- the good CNS mesh so a later revert to the OLD mesh is caught as wrong.
            if adoptNextGoodMesh and not isSpecialBodyMesh(name) then
                adoptNextGoodMesh = false
                rememberedGoodBodyMesh = name
                log("Adopted '" .. tostring(name) .. "' as good CNS mesh (helper applied an outfit).")
            end
            if isSpecialBodyMesh(name) then
                -- Entering SkinSuit or Tachy: never touch it.
                if name == SKIN_SUIT_MESH_NAME then
                    skinSuitPhaseSeen = true
                    -- 1.2.10: arm a re-roll for the next restore (cheap; the actual
                    -- pool read + pick runs in reloadCns on the game thread).
                    if RESTORE_MODE == "randomFavorite" or RESTORE_MODE == "randomAny" then
                        rerollPending = true
                    end
                end
            elseif prev ~= nil and isSpecialBodyMesh(prev) then
                -- Leaving SkinSuit/Tachy. Landing on the remembered good mesh is
                -- fine; anything else is the table-side default repaint.
                -- 1.2.18Z: with the NextBreakRearm table, shield-full can cleanly
                -- land on the previous good CNS mesh. In randomAny mode that is
                -- still the restore edge: consume the armed reroll now, after the
                -- SkinSuit equipment released the mesh, instead of treating the
                -- unchanged good mesh as "nothing to do".
                if prev == SKIN_SUIT_MESH_NAME and rerollPending
                    and (RESTORE_MODE == "randomFavorite" or RESTORE_MODE == "randomAny")
                    and ENABLE_SIGNAL_EDGE_RESTORE and not bodyMeshRestorePending then
                    bodyMeshRestorePending = true
                    log("BODYMESH clean SkinSuit exit -> immediate Random CNS restore (landed='" ..
                        tostring(name) .. "', good='" .. tostring(rememberedGoodBodyMesh) .. "').")
                    local cleanExitMesh = name
                    ExecuteInGameThread(function()
                        log("BODYMESH clean SkinSuit exit -> Random CNS NOW (no delay).")
                        reloadCns("CleanSkinSuitExitImmediate:" .. tostring(cleanExitMesh), true, true)
                    end)
                    ExecuteWithDelay(1000, function() bodyMeshRestorePending = false end)
                elseif rememberedGoodBodyMesh ~= nil and name ~= rememberedGoodBodyMesh
                    and ENABLE_SIGNAL_EDGE_RESTORE and not bodyMeshRestorePending then
                    bodyMeshRestorePending = true
                    local edgeMesh = name
                    log("BODYMESH repaint edge (verifying in " .. TACHY_ENTER_GUARD_MS .. " ms): '" ..
                        tostring(prev) .. "' -> '" .. tostring(edgeMesh) ..
                        "' != good '" .. tostring(rememberedGoodBodyMesh) .. "'.")
                    ExecuteWithDelay(TACHY_ENTER_GUARD_MS, function()
                        ExecuteInGameThread(function()
                            local pp = findPlayer()
                            local cur = nil
                            if isValid(pp) then pcall(function() cur = valueToString(pp:GetBodyMeshName()) end) end
                            if cur ~= nil and isSpecialBodyMesh(cur) then
                                -- Settled on Tachy/SkinSuit: this was an ENTER, not a
                                -- repaint. Do not restore (avoids random re-roll into Tachy).
                                log("BODYMESH edge cancelled: settled on special mesh '" .. tostring(cur) ..
                                    "' (Tachy/SkinSuit enter, no restore/re-roll).")
                                bodyMeshRestorePending = false
                                return
                            end
                            log("BODYMESH repaint edge confirmed -> '" .. tostring(cur or edgeMesh) ..
                                "'. Restoring last CNS NOW.")
                            runSignalEdgeRestore("BodyMeshRepaint:" .. tostring(cur or edgeMesh))
                            ExecuteWithDelay(1000, function() bodyMeshRestorePending = false end)
                        end)
                    end)
                else
                    rememberedGoodBodyMesh = name
                end
            elseif not bodyMeshRestorePending then
                if ENABLE_UNEXPECTED_REPAINT_RESTORE and prev ~= nil
                    and rememberedGoodBodyMesh ~= nil and prev == rememberedGoodBodyMesh
                    and name ~= rememberedGoodBodyMesh
                    and isGoodBodyOutfitAlias(lastKnownGoodBodyOutfit) then
                    -- 1.2.26: unexpected repaint away from the good mesh (seen in
                    -- the 23:31 log: a LATE engine repaint ~4 s after the
                    -- combat-end restore painted the default outfit again).
                    -- Verify after a short delay: if the CNS saved alias did NOT
                    -- change (so this is not the user picking a new outfit in
                    -- CNS) and the mesh is still wrong, restore.
                    bodyMeshRestorePending = true
                    local badMesh = name
                    log("BODYMESH unexpected leave-good: '" .. tostring(prev) .. "' -> '" .. tostring(badMesh) ..
                        "'. Verifying in " .. UNEXPECTED_REPAINT_VERIFY_MS .. " ms.")
                    ExecuteWithDelay(UNEXPECTED_REPAINT_VERIFY_MS, function()
                        ExecuteInGameThread(function()
                            local sd = findCnsSaveData()
                            local savedBody = getSavedBodyOutfit(sd)
                            local cur = nil
                            local pp = findPlayer()
                            if isValid(pp) then pcall(function() cur = valueToString(pp:GetBodyMeshName()) end) end
                            if cur == rememberedGoodBodyMesh then
                                log("Unexpected repaint self-recovered (mesh back to good); no restore.")
                            elseif savedBody == lastKnownGoodBodyOutfit then
                                log("UNEXPECTED repaint confirmed (savedBody unchanged '" .. tostring(savedBody) ..
                                    "', mesh='" .. tostring(cur) .. "'). Restoring last CNS NOW.")
                                runSignalEdgeRestore("UnexpectedRepaint:" .. tostring(badMesh))
                            else
                                log("Mesh change adopted as user outfit change (savedBody now '" .. tostring(savedBody) .. "').")
                                rememberedGoodBodyMesh = cur or badMesh
                            end
                            ExecuteWithDelay(1000, function() bodyMeshRestorePending = false end)
                        end)
                    end)
                else
                    -- Normal (non-SkinSuit, non-Tachy) mesh outside a restore window:
                    -- remember it as the current good CNS body mesh.
                    rememberedGoodBodyMesh = name
                end
            elseif bodyMeshRestorePending and rememberedGoodBodyMesh ~= nil and name == rememberedGoodBodyMesh then
                -- Restore landed on the good mesh; window can close early.
                bodyMeshRestorePending = false
            end
        end
    end
    if ENABLE_BODY_MESH_WATCHER then
        ExecuteWithDelay(BODY_MESH_WATCH_INTERVAL_MS, function() pcall(checkBodyMeshWatcher) end)
    end
end

-- ---------------------------------------------------------------------------
-- 1.2.20: guard de "SkinSuit atascado".
-- Los watchers de arriba disparan por FLANCO (la malla cambia, el marcador
-- BS_102 desaparece). Si ese flanco ocurre mientras el juego esta en un QTE /
-- cinematica de jefe, se pierde y EVE queda en Skin Suit al recuperar control
-- (reporte de latedozer, 25-Jul). Este chequeo periodico no mira flancos sino el
-- ESTADO: malla = SkinSuit pero el equipamiento BS_102 ya no esta puesto = el
-- juego ya no quiere Skin Suit y el repintado se perdio. Con el escudo realmente
-- roto el marcador SI esta presente, asi que no hay falsos positivos.
local stuckSkinSuitSince = nil
local function checkStuckSkinSuitGuard()
    local p = findPlayer()
    if isValid(p) then
        local mesh = nil
        pcall(function() mesh = valueToString(p:GetBodyMeshName()) end)
        local equip = readEquipmentListStr()
        local inSkinSuit = (mesh == SKIN_SUIT_MESH_NAME)
        local markerOn = (equip ~= nil) and (equip:find(SIGNAL_MARKER_SUBSTRING, 1, true) ~= nil)
        if inSkinSuit and equip ~= nil and not markerOn then
            if stuckSkinSuitSince == nil then
                stuckSkinSuitSince = nowMs()
                log("STUCK-GUARD: SkinSuit sin marcador " .. SIGNAL_MARKER_SUBSTRING ..
                    " -> arrancando cuenta (" .. STUCK_SKINSUIT_GRACE_MS .. " ms).")
            elseif (nowMs() - stuckSkinSuitSince) >= STUCK_SKINSUIT_GRACE_MS then
                stuckSkinSuitSince = nil
                if (nowMs() - lastUserOutfitChangeAt) >= USER_OUTFIT_CHANGE_TRUCE_MS then
                    log("STUCK-GUARD: SkinSuit atascado (QTE/cinematica?) -> restaurando CNS.")
                    if RESTORE_MODE == "randomFavorite" or RESTORE_MODE == "randomAny" then
                        rerollPending = true
                    end
                    ExecuteInGameThread(function()
                        pcall(function() reloadCns("StuckSkinSuitGuard", true, true) end)
                    end)
                end
            end
        else
            stuckSkinSuitSince = nil
        end
    end
    if ENABLE_STUCK_SKINSUIT_GUARD then
        ExecuteWithDelay(STUCK_SKINSUIT_CHECK_INTERVAL_MS, function() pcall(checkStuckSkinSuitGuard) end)
    end
end

-- ---------------------------------------------------------------------------
-- 30s variant: periodic random CNS re-roll.
-- Every PERIODIC_RANDOM_CNS_INTERVAL_MS, if EVE is currently in a normal
-- (random) CNS body mesh, arm a re-roll and run the same randomAny restore path
-- used after a SkinSuit phase. Gated strictly on the CURRENT body mesh: SkinSuit,
-- Tachy or any special/unknown mesh -> skip (never re-rolls in or out of those).
-- Respects the user outfit-change truce and skips while another restore is in
-- flight, so it never fights the CNS wardrobe menu or the shield-break restore.
-- ---------------------------------------------------------------------------
local function checkPeriodicRandomCns()
    ExecuteInGameThread(function()
        if not (RESTORE_MODE == "randomAny" or RESTORE_MODE == "randomFavorite") then
            return
        end
        local p = findPlayer()
        local curMesh = nil
        if isValid(p) then pcall(function() curMesh = valueToString(p:GetBodyMeshName()) end) end
        local meshOk = curMesh ~= nil and curMesh ~= "None" and curMesh ~= "nil" and curMesh ~= ""
            and not isSpecialBodyMesh(curMesh)
        if not meshOk then
            log("Periodic random CNS: skip, mesh='" .. tostring(curMesh) ..
                "' not a normal CNS body (SkinSuit/Tachy/unknown).")
            return
        end
        if savedBodyRestorePending or bodyMeshRestorePending or signalEdgeRestorePending or resetPulseInProgress then
            log("Periodic random CNS: skip, a restore is already in progress.")
            return
        end
        if (nowMs() - lastUserOutfitChangeAt) < USER_OUTFIT_CHANGE_TRUCE_MS then
            log("Periodic random CNS: skip, user outfit-change truce active.")
            return
        end
        log("Periodic random CNS: rolling a new random outfit (mesh='" .. tostring(curMesh) .. "').")
        rerollPending = true
        reloadCns("PeriodicRandomCns", false, true)
    end)
    if ENABLE_PERIODIC_RANDOM_CNS then
        ExecuteWithDelay(PERIODIC_RANDOM_CNS_INTERVAL_MS, function() pcall(checkPeriodicRandomCns) end)
    end
end

local function installAutoRestore()
    local ok, a = pcall(RegisterHook,
        "/Script/SB.SBCharacter:Event_ChangeBattleState",
        function(Context, ...) pcall(onBattleStateEvent) end)
    if ok and a then
        log("Auto-restore-on-combat-end hook registered (Event_ChangeBattleState).")
    else
        log("WARNING: could not register Event_ChangeBattleState hook: " .. tostring(a))
    end
end

-- ---------------------------------------------------------------------------
-- Diagnostics (kept; safe).
-- ---------------------------------------------------------------------------

local function dumpMaybeArray(label, arr)
    log("SNAPSHOT " .. label .. " = " .. valueToString(arr))
    if arr == nil then return end
    local count = nil
    pcall(function() if arr.GetArrayNum then count = arr:GetArrayNum() end end)
    pcall(function() if count == nil and arr.Num then count = arr:Num() end end)
    if count == nil then log("SNAPSHOT " .. label .. " count unavailable"); return end
    log("SNAPSHOT " .. label .. " count=" .. tostring(count))
    local n = math.min(count, MAX_ARRAY_ITEMS)
    for idx = 0, n - 1 do
        local item = nil
        pcall(function() if arr.GetArrayItem then item = arr:GetArrayItem(idx) end end)
        pcall(function() if item == nil and arr.Get then item = arr:Get(idx) end end)
        pcall(function() if item == nil then item = arr[idx] end end)
        log("SNAPSHOT " .. label .. "[" .. tostring(idx) .. "] = " .. valueToString(item))
    end
end

local function snapshot(source)
    ExecuteInGameThread(function()
        log("SNAPSHOT ==== build=" .. HELPER_BUILD .. " source=" .. tostring(source) .. " ====")
        local player = findPlayer()
        if not isValid(player) then
            log("SNAPSHOT player not found"); log("SNAPSHOT ==== end ===="); return
        end
        log("SNAPSHOT player=" .. safeFullName(player) .. " class=" .. safeClassName(player))
        for _, name in ipairs({ "bBattleState", "CharacterObjectState", "InteractionState" }) do
            local v = nil
            local ok = pcall(function() v = player[name] end)
            log("SNAPSHOT " .. name .. " = " .. (ok and valueToString(v) or "read failed"))
        end
        for _, name in ipairs({ "EquipmentDataList", "OverrideEffectAlias", "OverrideEffectID" }) do
            local v = nil
            local ok = pcall(function() v = player[name] end)
            if ok then dumpMaybeArray(name, v) else log("SNAPSHOT " .. name .. " read failed") end
        end
        log("SNAPSHOT ==== end ====")
    end)
end

local function diagnostic()
    ExecuteInGameThread(function()
        log("DIAG build=" .. HELPER_BUILD)
        local p = findPlayer()
        if isValid(p) then log("DIAG player = " .. safeFullName(p) .. " class=" .. safeClassName(p))
        else log("DIAG player not found") end
        log("DIAG ModActor_C found = " .. tostring(findCnsModActor() ~= nil))
        log("DIAG current player bBattleState = " .. tostring(readPlayerBattleState()))
        local sd = findCnsSaveData()
        log("DIAG BP_CNS_SaveData_C found = " .. tostring(isValid(sd)))
        log("DIAG saved CNS body = " .. tostring(getSavedBodyOutfit(sd)))
        log("DIAG remembered CNS body = " .. tostring(lastKnownGoodBodyOutfit))
        local favs = readCnsFavourites()
        if favs then
            log("DIAG CNS FavouritesMap entries = " .. tostring(#favs))
            for _, e in ipairs(favs) do
                local reg = getOutfitRegistry()
                log("DIAG favourite: key='" .. tostring(e.key) .. "' value='" .. tostring(e.val) ..
                    "' type='" .. tostring(reg[e.key] or "not-in-registry") .. "'")
            end
        else
            log("DIAG CNS FavouritesMap not readable (save data missing?)")
        end
    end)
end

-- ---------------------------------------------------------------------------
-- Hotkeys + startup.
-- ---------------------------------------------------------------------------

if ENABLE_HOTKEY then
    RegisterKeyBind(Key.S, { ModifierKey.ALT }, function() reloadCns("Alt+S", true) end)
end
if ENABLE_DIAG_HOTKEY then
    RegisterKeyBind(Key.X, { ModifierKey.ALT }, function() diagnostic() end)
end
if ENABLE_STATE_SNAPSHOT_HOTKEY then
    RegisterKeyBind(Key.P, { ModifierKey.ALT }, function() snapshot("Alt+P") end)
end

if ENABLE_AUTO_RESTORE then
    installAutoRestore()
end
if ENABLE_MESH_EVENT_HOOKS then
    installMeshEventHooks()
end
if ENABLE_BATTLE_STATE_WATCHER then
    ExecuteWithDelay(BATTLE_WATCH_INTERVAL_MS, function() pcall(checkBattleStateWatcher) end)
end
if ENABLE_SAVED_BODY_SKINSUIT_WATCHER then
    ExecuteWithDelay(SAVED_BODY_WATCH_INTERVAL_MS, function() pcall(checkSavedBodySkinSuitWatcher) end)
end
if ENABLE_EFFECT_ALIAS_WATCHER then
    ExecuteWithDelay(EFFECT_ALIAS_WATCH_INTERVAL_MS, function() pcall(checkEffectAliasWatcher) end)
end
if ENABLE_EQUIPMENT_LIST_WATCHER then
    ExecuteWithDelay(EQUIPMENT_WATCH_INTERVAL_MS, function() pcall(checkEquipmentListWatcher) end)
end
if ENABLE_BODY_MESH_WATCHER then
    ExecuteWithDelay(BODY_MESH_WATCH_INTERVAL_MS, function() pcall(checkBodyMeshWatcher) end)
end
if ENABLE_STUCK_SKINSUIT_GUARD then
    log("Stuck-SkinSuit guard ON (cada " .. STUCK_SKINSUIT_CHECK_INTERVAL_MS .. " ms, gracia " ..
        STUCK_SKINSUIT_GRACE_MS .. " ms)")
    ExecuteWithDelay(STUCK_SKINSUIT_CHECK_INTERVAL_MS, function() pcall(checkStuckSkinSuitGuard) end)
end
if ENABLE_PERIODIC_RANDOM_CNS then
    ExecuteWithDelay(PERIODIC_RANDOM_CNS_INTERVAL_MS, function() pcall(checkPeriodicRandomCns) end)
end

ExecuteWithDelay(1500, function() ExecuteInGameThread(function() rememberCurrentSavedBody("StartupDelay1500", true) end) end)
ExecuteWithDelay(6000, function() ExecuteInGameThread(function() rememberCurrentSavedBody("StartupDelay6000", true) end) end)

-- 1.2.36: PREWARM the outfit registry at startup, OFF the game thread. Random
-- modes used to run scanOutfitRegistry() (io.popen 'dir /s' over every
-- dekcns.json in ~mods) lazily on the game thread during the first shield-full
-- restore -> a 1-2 s freeze at the worst possible moment, plus a cmd.exe spawn
-- that can disturb other mods. Doing it once here (this callback is not on the
-- game thread) means the first random pick just reads the cached table.
if RESTORE_MODE == "randomAny" or RESTORE_MODE == "randomFavorite" then
    ExecuteWithDelay(4000, function()
        local ok, err = pcall(getOutfitRegistry)
        if ok then
            local n = 0
            if type(outfitTypeRegistry) == "table" then for _ in pairs(outfitTypeRegistry) do n = n + 1 end end
            log("Startup registry prewarm complete: " .. n .. " outfits cached (modsFolder='" .. tostring(MODS_FOLDER) .. "').")
            if n == 0 then
                log("WARNING: outfit registry is EMPTY. randomAny has no pool from ~mods; will fall back to CNS FavouritesMap / manual list. Check modsFolderPath in config.lua points at your ~mods folder.")
            end
        else
            log("Startup registry prewarm failed: " .. tostring(err))
        end
    end)
end

log("==================================================================")
log("LOADED build=" .. HELPER_BUILD)
log("Log file: " .. tostring(LOG_PATH))
log("Auto-restore CNS on combat end = " .. tostring(ENABLE_AUTO_RESTORE) ..
    " (delay=" .. RESTORE_DELAY_MS .. "ms, cooldown=" .. RESTORE_COOLDOWN_MS .. "ms)")
log("Battle-state watcher = " .. tostring(ENABLE_BATTLE_STATE_WATCHER) ..
    " (interval=" .. BATTLE_WATCH_INTERVAL_MS .. "ms, startupGrace=" .. STARTUP_WATCH_GRACE_MS .. "ms)")
log("Saved-body SkinSuit watcher = " .. tostring(ENABLE_SAVED_BODY_SKINSUIT_WATCHER) ..
    " (interval=" .. SAVED_BODY_WATCH_INTERVAL_MS .. "ms, restoreDelay=" .. SAVED_BODY_RESTORE_DELAY_MS .. "ms)")
if type(SAVED_BODY_BURST_REPAINT_DELAYS_MS) == "table" then
    local parts = {}
    for _, d in ipairs(SAVED_BODY_BURST_REPAINT_DELAYS_MS) do parts[#parts + 1] = tostring(d) end
    log("Saved-body burst repaint delays = {" .. table.concat(parts, ", ") .. "} ms")
end
log("Mesh-event hooks = " .. tostring(ENABLE_MESH_EVENT_HOOKS) ..
    " (restore=" .. tostring(ENABLE_MESH_EVENT_RESTORE) ..
    ", logThrottle=" .. MESH_EVENT_LOG_THROTTLE_MS .. "ms)")
log("Effect-alias shield-full watcher = " .. tostring(ENABLE_EFFECT_ALIAS_WATCHER) ..
    " (interval=" .. EFFECT_ALIAS_WATCH_INTERVAL_MS .. "ms, aliases={" .. table.concat(BREAK_EFFECT_ALIASES, ", ") .. "})")
log("EquipmentDataList watcher = " .. tostring(ENABLE_EQUIPMENT_LIST_WATCHER) ..
    " (interval=" .. EQUIPMENT_WATCH_INTERVAL_MS .. "ms)")
log("Body-mesh-name watcher = " .. tostring(ENABLE_BODY_MESH_WATCHER) ..
    " (interval=" .. BODY_MESH_WATCH_INTERVAL_MS .. "ms, skinSuitMesh='" .. tostring(SKIN_SUIT_MESH_NAME) ..
    "', tachyMeshes={" .. table.concat(TACHY_MESH_NAMES, ", ") ..
    "}, edgeRestore=" .. tostring(ENABLE_SIGNAL_EDGE_RESTORE) .. ")")
log("User outfit-change truce = " .. USER_OUTFIT_CHANGE_TRUCE_MS .. " ms")
log("Restore mode = " .. tostring(RESTORE_MODE) .. " (favorites=" .. tostring(#FAVORITE_OUTFITS) .. ")")
log("Periodic random CNS = " .. tostring(ENABLE_PERIODIC_RANDOM_CNS) ..
    " (interval=" .. PERIODIC_RANDOM_CNS_INTERVAL_MS .. "ms, only while in a normal CNS mesh)")
log("Alt+S = restore CNS now. Alt+X = diagnostic. Alt+P = state snapshot.")
log("No shield polling, no object probing. Restores remembered CNS body slot '" .. tostring(BODY_SLOT_KEY) .. "'.")
log("==================================================================")
