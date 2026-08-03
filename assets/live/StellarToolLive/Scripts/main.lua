-- Stellar Tool - Live bridge (fase 1: FOV / velocidad / salto).
--
-- Codigo propio de Stellar Tool. Solo usa reflection de UE4SS: busca las
-- propiedades por nombre sobre el PlayerController vivo. No registra hooks ni
-- key binds, no escribe el save, no toca inventario ni progresion, y no usa
-- offsets ni firmas de memoria, asi que no queda atado a una version del juego.
--
-- Protocolo con la app (archivos de texto clave=valor, escritura atomica):
--   Mods/StellarToolLive/live_request.txt   <- lo escribe Stellar Tool
--   Mods/StellarToolLive/live_status.txt    -> lo escribe este script
--
-- Todo valor fuera de rango se recorta; si el request no se puede leer, el
-- bridge queda idle y no toca nada.

local POLL_MS = 250
local API = "stellar-tool-live-v1"

local FOV_MIN, FOV_MAX = 40.0, 170.0
local MULT_MIN, MULT_MAX = 0.1, 10.0

local thisDir = debug.getinfo(1, "S").source:match("@?(.*[\\/])") or ""
local modRoot = thisDir .. "../"
local REQUEST_FILE = modRoot .. "live_request.txt"
local STATUS_FILE = modRoot .. "live_status.txt"

-- UEHelpers viene con UE4SS. Si falta, caemos a FindFirstOf.
local UEHelpers = nil
do
    local ok, mod = pcall(require, "UEHelpers")
    if ok then UEHelpers = mod end
end

local beat = 0
local appliedSeq = -1
local lastPawn = nil
local baseSpeed, baseJump, baseFov = nil, nil, nil
local fovHolder, fovProp = nil, nil  -- donde pego el FOV (se descubre una vez)
local lastMessage = "loaded"

local function trim(value)
    return tostring(value or ""):match("^%s*(.-)%s*$") or ""
end

local function clamp(value, low, high)
    if value < low then return low end
    if value > high then return high end
    return value
end

local function publishAtomic(path, body)
    local temporary = path .. ".tmp"
    local f = io.open(temporary, "w")
    if not f then return false end
    f:write(body)
    f:close()
    os.remove(path)
    local ok = os.rename(temporary, path)
    if not ok then
        os.remove(temporary)
        return false
    end
    return true
end

