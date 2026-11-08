-- ProtoDroid script API 1. Minimap position and size mod.
-- Moves and scales the game's own HUD minimap (the FEHudMiniMap element) live from the
-- in-game settings panel: presets, fine pixel shifts, and a size slider that resizes the
-- minimap rectangle itself (everything drawn inside it follows the rect).
--
-- Calibration: the rect's byte offset inside the element instance is engine-build knowledge.
-- With rect_offset = 0 this mod auto-calibrates: it scans the instance for screen-space float
-- rectangles and picks the best minimap-shaped quad (square-ish, 120-640 px). The pick is
-- logged with its offset; if the wrong quad is picked, set rect_offset manually from the
-- calibration log (the scanner also dumps candidates) or via the in-game slider.

local SCREEN_W, SCREEN_H = 2400, 1080  -- the engine's render target; render_height = 1080 default
local MARGIN = 64                      -- preset distance from the screen edges, in pixels

local enabled     = pd.settings.get_boolean("enabled", true)
local preset      = pd.settings.get_string("position_preset", "Bottom left")
local shift_x     = pd.settings.get_number("horizontal_shift", 0.0)
local shift_y     = pd.settings.get_number("vertical_shift", 0.0)
local scale       = math.max(0.5, math.min(3.0, pd.settings.get_number("scale", 100.0) / 100.0))
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

local function scan(this)
    if scan_budget <= 0 then return end
    scan_budget = scan_budget - 1
    local best, best_score, best_off
    local candidates = {}
    for off = 0, 0x7f0, 4 do
        local x = pd.memory.read_f32(this + off)
        local y = pd.memory.read_f32(this + off + 4)
        local w = pd.memory.read_f32(this + off + 8)
        local h = pd.memory.read_f32(this + off + 12)
        if x and y and w and h and x == x and y == y and w == w and h == h
            and x >= 0 and y >= 0 and x < SCREEN_W and y < SCREEN_H
            and w >= 16 and w <= SCREEN_W and h >= 16 and h <= SCREEN_H then
            table.insert(candidates, string.format("  +%d: %.1f, %.1f, %.1f x %.1f", off, x, y, w, h))
            -- Minimap-shaped: roughly square, 120-640 px on a side. Score = squareness times
            -- closeness to the class's typical 320 px minimap; biggest square-ish quad wins.
            if w >= 120 and w <= 640 and h >= 120 and h <= 640 then
                local ratio = (w < h) and (w / h) or (h / w)
                local size = 1 - math.abs((w + h) / 2 - 320) / 320
                local score = ratio * math.max(0, size)
                if score > (best_score or 0) then best, best_score, best_off = { x = x, y = y, w = w, h = h }, score, off end
            end
        end
    end
    if best then
        rect_offset = best_off
        base_rect = best
        pd.log(string.format("minimap: auto-calibrated rect_offset=+%d (quad %.1f, %.1f, %.1f x %.1f); manual override via the rect_offset slider",
            best_off, best.x, best.y, best.w, best.h))
    else
        pd.log("minimap: no minimap-shaped quad found this pass; candidates below")
    end
    pd.log(string.format("minimap instance 0x%08x; screen-space float rects (offset, x, y, w, h):", this))
    for _, line in ipairs(candidates) do pd.log(line) end
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
    elseif preset == "Bottom left" then
        x, y = MARGIN, SCREEN_H - base_rect.h - MARGIN
    end
    x = math.max(0, math.min(SCREEN_W - rect.w, x + shift_x))
    y = math.max(0, math.min(SCREEN_H - rect.h, y + shift_y))
    pd.memory.write_f32(this + rect_offset, x)
    pd.memory.write_f32(this + rect_offset + 4, y)
    -- Size: resize the rect itself; HUD contents drawn inside the minimap follow it.
    pd.memory.write_f32(this + rect_offset + 8, base_rect.w * scale)
    pd.memory.write_f32(this + rect_offset + 12, base_rect.h * scale)
end

if enabled then
    local handle, hook_error = pd.engine.hook(symbol.address, function(ctx)
        -- __thiscall: the instance arrives in ecx, with the stack as the documented fallback.
        local this = ctx:get("ecx")
        if not this or this == 0 then this = ctx:arg(0) end
        if not this or this == 0 or not pd.memory.is_valid(this, 0x800) then return end
        if rect_offset <= 0 then
            scan(this)
            if rect_offset <= 0 then return end
        end
        apply(this)
    end)
    if not handle then error(hook_error or "minimap detour rejected") end
    pd.log(string.format("minimap: positioned/sized live (preset=%s scale=%.2f shifts %.0f/%.0f)",
        preset, scale, shift_x, shift_y))
end
