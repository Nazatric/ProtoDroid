// Production Lua loader/runtime test with a deterministic native hook backend and real generated symbols.
#include "../mod_platform.h"

#include <sys/mman.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../guest.h"

namespace {
namespace fs = std::filesystem;
int checks = 0;
std::unordered_map<uint32_t, RecompFn> targets;
std::unordered_map<void*, void*> replacements;
std::vector<std::string> logs;
std::vector<std::string> launch_errors;
uint32_t call_target = 0, call_ecx = 0;
std::vector<uint32_t> call_arguments;
int original_calls = 0;

void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool LogContains(const std::string& value) {
    for (const std::string& line : logs) if (line.find(value) != std::string::npos) return true;
    return false;
}

void OriginalTarget(Ctx* context) {
    ++original_calls;
    context->eax = static_cast<uint32_t>(*native::Ptr<float>(context->esp + 4)) + 10;
    context->edx = 0x44;
}

void Invoke(RecompFn target, Ctx* context) {
    auto found = replacements.find(reinterpret_cast<void*>(target));
    if (found == replacements.end()) target(context);
    else reinterpret_cast<RecompFn>(found->second)(context);
}

void WriteManifest(const fs::path& root, const std::string& id, const std::string& source) {
    fs::create_directories(root / id / "scripts");
    std::ofstream manifest(root / id / "protodroid-mod.ini");
    manifest << "[mod]\napi=2\nid=" << id << "\nname=" << id << "\n[content]\nscript=scripts/main.lua\n";
    std::ofstream script(root / id / "scripts/main.lua");
    script << source;
}

const char* kAlpha = R"LUA(
assert(pd.api_version == 1 and pd.mod_id == 'test.alpha')
assert(io == nil and os == nil and package == nil and debug == nil)
assert(load == nil and loadfile == nil and dofile == nil and require == nil and string.dump == nil)
assert(pd.symbols.count() == 105037)
local symbol, error = pd.symbols.find('FUN_106dcfe0')
assert(symbol and not error and symbol.rva == 0x6dcfe0 and symbol.address == 0x7c6dcfe0)
assert(symbol.ordinal == 1 and symbol.matches == 1)
local duplicate = 'proto::fightitems::motion::SetVehicleSolverEnabledTrack::SetVehicleSolverEnabledTrack'
local first = pd.symbols.find(duplicate, 1)
local second = pd.symbols.find(duplicate, 2)
assert(first.rva == 0x1005 and second.rva == 0x1c8be0)
assert(first.ordinal == 1 and second.ordinal == 2 and first.matches == 2 and second.matches == 2)
local absent, ordinal_error = pd.symbols.find(duplicate, 3)
assert(absent == nil and ordinal_error ~= nil)
local found = pd.symbols.search('FUN_106dcfe0', 4)
assert(#found == 1 and found[1].address == symbol.address)
assert(pd.settings.get_boolean('enabled', false) == true)
assert(pd.settings.get_number('offset', 1.0) == 7.5)
assert(pd.settings.get_string('text', 'missing') == 'saved')
local util1 = pd.require('lib/util')
local util2 = pd.require('lib/util')
assert(util1 == util2 and util1.value == 42)
local ok = pcall(function() pd.require('../escape') end)
assert(not ok)
assert(pd.memory.write_u8(0x2000, 0xab))
assert(pd.memory.write_u16(0x2002, 0xbcde))
assert(pd.memory.write_u32(0x2004, 0xf1234567))
assert(pd.memory.write_i32(0x2008, -1234567))
assert(pd.memory.write_f32(0x200c, 1.25))
assert(pd.memory.read_u8(0x2000) == 0xab and pd.memory.read_u16(0x2002) == 0xbcde)
assert(pd.memory.read_u32(0x2004) == 0xf1234567 and pd.memory.read_i32(0x2008) == -1234567)
assert(pd.memory.read_f32(0x200c) == 1.25)
assert(pd.memory.write_bytes(0x2010, 'A\0B'))
local bytes = pd.memory.read_bytes(0x2010, 3)
assert(bytes == 'A\0B' and #bytes == 3)
assert(pd.memory.write_string(0x2020, 'Prototype'))
assert(pd.memory.read_string(0x2020, 32) == 'Prototype')
local invalid, range_error = pd.memory.read_u32(0xffffffff)
assert(invalid == nil and range_error ~= nil)
local unmapped, unmapped_error = pd.memory.read_u32(0x300000)
assert(unmapped == nil and unmapped_error ~= nil)
local unmapped_write, unmapped_write_error = pd.memory.write_u32(0x300000, 1)
assert(unmapped_write == nil and unmapped_write_error ~= nil)
local unmapped_bytes, unmapped_bytes_error = pd.memory.read_bytes(0x300000, 4)
assert(unmapped_bytes == nil and unmapped_bytes_error ~= nil)
assert(pd.memory.is_valid(0x100000001, 1) == false)
assert(pcall(function() pd.memory.read_u32(0) end) == false)
local missing, target_error = pd.engine.call('not.a.symbol')
assert(missing == nil and target_error ~= nil)
assert(pd.engine.call(symbol.address, {0x11, 0x22}, 0x33) == 0x9876)
local hook, hook_error = pd.engine.hook(symbol, function() end)
assert(hook == nil and hook_error ~= nil)
local previous_ctx = nil
local handle, install_error = pd.engine.hook(symbol.address, function(ctx)
    if previous_ctx then assert(not pcall(function() previous_ctx:get('esp') end)) end
    previous_ctx = ctx
    local bits = ctx:arg(0)
    ctx:set_arg(0, bits)
    local stack = ctx:get('esp') + 4
    local value = pd.memory.read_f32(stack)
    assert(value and pd.memory.write_f32(stack, value + 5.0))
    local eax, edx = ctx:original()
    assert(edx == 0x44)
    ctx:set('eax', eax + 2)
end)
assert(handle and not install_error)
local frames = 0
pd.on('frame', function()
    frames = frames + 1
    assert(pd.memory.write_u32(0x2040, frames))
    if frames == 1 then assert(pd.emit('example.ready', 0x123, 0xffffffff)) end
end)
pd.on('shutdown', function() pd.memory.write_u32(0x2044, 0x55) end)
pd.log('alpha initialized')
)LUA";

const char* kBeta = R"LUA(
pd.on('example.ready', function(a, b)
    assert(a == 0x123 and b == 0xffffffff)
    pd.memory.write_u32(0x3000, a)
    pd.memory.write_u32(0x3004, b)
end)
pd.on('native.ready', function(value) pd.memory.write_u32(0x3008, value) end)
pd.on('frame', function() pd.memory.write_u32(0x300c, 0x99) end)
)LUA";

}  // namespace

