#include "mod_platform.h"

#include <limits.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unistd.h>

extern "C" {
#include "../../third_party/lua-5.4.8/lauxlib.h"
#include "../../third_party/lua-5.4.8/lua.h"
#include "../../third_party/lua-5.4.8/lualib.h"
}

#include "guest.h"
#include "mod_settings.h"
#include "mod_symbols.h"

namespace native {
namespace {

constexpr size_t kLuaMemoryLimit = 16 * 1024 * 1024;
constexpr size_t kLuaSourceLimit = 1024 * 1024;
constexpr size_t kLuaTotalSourceLimit = 2 * 1024 * 1024;
constexpr int kInstructionLimit = 1000000;
constexpr int kInstructionTick = 10000;
constexpr size_t kMaxScripts = 64;
constexpr size_t kMaxCallbacks = 64;
constexpr size_t kMaxQueuedEvents = 64;
constexpr size_t kMaxEventArguments = 8;
constexpr size_t kMaxCallArguments = 16;
constexpr size_t kMaxByteAccess = 4096;
constexpr size_t kMaxScriptHooks = 64;

struct ScriptState;

struct ScriptHook {
    ScriptState* owner = nullptr;
    uint32_t target = 0;
    ModHookFn original = nullptr;
    int callback_ref = LUA_NOREF;
    bool enabled = false;
};

struct ActiveHook {
    ScriptHook* hook = nullptr;
    Ctx* context = nullptr;
    bool handled = false;
    bool original_invoked = false;
    uint64_t generation = 0;
};

struct ScriptState {
    const ModEntry* mod = nullptr;
    lua_State* lua = nullptr;
    ModSettings settings;
    std::unordered_map<std::string, std::vector<int>> callbacks;
    std::vector<ScriptHook*> hooks;
    std::string script_directory;
    size_t allocated = 0;
    size_t source_bytes = 0;
    int modules_ref = LUA_NOREF;
    int instructions = 0;
    int call_depth = 0;
    bool loading = false;
    bool in_callback = false;
    bool active = false;
    bool failed = false;
};

struct ScriptEvent {
    std::string name;
    std::string source;
    std::vector<uint32_t> arguments;
};

std::vector<std::unique_ptr<ScriptState>> g_states;
std::vector<std::unique_ptr<ScriptHook>> g_hook_storage;
std::array<ScriptHook*, kMaxScriptHooks> g_hooks{};
std::deque<ScriptEvent> g_events;
ActiveHook* g_active_hook = nullptr;
uint64_t g_hook_generation = 0;
bool g_shutdown = false;
bool g_dispatching_events = false;

ScriptState* State(lua_State* lua) {
    return static_cast<ScriptState*>(lua_touserdata(lua, lua_upvalueindex(1)));
}

bool GuestRange(uint32_t address, uint64_t size) {
    return size == 0 || (address != 0 && uint64_t(address) + size <= 0x100000000ULL);
}

// Unlike a numeric range check, these kernel-mediated self-transfers reject unmapped and PROT_NONE guest pages.
// Android 26+ and the Linux host both provide process_vm_readv/writev. Native plugins retain their direct trusted ABI.
bool GuestRead(uint32_t address, void* output, size_t size) {
    if ((!output && size) || !GuestRange(address, size)) return false;
    if (!size) return true;
    iovec local{output, size};
    iovec remote{Ptr<void>(address), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
}

bool GuestWrite(uint32_t address, const void* input, size_t size) {
    if ((!input && size) || !GuestRange(address, size)) return false;
    if (!size) return true;
    iovec local{const_cast<void*>(input), size};
    iovec remote{Ptr<void>(address), size};
    return process_vm_writev(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
}

void* LuaAllocator(void* opaque, void* pointer, size_t old_size, size_t new_size) {
    auto* state = static_cast<ScriptState*>(opaque);
    const size_t old = pointer ? old_size : 0;
    const size_t retained = old <= state->allocated ? state->allocated - old : 0;
    if (new_size > kLuaMemoryLimit || retained > kLuaMemoryLimit - new_size) return nullptr;
    void* result = nullptr;
    if (new_size == 0) {
        free(pointer);
    } else {
        result = realloc(pointer, new_size);
        if (!result) return nullptr;
    }
    state->allocated = retained + new_size;
    return result;
}

void InstructionHook(lua_State* lua, lua_Debug*) {
    auto* state = *static_cast<ScriptState**>(lua_getextraspace(lua));
    if (!state || state->failed) return;
    if (state->instructions <= kInstructionTick) {
        state->instructions = 0;
        luaL_error(lua, "script instruction budget exhausted");
        return;
    }
    state->instructions -= kInstructionTick;
}

void DisableState(ScriptState* state, const std::string& error) {
    if (!state || state->failed) return;
    state->failed = true;
    state->active = false;
    for (ScriptHook* hook : state->hooks) if (hook) hook->enabled = false;
    std::string message = "Lua script disabled: " + error;
    Log("mods[%s] %s", state->mod->id.c_str(), message.c_str());
    ModWriteLaunchError(*state->mod, message);
}

bool CallProtected(ScriptState* state, int arguments, int results, std::string* error) {
    lua_State* lua = state->lua;
    const bool outer = state->call_depth == 0;
    if (outer) {
        state->instructions = kInstructionLimit;
        lua_sethook(lua, InstructionHook, LUA_MASKCOUNT, kInstructionTick);
    }
    ++state->call_depth;
    const bool previous_callback = state->in_callback;
    state->in_callback = true;
    int status = lua_pcall(lua, arguments, results, 0);
    state->in_callback = previous_callback;
    --state->call_depth;
    if (outer) lua_sethook(lua, nullptr, 0, 0);
    if (status == LUA_OK) return true;
    const char* message = lua_tostring(lua, -1);
    if (error) *error = message ? message : (status == LUA_ERRMEM ? "Lua memory limit exhausted" : "unknown Lua error");
    lua_pop(lua, 1);
    return false;
}

bool InvokeProtected(ScriptState* state, int arguments, int results = 0) {
    std::string error;
    if (CallProtected(state, arguments, results, &error)) return true;
    DisableState(state, error);
    return false;
}

bool ValidEventName(const std::string& name) {
    if (name.empty() || name.size() > 64 || name == "frame" || name == "shutdown") return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.' && c != ':' && c != '/') {
            return false;
        }
    }
    return true;
}

bool QueueEvent(const std::string& name, const std::string& source, const uint32_t* arguments, size_t count) {
    if (!ValidEventName(name) || count > kMaxEventArguments || (count && !arguments) || g_shutdown || g_events.size() >= 64) {
        return false;
    }
    ScriptEvent event;
    event.name = name;
    event.source = source;
    if (count) event.arguments.assign(arguments, arguments + count);
    g_events.push_back(std::move(event));
    return true;
}

int LuaLogImpl(lua_State* lua, bool warning) {
    auto* state = State(lua);
    std::string message;
    const int count = std::min(lua_gettop(lua), 8);
    for (int i = 1; i <= count; ++i) {
        size_t length = 0;
        const char* part = luaL_tolstring(lua, i, &length);
        if (length > 4096) {
            lua_pop(lua, 1);
            return luaL_error(lua, "log value is too long");
        }
        if (i > 1) message += '\t';
        message.append(part, length);
        lua_pop(lua, 1);
    }
    if (message.size() > 8192) return luaL_error(lua, "log message is too long");
    Log("mods[%s] script%s: %s", state->mod->id.c_str(), warning ? " warning" : "", message.c_str());
    return 0;
}

int LuaLog(lua_State* lua) { return LuaLogImpl(lua, false); }
int LuaWarn(lua_State* lua) { return LuaLogImpl(lua, true); }

const std::string* Setting(ScriptState* state, const char* key) {
    return state ? ModSettingsValue(state->settings, key) : nullptr;
}

bool EqualsIgnoreCase(const std::string& value, const char* expected) {
    const size_t size = strlen(expected);
    if (value.size() != size) return false;
    for (size_t i = 0; i < size; ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(expected[i]))) return false;
    }
    return true;
}

