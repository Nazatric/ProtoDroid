-- ProtoDroid script API 1. Text source only; no host file, OS, package, or debug access.
local enabled = pd.settings.get_boolean("enabled", true)
local offset = pd.settings.get_number("fov_offset", 5.0)
local logging = pd.settings.get_string("frame_logging", "Quiet")
offset = math.max(-20.0, math.min(20.0, offset))

-- This is the same independently tested entry used by the native FOV example.
-- A symbol name alone never establishes a signature; use numeric RVAs for your own verified targets.
local symbol, symbol_error = pd.symbols.find("FUN_106dcfe0")
if not symbol then error(symbol_error) end

if enabled then
    local handle, hook_error = pd.engine.hook(symbol.address, function(ctx)
        local stack = ctx:get("esp") + 4
        if pd.memory.is_valid(stack, 4) then
            local fov = pd.memory.read_f32(stack)
            if fov and fov == fov then
                fov = math.max(30.0, math.min(150.0, fov + offset))
                pd.memory.write_f32(stack, fov)
            end
        end
        ctx:original() -- omit this only when intentionally replacing the original; returning also calls it automatically
    end)
    if not handle then error(hook_error) end
end

-- Other packages can adjust the offset in tenths of a degree, for example 75 means +7.5.
pd.on("protodroid.example.lua_set_offset", function(tenths)
    if tenths >= -200 and tenths <= 200 then offset = tenths / 10.0 end
end)

pd.on("protodroid.example.fov_ready", function(native_rva)
    pd.log("native FOV sibling is present at RVA", string.format("0x%08x", native_rva))
end)

local first_frame = true
pd.on("frame", function()
    if first_frame and logging == "First frame" then
        pd.log("Lua FOV example reached gameplay; enabled =", enabled, "offset =", offset)
    end
    first_frame = false
end)

pd.on("shutdown", function() pd.log("Lua FOV example ordinary shutdown") end)
pd.emit("protodroid.example.lua_fov_ready", symbol.rva, enabled and 1 or 0)
pd.log("Lua FOV example initialized with", pd.symbols.count(), "engine symbols")