extern "C" RecompFn RecompLookup(uint32_t target) {
    auto found = targets.find(target);
    return found == targets.end() ? nullptr : found->second;
}

extern "C" int DobbyHook(void* address, void* fake, void** original) {
    *original = address;
    replacements[address] = fake;
    return 0;
}

namespace native {
void Log(const char* format, ...) {
    char text[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    logs.emplace_back(text);
}

void ModWriteLaunchError(const ModEntry& mod, const std::string& message) {
    launch_errors.push_back(mod.id + ": " + message);
}

uint32_t GuestCallArray(Ctx*, uint32_t target, const uint32_t* arguments, size_t count, uint32_t ecx) {
    call_target = target;
    call_ecx = ecx;
    call_arguments.assign(arguments, arguments + count);
    return 0x9876;
}
}  // namespace native

int main() {
    using namespace native;
    void* memory = mmap(reinterpret_cast<void*>(native::kGuestBase), 2 * 1024 * 1024,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    Check(memory != MAP_FAILED, "guest test memory");
    const std::string root = "/tmp/protodroid-script-test";
    fs::remove_all(root);
    fs::create_directories(root);
    WriteManifest(root, "test.alpha", kAlpha);
    WriteManifest(root, "test.beta", kBeta);
    WriteManifest(root, "test.loop", "pd.on('frame', function() while true do end end)\n");
    WriteManifest(root, "test.memory", "local t = {}\nfor i = 1, 100000 do t[i] = string.rep('x', 1024) end\n");
    {
        std::ofstream settings(root + "/test.alpha/protodroid-settings.ini");
        settings << "enabled=1\noffset=7.5\ntext=saved\n";
    }
    fs::create_directories(root + "/test.alpha/scripts/lib");
    {
        std::ofstream util(root + "/test.alpha/scripts/lib/util.lua");
        util << "return { value = 42 }\n";
    }

    std::vector<ModEntry> mods;
    for (const std::string id : {"test.alpha", "test.beta", "test.loop", "test.memory"}) {
        ModEntry mod;
        mod.id = id;
        mod.root = root + "/" + id;
        mod.script = mod.root + "/scripts/main.lua";
        mods.push_back(mod);
    }
    const std::string example_id = "protodroid.example.lua_fov";
    fs::copy("modding/script-example/package", root + "/" + example_id,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    {
        std::ofstream settings(root + "/" + example_id + "/protodroid-settings.ini");
        settings << "enabled=1\nfov_offset=0\nframe_logging=Quiet\n";
    }
    ModEntry example;
    example.id = example_id;
    example.root = root + "/" + example_id;
    example.script = example.root + "/scripts/main.lua";
    mods.push_back(example);
    targets[kEngineBase + 0x006dcfe0] = OriginalTarget;
    ModScriptsInitialize(mods);
    Check(LogContains("alpha initialized"), "main entrypoint, module and API execute");
    Check(LogContains("Lua FOV example initialized"), "shipped Lua example initializes and installs its detour");
    Check(call_target == kEngineBase + 0x006dcfe0 && call_ecx == 0x33, "symbolic guest call target/ecx");
    Check(call_arguments == std::vector<uint32_t>({0x11, 0x22}), "guest call argument array");
    Check(*Ptr<uint32_t>(0x2004) == 0xf1234567 && *Ptr<int32_t>(0x2008) == -1234567,
          "typed writes reach guest memory");
    Check(!launch_errors.empty(), "faulting resource test produced isolated launch errors");

    Ctx context{};
    context.esp = 0x4000;
    *Ptr<float>(context.esp + 4) = 50.0f;
    Invoke(OriginalTarget, &context);
    Check(context.eax == 67 && context.edx == 0x44, "Lua hook mutates argument and wraps original exactly once");
    *Ptr<float>(context.esp + 4) = 50.0f;
    context.eax = 0;
    Invoke(OriginalTarget, &context);
    Check(context.eax == 67, "stale hook contexts are rejected on a later detour generation");

    const uint32_t native_event = 0x777;
    Check(ModScriptsEmit("native.ready", &native_event, 1), "native event queues");
    ModScriptsFrame();
    Check(*Ptr<uint32_t>(0x2040) == 1 && *Ptr<uint32_t>(0x300c) == 0x99,
          "healthy mods continue after another mod exhausts its budget");
    Check(*Ptr<uint32_t>(0x3000) == 0x123 && *Ptr<uint32_t>(0x3004) == 0xffffffff,
          "cross-mod event preserves unsigned arguments");
    Check(*Ptr<uint32_t>(0x3008) == 0x777, "native-to-script event dispatches");
    Check(LogContains("instruction budget exhausted") || LogContains("script instruction"),
          "instruction exhaustion is diagnosed");
    Check(LogContains("memory") || LogContains("not enough"), "memory exhaustion is diagnosed");
    for (const std::string& error : launch_errors) {
        Check(error.find("test.alpha") == std::string::npos && error.find("test.beta") == std::string::npos &&
                  error.find(example_id) == std::string::npos,
              "healthy mods and shipped example are not disabled by another mod");
    }

    ModScriptsShutdown();
    Check(*Ptr<uint32_t>(0x2044) == 0x55, "shutdown callback runs");
    context.eax = 0;
    *Ptr<float>(context.esp + 4) = 50.0f;
    original_calls = 0;
    Invoke(OriginalTarget, &context);
    Check(context.eax == 60 && original_calls == 1, "late teardown detour uses original without freed Lua callback");
    std::cout << "mod script tests passed: " << checks << " checks\n";
    return 0;
}