int LuaSettingBoolean(lua_State* lua) {
    auto* state = State(lua);
    const char* key = luaL_checkstring(lua, 1);
    const bool fallback = lua_toboolean(lua, 2);
    const std::string* value = Setting(state, key);
    if (!value) {
        lua_pushboolean(lua, fallback);
        return 1;
    }
    if (*value == "1" || EqualsIgnoreCase(*value, "true") || EqualsIgnoreCase(*value, "on") ||
        EqualsIgnoreCase(*value, "yes")) lua_pushboolean(lua, 1);
    else if (*value == "0" || EqualsIgnoreCase(*value, "false") || EqualsIgnoreCase(*value, "off") ||
             EqualsIgnoreCase(*value, "no")) lua_pushboolean(lua, 0);
    else lua_pushboolean(lua, fallback);
    return 1;
}

int LuaSettingNumber(lua_State* lua) {
    auto* state = State(lua);
    const char* key = luaL_checkstring(lua, 1);
    const double fallback = luaL_checknumber(lua, 2);
    const std::string* value = Setting(state, key);
    double result = fallback;
    if (value && !value->empty()) {
        char* end = nullptr;
        errno = 0;
        const double parsed = std::strtod(value->c_str(), &end);
        if (!errno && end && !*end && std::isfinite(parsed)) result = parsed;
    }
    lua_pushnumber(lua, result);
    return 1;
}

int LuaSettingString(lua_State* lua) {
    auto* state = State(lua);
    const char* key = luaL_checkstring(lua, 1);
    const char* fallback = luaL_optstring(lua, 2, "");
    const std::string* value = Setting(state, key);
    lua_pushstring(lua, value ? value->c_str() : fallback);
    return 1;
}

uint32_t CheckAddress(lua_State* lua, int index) {
    const lua_Integer value = luaL_checkinteger(lua, index);
    if (value <= 0 || static_cast<uint64_t>(value) > 0xffffffffULL) {
        luaL_argerror(lua, index, "guest address must be in 0x00000001..0xffffffff");
    }
    return static_cast<uint32_t>(value);
}

uint32_t CheckU32(lua_State* lua, int index) {
    const lua_Integer value = luaL_checkinteger(lua, index);
    if (value < 0 || static_cast<uint64_t>(value) > 0xffffffffULL) {
        luaL_argerror(lua, index, "expected an unsigned 32-bit integer");
    }
    return static_cast<uint32_t>(value);
}

int MemoryFailure(lua_State* lua, const char* message) {
    lua_pushnil(lua);
    lua_pushstring(lua, message);
    return 2;
}

