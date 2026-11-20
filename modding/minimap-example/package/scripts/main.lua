-- ProtoDroid script API 1. Minimap position example.
-- Demonstrates the full stack a gameplay mod uses: named engine symbols, a guarded detour,
-- live settings from the in-game panel (protodroid.settings_changed), typed memory access,
-- and the calibration discipline every layout-dependent mod must follow.
--
-- How it works: the FEHudMiniMap element's position lives in its instance memory. The rect's
-- byte offset inside the class is engine-build knowledge, not something a symbol name proves,
-- so this mod ships a bounded scanner: while rect_offset is 0 it logs every float rectangle in
-- screen space it can see inside the instance (three dumps per session). Pick the quad that
-- matches the minimap (roughly square, a few hundred pixels, anchored where the minimap draws)
-- and set the mod's rect_offset to its +offset — from then on the presets and the two sliders
-- move the real minimap, live, from the in-game panel.

local SCREEN_W, SCREEN_H = 2400, 1080  -- the engine's render target; render_height = 1080 default
local MARGIN = 64                      -- preset distance from the screen edges, in pixels

local enabled     = pd.settings.get_boolean("enabled", true)
local preset      = pd.settings.get_string("position_preset", "Bottom left")
local shift_x     = pd.settings.get_number("horizontal_shift", 0.0)
local shift_y     = pd.settings.get_number("vertical_shift", 0.0)
local rect_offset = math.floor(pd.settings.get_number("rect_offset", 0.0) + 0.5)

local symbol, symbol_error = pd.symbols.find("proto::FEHudMiniMap::vfunction1_for_FEHudElement")
if not symbol then error(symbol_error or "FEHudMiniMap element virtual not found in this engine build") end

local base_rect   -- the layout's own rect, anchored once so sliders stay relative to it
local scan_budget = 3

local function read_rect(this)
    local x = pd.memory.read_f32(this + rect_offset)
    local y = pd.memory.read_f32(this + rect_offset + 4)
    local w = pd.memory.read_f32(this + rect_offset + 8)
    local h = pd.memory.read_f32(this + rect_offset + 12)
    if not x or not y or not w or not h then return nil end
    if x ~= x or y ~= y or w ~= w or h ~= h then return nil end  -- NaN guard
    return { x = x, y = y, w = w, h = h }
end

local function apply(this)
    local rect = read_rect(this)
    if not rect then return end
    if not base_rect then base_rect = rect end
    local x, y = base_rect.x, base_rect.y
    if preset == "Bottom right" then
        x, y = SCREEN_W - base_rect.w - MARGIN, SCREEN_H - base_rect.h - MARGIN
    elseif preset == "Top left" then
        x, y = MARGIN, MARGIN
    elseif preset == "Top right" then
        x, y = SCREEN_W - base_rect.w - MARGIN, MARGIN
    end
    x = math.max(0, math.min(SCREEN_W - rect.w, x + shift_x))
    y = math.max(0, math.min(SCREEN_H - rect.h, y + shift_y))
    pd.memory.write_f32(this + rect_offset, x)
    pd.memory.write_f32(this + rect_offset + 4, y)
end

local function scan(this)
    if scan_budget <= 0 then return end
    scan_budget = scan_budget - 1
    pd.log(string.format("minimap instance 0x%08x; screen-space float rects (offset, x, y, w, h):", this))
    for off = 0, 0x7f0, 4 do
        local x = pd.memory.read_f32(this + off)
        local y = pd.memory.read_f32(this + off + 4)
        local w = pd.memory.read_f32(this + off + 8)
        local h = pd.memory.read_f32(this + off + 12)
        if x and y and w and h and x == x and y == y and w == w and h == h
            and x >= 0 and y >= 0 and x < SCREEN_W and y < SCREEN_H
            and w >= 16 and w <= SCREEN_W and h >= 16 and h <= SCREEN_H then
            pd.log(string.format("  +%d: %.1f, %.1f, %.1f x %.1f", off, x, y, w, h))
        end
    end
    pd.log("set this mod's rect_offset to the +offset of the minimap's quad to activate the presets")
end

if enabled then
    local handle, hook_error = pd.engine.hook(symbol.address, function(ctx)
        -- __thiscall: the instance arrives in ecx, with the stack as the documented fallback.
        local this = ctx:get("ecx")
        if not this or this == 0 then this = ctx:arg(0) end
        if this and this ~= 0 and pd.memory.is_valid(this, 0x800) then
            if rect_offset > 0 then apply(this) else scan(this) end
        end
        ctx:original()
    end)
    if not handle then error(hook_error) end
end

-- Live updates from the in-game panel: re-read values, re-anchor on offset changes, and
-- refresh the scan budget so a calibration run can be repeated without restarting the game.
pd.on("protodroid.settings_changed", function()
    preset      = pd.settings.get_string("position_preset", "Bottom left")
    shift_x     = pd.settings.get_number("horizontal_shift", 0.0)
    shift_y     = pd.settings.get_number("vertical_shift", 0.0)
    local new_offset = math.floor(pd.settings.get_number("rect_offset", 0.0) + 0.5)
    if new_offset ~= rect_offset then
        rect_offset = new_offset
        base_rect = nil
        scan_budget = 3
    end
end)

pd.on("shutdown", function() pd.log("minimap example shutdown") end)
pd.log("minimap example initialized; rect_offset =", rect_offset, "preset =", preset)