local function readKeyValueFile(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local values = {}
    for line in f:lines() do
        local key, value = line:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
        if key then values[key] = value end
    end
    f:close()
    return values
end

-- Lectura/escritura defensiva: cualquier property que no exista en esta version
-- del juego tira desde el binding de UE4SS, asi que todo va con pcall.
local function getNumber(object, name)
    if not object then return nil end
    local ok, value = pcall(function() return object[name] end)
    if not ok then return nil end
    value = tonumber(value)
    if value == nil then return nil end
    return value
end

local function setNumber(object, name, value)
    if not object then return false end
    local ok = pcall(function() object[name] = value end)
    return ok and true or false
end

local function setBool(object, name, value)
    if not object then return false end
    local ok = pcall(function() object[name] = value end)
    return ok and true or false
end

local function isValid(object)
    if object == nil then return false end
    local ok, valid = pcall(function() return object:IsValid() end)
    return ok and valid == true
end

local function getPlayerController()
    if UEHelpers then
        local ok, pc = pcall(UEHelpers.GetPlayerController)
        if ok and isValid(pc) then return pc end
    end
    local ok, pc = pcall(FindFirstOf, "PlayerController")
    if ok and isValid(pc) then return pc end
    return nil
end

local function getProperty(object, name)
    if not object then return nil end
    local ok, value = pcall(function() return object[name] end)
    if not ok then return nil end
    if value == nil then return nil end
    if not isValid(value) then return nil end
    return value
end

-- El movimiento vive en el CharacterMovementComponent del pawn. El nombre de la
-- property cambia entre juegos, por eso probamos las dos formas habituales.
local function getMovement(pawn)
    return getProperty(pawn, "CharacterMovement")
        or getProperty(pawn, "MovementComponent")
end

-- Stellar Blade maneja el FOV con un override manual propio; si no aparece,
-- caemos a las properties estandar del PlayerCameraManager de Unreal. Se
-- resuelve una sola vez por pawn y se recuerda cual funciono.
local FOV_CANDIDATES = {
    "ManualCameraFov",
    "CameraFovOverride",
    "CameraFov",
    "CurrentFov",
    "DefaultFOV",
    "FieldOfView",
}

local function resolveFovTarget(controller, pawn)
    local holders = {}
    local cameraManager = getProperty(controller, "PlayerCameraManager")
    if cameraManager then holders[#holders + 1] = cameraManager end
    if pawn then holders[#holders + 1] = pawn end
    if controller then holders[#holders + 1] = controller end

    for _, holder in ipairs(holders) do
        for _, name in ipairs(FOV_CANDIDATES) do
            local current = getNumber(holder, name)
            -- Un FOV plausible confirma que la property es la correcta y no
            -- otra cosa que casualmente se llama parecido.
            if current and current > 1.0 and current < 200.0 then
                return holder, name, current
            end
        end
    end
    return nil, nil, nil
end

local function captureBases(controller, pawn)
    local movement = getMovement(pawn)
    baseSpeed = getNumber(movement, "MaxWalkSpeed")
    baseJump = getNumber(movement, "JumpZVelocity")
    fovHolder, fovProp, baseFov = resolveFovTarget(controller, pawn)
end

local function writeStatus(ready, request)
    beat = beat + 1
    local body = table.concat({
        "api=" .. API,
        "beat=" .. tostring(beat),
        "ready=" .. (ready and "1" or "0"),
        "seq=" .. tostring(appliedSeq),
        "fov_prop=" .. tostring(fovProp or ""),
        "fov_base=" .. string.format("%.2f", baseFov or 0),
        "fov_live=" .. string.format("%.2f", (fovHolder and fovProp and getNumber(fovHolder, fovProp)) or 0),
        "speed_base=" .. string.format("%.2f", baseSpeed or 0),
        "speed_live=" .. string.format("%.2f", (request and request.speed) or 1),
        "jump_base=" .. string.format("%.2f", baseJump or 0),
        "jump_live=" .. string.format("%.2f", (request and request.jump) or 1),
        "message=" .. trim(lastMessage):gsub("[\r\n]", " "),
        "",
    }, "\n")
    publishAtomic(STATUS_FILE, body)
end

local function readRequest()
    local raw = readKeyValueFile(REQUEST_FILE)
    if not raw then return nil end
    local seq = tonumber(raw.seq)
    if not seq then return nil end
    -- fov <= 0 significa "no tocar el FOV" (deja el del juego).
    local fov = tonumber(raw.fov) or 0
    if fov > 0 then fov = clamp(fov, FOV_MIN, FOV_MAX) else fov = 0 end
    return {
        seq = seq,
        fov = fov,
        speed = clamp(tonumber(raw.speed) or 1.0, MULT_MIN, MULT_MAX),
        jump = clamp(tonumber(raw.jump) or 1.0, MULT_MIN, MULT_MAX),
    }
end

local function applyRequest(request)
    local controller = getPlayerController()
    if not controller then
        lastMessage = "waiting_for_player_controller"
        writeStatus(false, request)
        return
    end
    local pawn = getProperty(controller, "Pawn") or getProperty(controller, "AcknowledgedPawn")
    if not pawn then
        lastMessage = "waiting_for_pawn"
        writeStatus(false, request)
        return
    end

    -- Al cambiar de pawn (carga de save, cinematica, cambio de nivel) los
    -- valores base son otros: hay que recapturarlos antes de multiplicar, si no
    -- el multiplicador se aplicaria sobre un valor ya modificado.
    local pawnId = tostring(pawn)
    if pawnId ~= lastPawn then
        lastPawn = pawnId
        captureBases(controller, pawn)
    end

    local movement = getMovement(pawn)
    local applied = {}

    if baseSpeed and movement then
        if setNumber(movement, "MaxWalkSpeed", baseSpeed * request.speed) then
            applied[#applied + 1] = "speed"
        end
    end
    if baseJump and movement then
        if setNumber(movement, "JumpZVelocity", baseJump * request.jump) then
            applied[#applied + 1] = "jump"
        end
    end

    if fovHolder and fovProp then
        local target = request.fov > 0 and request.fov or baseFov
        if target then
            -- El override manual necesita su flag prendido; si el juego no lo
            -- tiene, el set directo alcanza igual.
            setBool(fovHolder, "bManualCameraFovMode", request.fov > 0)
            if setNumber(fovHolder, fovProp, target) then
                applied[#applied + 1] = "fov"
            end
        end
    end

    appliedSeq = request.seq
    if #applied == 0 then
        lastMessage = "no_writable_properties"
        writeStatus(false, request)
    else
        lastMessage = "applied:" .. table.concat(applied, ",")
        writeStatus(true, request)
    end
end

writeStatus(false, nil)

-- El juego reescribe velocidad/salto/FOV solo (equipar, cinematicas, cambio de
-- estado), asi que reaplicamos cada tick en vez de una sola vez por request.
LoopAsync(POLL_MS, function()
    local ok, err = pcall(function()
        local request = readRequest()
        if not request then
            lastMessage = "idle"
            writeStatus(false, nil)
            return
        end
        ExecuteInGameThread(function()
            local innerOk, innerErr = pcall(applyRequest, request)
            if not innerOk then
                lastMessage = "apply_error:" .. tostring(innerErr)
                writeStatus(false, request)
            end
        end)
    end)
    if not ok then
        lastMessage = "loop_error:" .. tostring(err)
        pcall(writeStatus, false, nil)
    end
    return false
end)

print("[StellarToolLive] bridge cargado (" .. API .. "); sin hooks, sin key binds.\n")