template <typename T>
int ReadNumber(lua_State* lua) {
    const uint32_t address = CheckAddress(lua, 1);
    if (!GuestRange(address, sizeof(T))) return MemoryFailure(lua, "guest memory range is invalid");
    T value{};
    if (!GuestRead(address, &value, sizeof(value))) return MemoryFailure(lua, "guest memory is unmapped or inaccessible");
    if constexpr (std::is_floating_point<T>::value) lua_pushnumber(lua, static_cast<lua_Number>(value));
    else lua_pushinteger(lua, static_cast<lua_Integer>(value));
    return 1;
}

int LuaReadU32(lua_State* lua) {
    const uint32_t address = CheckAddress(lua, 1);
    if (!GuestRange(address, sizeof(uint32_t))) return MemoryFailure(lua, "guest memory range is invalid");
    uint32_t value = 0;
    if (!GuestRead(address, &value, sizeof(value))) return MemoryFailure(lua, "guest memory is unmapped or inaccessible");
    lua_pushinteger(lua, static_cast<lua_Integer>(value));
    return 1;
}

template <typename T>
int WriteNumber(lua_State* lua) {
    const uint32_t address = CheckAddress(lua, 1);
    const lua_Integer input = luaL_checkinteger(lua, 2);
    const auto minimum = static_cast<lua_Integer>(std::numeric_limits<T>::lowest());
    const auto maximum = static_cast<lua_Integer>(std::numeric_limits<T>::max());
    if (input < minimum || input > maximum) return luaL_argerror(lua, 2, "integer is outside the requested type");
    if (!GuestRange(address, sizeof(T))) return MemoryFailure(lua, "guest memory range is invalid");
    const T value = static_cast<T>(input);
    if (!GuestWrite(address, &value, sizeof(value))) return MemoryFailure(lua, "guest memory is unmapped or inaccessible");
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaWriteU32(lua_State* lua) {
    const uint32_t address = CheckAddress(lua, 1);
    const uint32_t value = CheckU32(lua, 2);
    if (!GuestRange(address, sizeof(value))) return MemoryFailure(lua, "guest memory range is invalid");
    if (!GuestWrite(address, &value, sizeof(value))) return MemoryFailure(lua, "guest memory is unmapped or inaccessible");
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaWriteFloat(lua_State* lua) {
    const uint32_t address = CheckAddress(lua, 1);
    const lua_Number input = luaL_checknumber(lua, 2);
    if (input != input || input < -3.402823466e38 || input > 3.402823466e38) {
        return luaL_argerror(lua, 2, "expected a finite 32-bit float");
    }
    if (!GuestRange(address, sizeof(float))) return MemoryFailure(lua, "guest memory range is invalid");
    const float value = static_cast<float>(input);
    if (!GuestWrite(address, &value, sizeof(value))) return MemoryFailure(lua, "guest memory is unmapped or inaccessible");
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaMemoryValid(lua_State* lua) {
    const lua_Integer address = luaL_checkinteger(lua, 1);
    const lua_Integer size = luaL_optinteger(lua, 2, 1);
    const bool valid = address >= 0 && static_cast<uint64_t>(address) <= 0xffffffffULL &&
        size >= 0 && size <= 0x100000000LL &&
        GuestRange(static_cast<uint32_t>(address), static_cast<uint64_t>(size));
    lua_pushboolean(lua, valid);
    return 1;
}

int LuaReadBytes(lua_State* lua) {
    const uint32_t address = CheckAddress(lua, 1);
    const lua_Integer size = luaL_checkinteger(lua, 2);
    if (size <= 0 || size > static_cast<lua_Integer>(kMaxByteAccess)) {
        return luaL_argerror(lua, 2, "byte count must be in 1..4096");
    }
    if (!GuestRange(address, static_cast<uint64_t>(size))) return MemoryFailure(lua, "guest memory range is invalid");
    std::array<char, kMaxByteAccess> bytes{};
    if (!GuestRead(address, bytes.data(), static_cast<size_t>(size))) {
        return MemoryFailure(lua, "guest memory is unmapped or inaccessible");
    }
    lua_pushlstring(lua, bytes.data(), static_cast<size_t>(size));
    return 1;
}

int LuaWriteBytes(lua_State* lua) {
    const uint32_t address = CheckAddress(lua, 1);
    size_t size = 0;
    const char* bytes = luaL_checklstring(lua, 2, &size);
    if (size == 0 || size > kMaxByteAccess) return luaL_argerror(lua, 2, "byte string must contain 1..4096 bytes");
    if (!GuestRange(address, size)) return MemoryFailure(lua, "guest memory range is invalid");
    if (!GuestWrite(address, bytes, size)) return MemoryFailure(lua, "guest memory is unmapped or inaccessible");
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaReadString(lua_State* lua) {
    const uint32_t address = CheckAddress(lua, 1);
    const lua_Integer maximum = luaL_optinteger(lua, 2, 256);
    if (maximum <= 0 || maximum > static_cast<lua_Integer>(kMaxByteAccess)) {
        return luaL_argerror(lua, 2, "maximum string size must be in 1..4096");
    }
    std::string value;
    value.reserve(static_cast<size_t>(maximum));
    for (lua_Integer i = 0; i < maximum; ++i) {
        const uint64_t current = uint64_t(address) + static_cast<uint64_t>(i);
        if (current > 0xffffffffULL || !GuestRange(static_cast<uint32_t>(current), 1)) {
            return MemoryFailure(lua, "unterminated or invalid guest string");
        }
        char c = 0;
        if (!GuestRead(static_cast<uint32_t>(current), &c, 1)) {
            return MemoryFailure(lua, "guest string is unmapped or inaccessible");
        }
        if (!c) {
            lua_pushlstring(lua, value.data(), value.size());
            return 1;
        }
        value.push_back(c);
    }
    return MemoryFailure(lua, "guest string is not NUL-terminated within the limit");
}

int LuaWriteString(lua_State* lua) {
    const uint32_t address = CheckAddress(lua, 1);
    size_t size = 0;
    const char* value = luaL_checklstring(lua, 2, &size);
    if (size == 0 || size >= kMaxByteAccess || memchr(value, 0, size)) {
        return luaL_argerror(lua, 2, "string must contain 1..4095 non-NUL bytes");
    }
    if (!GuestRange(address, size + 1)) return MemoryFailure(lua, "guest memory range is invalid");
    std::array<char, kMaxByteAccess> terminated{};
    memcpy(terminated.data(), value, size);
    if (!GuestWrite(address, terminated.data(), size + 1)) {
        return MemoryFailure(lua, "guest memory is unmapped or inaccessible");
    }
    lua_pushboolean(lua, 1);
    return 1;
}

void PushSymbol(lua_State* lua, const ModSymbolMatch& match) {
    lua_createtable(lua, 0, 5);
    lua_pushlstring(lua, match.name.data(), match.name.size());
    lua_setfield(lua, -2, "name");
    lua_pushinteger(lua, match.rva);
    lua_setfield(lua, -2, "rva");
    lua_pushinteger(lua, match.address);
    lua_setfield(lua, -2, "address");
    lua_pushinteger(lua, match.ordinal);
    lua_setfield(lua, -2, "ordinal");
    lua_pushinteger(lua, match.matches);
    lua_setfield(lua, -2, "matches");
}

int LuaSymbolFind(lua_State* lua) {
    const char* name = luaL_checkstring(lua, 1);
    const lua_Integer ordinal = luaL_optinteger(lua, 2, 1);
    if (ordinal <= 0 || ordinal > 65535) return luaL_argerror(lua, 2, "ordinal must be in 1..65535");
    ModSymbolMatch match;
    if (!ModSymbolResolve(name, static_cast<uint32_t>(ordinal), &match)) return MemoryFailure(lua, "engine symbol or ordinal not found");
    PushSymbol(lua, match);
    return 1;
}

int LuaSymbolSearch(lua_State* lua) {
    const char* query = luaL_checkstring(lua, 1);
    const lua_Integer limit = luaL_optinteger(lua, 2, 32);
    if (limit <= 0 || limit > 64) return luaL_argerror(lua, 2, "search limit must be in 1..64");
    const auto matches = ModSymbolSearch(query, static_cast<size_t>(limit));
    lua_createtable(lua, static_cast<int>(matches.size()), 0);
    int index = 1;
    for (const auto& match : matches) {
        PushSymbol(lua, match);
        lua_rawseti(lua, -2, index++);
    }
    return 1;
}

int LuaSymbolCount(lua_State* lua) {
    lua_pushinteger(lua, static_cast<lua_Integer>(ModSymbolCount()));
    return 1;
}

bool TargetRva(lua_State* lua, int index, uint32_t* rva) {
    if (lua_type(lua, index) == LUA_TSTRING) {
        const char* name = lua_tostring(lua, index);
        ModSymbolMatch match;
        if (!ModSymbolResolve(name, 1, &match)) return false;
        *rva = match.rva;
        return true;
    }
    if (lua_type(lua, index) != LUA_TNUMBER || !lua_isinteger(lua, index)) return false;
    const lua_Integer input = lua_tointeger(lua, index);
    if (input < 0) return false;
    const uint64_t value = static_cast<uint64_t>(input);
    if (value < kEngineReserve) {
        *rva = static_cast<uint32_t>(value);
        return true;
    }
    if (value >= kEngineBase && value < uint64_t(kEngineBase) + kEngineReserve) {
        *rva = static_cast<uint32_t>(value - kEngineBase);
        return true;
    }
    return false;
}

int LuaEngineCall(lua_State* lua) {
    uint32_t rva = 0;
    if (!TargetRva(lua, 1, &rva)) return MemoryFailure(lua, "target must be an exact symbol, engine RVA, or rebased engine address");
    const uint32_t target = kEngineBase + rva;
    if (!RecompLookup(target)) return MemoryFailure(lua, "target has no recompiled function entry");
    std::array<uint32_t, kMaxCallArguments> arguments{};
    size_t count = 0;
    if (!lua_isnoneornil(lua, 2)) {
        luaL_checktype(lua, 2, LUA_TTABLE);
        count = lua_rawlen(lua, 2);
        if (count > kMaxCallArguments) return MemoryFailure(lua, "at most 16 call arguments are allowed");
        for (size_t i = 0; i < count; ++i) {
            lua_geti(lua, 2, static_cast<lua_Integer>(i + 1));
            arguments[i] = CheckU32(lua, -1);
            lua_pop(lua, 1);
        }
    }
    const uint32_t ecx = lua_isnoneornil(lua, 3) ? 0 : CheckU32(lua, 3);
    const uint32_t result = GuestCallArray(g_active_hook ? g_active_hook->context : nullptr,
                                           target, arguments.data(), count, ecx);
    lua_pushinteger(lua, result);
    return 1;
}

int LuaOn(lua_State* lua) {
    auto* state = State(lua);
    if (!state->loading) return MemoryFailure(lua, "event callbacks must be registered by the top-level script");
    const std::string name = luaL_checkstring(lua, 1);
    if (name != "frame" && name != "shutdown" && !ValidEventName(name)) {
        return MemoryFailure(lua, "invalid lifecycle or custom event name");
    }
    luaL_checktype(lua, 2, LUA_TFUNCTION);
    size_t total = 0;
    for (const auto& entry : state->callbacks) total += entry.second.size();
    if (total >= kMaxCallbacks) return MemoryFailure(lua, "per-mod callback limit reached");
    lua_pushvalue(lua, 2);
    const int reference = luaL_ref(lua, LUA_REGISTRYINDEX);
    state->callbacks[name].push_back(reference);
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaEmit(lua_State* lua) {
    auto* state = State(lua);
    const std::string name = luaL_checkstring(lua, 1);
    const int top = lua_gettop(lua);
    const size_t count = top > 1 ? static_cast<size_t>(top - 1) : 0;
    std::array<uint32_t, kMaxEventArguments> arguments{};
    if (count > kMaxEventArguments) return MemoryFailure(lua, "at most eight integer event arguments are allowed");
    for (size_t i = 0; i < count; ++i) arguments[i] = CheckU32(lua, static_cast<int>(i + 2));
    if (!QueueEvent(name, state->mod->id, arguments.data(), count)) {
        return MemoryFailure(lua, "invalid event, shutdown, or per-frame event queue limit");
    }
    lua_pushboolean(lua, 1);
    return 1;
}

bool SafeModuleName(const std::string& name) {
    if (name.empty() || name.size() > 128 || name.front() == '/' || name.back() == '/') return false;
    size_t begin = 0;
    while (begin < name.size()) {
        const size_t end = name.find('/', begin);
        const std::string part = name.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (part.empty() || part == "." || part == "..") return false;
        for (char c : part) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}

bool RealWithin(const std::string& root, const std::string& candidate) {
    char root_real[PATH_MAX], candidate_real[PATH_MAX];
    if (!realpath(root.c_str(), root_real) || !realpath(candidate.c_str(), candidate_real)) return false;
    const std::string r(root_real), c(candidate_real);
    return c == r || (c.size() > r.size() && c.compare(0, r.size(), r) == 0 && c[r.size()] == '/');
}

bool ReadChunk(ScriptState* state, const std::string& path, std::string* source, std::string* error) {
    struct stat st {};
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        st.st_size > static_cast<off_t>(kLuaSourceLimit) || !RealWithin(state->mod->root, path)) {
        *error = "Lua source is missing, non-regular, empty, too large, or outside the package";
        return false;
    }
    if (source->size() || state->source_bytes + static_cast<size_t>(st.st_size) > kLuaTotalSourceLimit) {
        *error = "per-mod Lua source budget exhausted";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        *error = "Lua source could not be read";
        return false;
    }
    source->resize(static_cast<size_t>(st.st_size));
    input.read(source->data(), st.st_size);
    if (!input || source->find('\0') != std::string::npos || (!source->empty() && (*source)[0] == '\x1b')) {
        source->clear();
        *error = "Lua source must be bounded text without NUL or bytecode signatures";
        return false;
    }
    state->source_bytes += source->size();
    return true;
}

int LuaRequire(lua_State* lua) {
    auto* state = State(lua);
    if (!state->loading) return luaL_error(lua, "pd.require is available only while the entrypoint is loading");
    const std::string name = luaL_checkstring(lua, 1);
    if (!SafeModuleName(name)) return luaL_error(lua, "module name must be a safe relative path without an extension");
    lua_rawgeti(lua, LUA_REGISTRYINDEX, state->modules_ref);  // stack: module cache
    lua_getfield(lua, 2, name.c_str());
    if (!lua_isnil(lua, -1)) return 1;
    lua_pop(lua, 1);
    const std::string path = state->script_directory + "/" + name + ".lua";
    std::string source, error;
    if (!ReadChunk(state, path, &source, &error)) return luaL_error(lua, "%s", error.c_str());
    const std::string chunk = "@" + path;
    if (luaL_loadbufferx(lua, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK) {
        return lua_error(lua);
    }
    if (!CallProtected(state, 0, 1, &error)) return luaL_error(lua, "%s", error.c_str());
    if (lua_isnil(lua, -1)) {
        lua_pop(lua, 1);
        lua_pushboolean(lua, 1);
    }
    lua_pushvalue(lua, -1);
    lua_setfield(lua, 2, name.c_str());
    return 1;
}

uint32_t* HookRegister(Ctx* context, const char* name) {
    if (!context || !name) return nullptr;
    if (!strcmp(name, "eax")) return &context->eax;
    if (!strcmp(name, "ecx")) return &context->ecx;
    if (!strcmp(name, "edx")) return &context->edx;
    if (!strcmp(name, "ebx")) return &context->ebx;
    if (!strcmp(name, "esp")) return &context->esp;
    if (!strcmp(name, "ebp")) return &context->ebp;
    if (!strcmp(name, "esi")) return &context->esi;
    if (!strcmp(name, "edi")) return &context->edi;
    if (!strcmp(name, "ret_target")) return &context->ret_target;
    if (!strcmp(name, "fs_base")) return &context->fs_base;
    return nullptr;
}

ActiveHook* HookContext(lua_State* lua) {
    lua_getfield(lua, 1, "_pd");
    auto* active = static_cast<ActiveHook*>(lua_touserdata(lua, -1));
    lua_pop(lua, 1);
    lua_getfield(lua, 1, "_generation");
    const lua_Integer generation = lua_tointeger(lua, -1);
    lua_pop(lua, 1);
    if (!active || active != g_active_hook || !active->context || !active->hook ||
        generation != static_cast<lua_Integer>(active->generation)) {
        luaL_error(lua, "hook context is no longer active");
        return nullptr;
    }
    return active;
}

int LuaHookGet(lua_State* lua) {
    auto* active = HookContext(lua);
    const char* name = luaL_checkstring(lua, 2);
    uint32_t* value = HookRegister(active->context, name);
    if (!value) return luaL_argerror(lua, 2, "unknown integer register");
    lua_pushinteger(lua, *value);
    return 1;
}

int LuaHookSet(lua_State* lua) {
    auto* active = HookContext(lua);
    const char* name = luaL_checkstring(lua, 2);
    uint32_t* target = HookRegister(active->context, name);
    if (!target) return luaL_argerror(lua, 2, "unknown integer register");
    *target = CheckU32(lua, 3);
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaHookArgument(lua_State* lua) {
    auto* active = HookContext(lua);
    const lua_Integer index = luaL_checkinteger(lua, 2);
    if (index < 0 || index > 63) return luaL_argerror(lua, 2, "argument index must be in 0..63");
    const uint64_t wide = uint64_t(active->context->esp) + 4 + static_cast<uint64_t>(index) * 4;
    if (wide > 0xffffffffULL) return MemoryFailure(lua, "guest stack argument is invalid");
    const uint32_t address = static_cast<uint32_t>(wide);
    if (!GuestRange(address, 4)) return MemoryFailure(lua, "guest stack argument is invalid");
    uint32_t value = 0;
    if (!GuestRead(address, &value, sizeof(value))) return MemoryFailure(lua, "guest stack argument is inaccessible");
    lua_pushinteger(lua, value);
    return 1;
}

int LuaHookSetArgument(lua_State* lua) {
    auto* active = HookContext(lua);
    const lua_Integer index = luaL_checkinteger(lua, 2);
    if (index < 0 || index > 63) return luaL_argerror(lua, 2, "argument index must be in 0..63");
    const uint32_t value = CheckU32(lua, 3);
    const uint64_t wide = uint64_t(active->context->esp) + 4 + static_cast<uint64_t>(index) * 4;
    if (wide > 0xffffffffULL) return MemoryFailure(lua, "guest stack argument is invalid");
    const uint32_t address = static_cast<uint32_t>(wide);
    if (!GuestRange(address, 4)) return MemoryFailure(lua, "guest stack argument is invalid");
    if (!GuestWrite(address, &value, sizeof(value))) return MemoryFailure(lua, "guest stack argument is inaccessible");
    lua_pushboolean(lua, 1);
    return 1;
}

int LuaHookOriginal(lua_State* lua) {
    auto* active = HookContext(lua);
    if (active->handled) return luaL_error(lua, "original was already called or the hook was skipped");
    active->handled = true;
    active->original_invoked = true;
    active->hook->original(active->context);
    lua_pushinteger(lua, active->context->eax);
    lua_pushinteger(lua, active->context->edx);
    return 2;
}

int LuaHookSkip(lua_State* lua) {
    auto* active = HookContext(lua);
    if (active->handled) return luaL_error(lua, "original was already called or the hook was skipped");
    active->handled = true;
    if (!lua_isnoneornil(lua, 2)) active->context->eax = CheckU32(lua, 2);
    if (!lua_isnoneornil(lua, 3)) active->context->edx = CheckU32(lua, 3);
    return 0;
}

void SetHookMethod(lua_State* lua, const char* name, lua_CFunction function) {
    lua_pushcfunction(lua, function);
    lua_setfield(lua, -2, name);
}

void PushHookContext(lua_State* lua, ActiveHook* active) {
    lua_createtable(lua, 0, 8);
    lua_pushlightuserdata(lua, active);
    lua_setfield(lua, -2, "_pd");
    lua_pushinteger(lua, static_cast<lua_Integer>(active->generation));
    lua_setfield(lua, -2, "_generation");
    SetHookMethod(lua, "get", LuaHookGet);
    SetHookMethod(lua, "set", LuaHookSet);
    SetHookMethod(lua, "arg", LuaHookArgument);
    SetHookMethod(lua, "set_arg", LuaHookSetArgument);
    SetHookMethod(lua, "original", LuaHookOriginal);
    SetHookMethod(lua, "skip", LuaHookSkip);
}

template <size_t Index>
void DispatchHook(Ctx* context) {
    ScriptHook* hook = g_hooks[Index];
    if (!hook || !hook->original) return;
    ScriptState* state = hook->owner;
    if (g_shutdown || !state || state->failed || state->in_callback || !hook->enabled) {
        hook->original(context);
        return;
    }
    ActiveHook active;
    active.hook = hook;
    active.context = context;
    active.generation = ++g_hook_generation;
    ActiveHook* previous = g_active_hook;
    g_active_hook = &active;
    lua_State* lua = state->lua;
    lua_rawgeti(lua, LUA_REGISTRYINDEX, hook->callback_ref);
    PushHookContext(lua, &active);
    const bool success = InvokeProtected(state, 1);
    if ((!success && !active.original_invoked) || (success && !active.handled)) {
        active.original_invoked = true;
        hook->original(context);
    }
    active.context = nullptr;
    g_active_hook = previous;
}

template <size_t... Index>
constexpr auto HookDispatchers(std::index_sequence<Index...>) {
    return std::array<ModHookFn, sizeof...(Index)>{&DispatchHook<Index>...};
}

constexpr auto g_hook_dispatchers = HookDispatchers(std::make_index_sequence<kMaxScriptHooks>{});

int LuaEngineHook(lua_State* lua) {
    auto* state = State(lua);
    if (!state->loading) return MemoryFailure(lua, "hooks must be registered by the top-level script");
    uint32_t rva = 0;
    if (!TargetRva(lua, 1, &rva)) return MemoryFailure(lua, "hook target must be an exact symbol, RVA, or rebased address");
    luaL_checktype(lua, 2, LUA_TFUNCTION);
    size_t slot = 0;
    while (slot < g_hooks.size() && g_hooks[slot]) ++slot;
    if (slot == g_hooks.size()) return MemoryFailure(lua, "script hook table is full");
    auto hook = std::make_unique<ScriptHook>();
    hook->owner = state;
    hook->target = kEngineBase + rva;
    lua_pushvalue(lua, 2);
    hook->callback_ref = luaL_ref(lua, LUA_REGISTRYINDEX);
    if (!ModHookInstall(hook->target, g_hook_dispatchers[slot], &hook->original)) {
        luaL_unref(lua, LUA_REGISTRYINDEX, hook->callback_ref);
        return MemoryFailure(lua, "target has no recompiled entry or the native hook backend rejected it");
    }
    hook->enabled = true;
    g_hooks[slot] = hook.get();
    state->hooks.push_back(hook.get());
    // Process lifetime is intentional: detours can still be entered during engine teardown.
    g_hook_storage.push_back(std::move(hook));
    lua_pushinteger(lua, static_cast<lua_Integer>(slot + 1));
    return 1;
}

void Register(lua_State* lua, ScriptState* state, const char* name, lua_CFunction function) {
    lua_pushlightuserdata(lua, state);
    lua_pushcclosure(lua, function, 1);
    lua_setfield(lua, -2, name);
}

void NewApiTable(lua_State* lua, ScriptState* state,
                 const std::vector<std::pair<const char*, lua_CFunction>>& functions) {
    lua_createtable(lua, 0, static_cast<int>(functions.size()));
    for (const auto& function : functions) Register(lua, state, function.first, function.second);
}

void RemoveGlobal(lua_State* lua, const char* name) {
    lua_pushnil(lua);
    lua_setglobal(lua, name);
}

void OpenRestrictedLibraries(ScriptState* state) {
    lua_State* lua = state->lua;
    luaL_requiref(lua, "_G", luaopen_base, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, "table", luaopen_table, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, "string", luaopen_string, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, "math", luaopen_math, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, "utf8", luaopen_utf8, 1);
    lua_pop(lua, 1);
    for (const char* name : {"load", "loadfile", "dofile", "require", "module"}) RemoveGlobal(lua, name);
    lua_getglobal(lua, "string");
    lua_pushnil(lua);
    lua_setfield(lua, -2, "dump");
    lua_pop(lua, 1);

    lua_createtable(lua, 0, 16);
    Register(lua, state, "log", LuaLog);
    Register(lua, state, "warn", LuaWarn);
    Register(lua, state, "on", LuaOn);
    Register(lua, state, "emit", LuaEmit);
    Register(lua, state, "require", LuaRequire);
    lua_pushinteger(lua, 1);
    lua_setfield(lua, -2, "api_version");
    lua_pushstring(lua, state->mod->id.c_str());
    lua_setfield(lua, -2, "mod_id");

    NewApiTable(lua, state, {{"get_boolean", LuaSettingBoolean}, {"get_number", LuaSettingNumber},
                             {"get_string", LuaSettingString}});
    lua_setfield(lua, -2, "settings");
    NewApiTable(lua, state, {{"find", LuaSymbolFind}, {"search", LuaSymbolSearch}, {"count", LuaSymbolCount}});
    lua_setfield(lua, -2, "symbols");
    NewApiTable(lua, state, {{"call", LuaEngineCall}, {"hook", LuaEngineHook}});
    lua_setfield(lua, -2, "engine");
    NewApiTable(lua, state, {
        {"read_u8", ReadNumber<uint8_t>}, {"read_u16", ReadNumber<uint16_t>}, {"read_u32", LuaReadU32},
        {"read_i8", ReadNumber<int8_t>}, {"read_i16", ReadNumber<int16_t>}, {"read_i32", ReadNumber<int32_t>},
        {"read_f32", ReadNumber<float>}, {"write_u8", WriteNumber<uint8_t>},
        {"write_u16", WriteNumber<uint16_t>}, {"write_u32", LuaWriteU32},
        {"write_i8", WriteNumber<int8_t>}, {"write_i16", WriteNumber<int16_t>},
        {"write_i32", WriteNumber<int32_t>}, {"write_f32", LuaWriteFloat},
        {"read_bytes", LuaReadBytes}, {"write_bytes", LuaWriteBytes},
        {"read_string", LuaReadString}, {"write_string", LuaWriteString}, {"is_valid", LuaMemoryValid},
    });
    lua_setfield(lua, -2, "memory");
    lua_pushvalue(lua, -1);
    lua_setglobal(lua, "pd");

    // Replace base print with the same bounded per-mod log path.
    lua_pushlightuserdata(lua, state);
    lua_pushcclosure(lua, LuaLog, 1);
    lua_setglobal(lua, "print");
    lua_pop(lua, 1);  // pd
}

void InvokeEvent(ScriptState* state, const std::string& name, const std::vector<uint32_t>& arguments) {
    if (!state->active || state->failed) return;
    auto found = state->callbacks.find(name);
    if (found == state->callbacks.end()) return;
    for (int reference : found->second) {
        lua_rawgeti(state->lua, LUA_REGISTRYINDEX, reference);
        for (uint32_t argument : arguments) lua_pushinteger(state->lua, argument);
        if (!InvokeProtected(state, static_cast<int>(arguments.size()))) break;
    }
}

void DispatchEvents() {
    if (g_dispatching_events || g_shutdown) return;
    g_dispatching_events = true;
    size_t dispatched = 0;
    while (!g_events.empty() && dispatched < kMaxQueuedEvents) {
        ScriptEvent event = std::move(g_events.front());
        g_events.pop_front();
        ++dispatched;
        for (auto& state : g_states) InvokeEvent(state.get(), event.name, event.arguments);
    }
    if (!g_events.empty()) {
        Log("mods: script event cascade exceeded %zu events; clearing queued events", kMaxQueuedEvents);
        g_events.clear();
    }
    g_dispatching_events = false;
}

void LoadScript(const ModEntry& mod) {
    if (g_states.size() >= kMaxScripts) {
        ModWriteLaunchError(mod, "script package limit reached");
        return;
    }
    auto state = std::make_unique<ScriptState>();
    state->mod = &mod;
    std::string error;
    if (!ModSettingsLoad(mod, &state->settings, &error)) {
        ModWriteLaunchError(mod, error);
        return;
    }
    state->lua = lua_newstate(LuaAllocator, state.get());
    if (!state->lua) {
        ModWriteLaunchError(mod, "Lua state could not be allocated within its memory limit");
        return;
    }
    g_states.push_back(std::move(state));
    ScriptState* current = g_states.back().get();
    *static_cast<ScriptState**>(lua_getextraspace(current->lua)) = current;
    lua_newtable(current->lua);
    current->modules_ref = luaL_ref(current->lua, LUA_REGISTRYINDEX);
    OpenRestrictedLibraries(current);
    const size_t slash = mod.script.rfind('/');
    current->script_directory = slash == std::string::npos ? mod.root : mod.script.substr(0, slash);
    std::string source;
    if (!ReadChunk(current, mod.script, &source, &error)) {
        DisableState(current, error);
        return;
    }
    const std::string chunk = "@" + mod.script;
    if (luaL_loadbufferx(current->lua, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK) {
        const char* message = lua_tostring(current->lua, -1);
        DisableState(current, message ? message : "Lua entrypoint did not compile");
        lua_pop(current->lua, 1);
        return;
    }
    current->loading = true;
    const bool loaded = InvokeProtected(current, 0);
    current->loading = false;
    if (!loaded) return;
    current->active = true;
    unlink((mod.root + "/launch-error.txt").c_str());
    Log("mods: script %s loaded (%zu settings, %zu source bytes)", mod.id.c_str(),
        current->settings.size(), current->source_bytes);
}

}  // namespace

bool ModScriptsEmit(const std::string& name, const uint32_t* arguments, size_t count) {
    return QueueEvent(name, "native", arguments, count);
}

void ModScriptsInitialize(const std::vector<ModEntry>& mods) {
    for (const ModEntry& mod : mods) if (!mod.script.empty()) LoadScript(mod);
    Log("mods: %zu sandboxed Lua package(s) initialized", g_states.size());
}

void ModScriptsFrame() {
    if (g_shutdown) return;
    static const std::vector<uint32_t> no_arguments;
    for (auto& state : g_states) InvokeEvent(state.get(), "frame", no_arguments);
    DispatchEvents();
}

void ModScriptsShutdown() {
    if (g_shutdown) return;
    g_shutdown = true;  // reject new queued events and bypass script detours during teardown
    static const std::vector<uint32_t> no_arguments;
    // Dependants shut down before their dependencies. Keep states and detours allocated for late engine teardown.
    for (auto state = g_states.rbegin(); state != g_states.rend(); ++state) InvokeEvent(state->get(), "shutdown", no_arguments);
    g_events.clear();
    for (auto& state : g_states) {
        state->active = false;
        for (ScriptHook* hook : state->hooks) if (hook) hook->enabled = false;
    }
}

}  // namespace native
