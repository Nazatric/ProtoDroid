// Real dlopen host test for the public C ABI using the source example and a second test plugin. Dobby itself is
// replaced by the deterministic test backend; all loader, API, chaining, guest-memory and event code is production.
#include "../mod_hook.cpp"
#include "../mod_plugins.cpp"

#include <sys/mman.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace {
namespace fs = std::filesystem;
int checks = 0;
std::unordered_map<uint32_t, RecompFn> targets;
std::unordered_map<void*, void*> replacements;
std::vector<std::string> logs;
std::vector<std::string> launch_errors;
std::string native_root;
std::string alias_key;
std::string alias_path;
float original_fov = 0;

uint32_t call_va = 0, call_ecx = 0;
std::vector<uint32_t> call_arguments;
Ctx* call_outer = reinterpret_cast<Ctx*>(1);

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

void OriginalFov(Ctx* c) { original_fov = *native::Ptr<float>(c->esp + 4); }
void GuestCallTarget(Ctx*) {}

void Invoke(RecompFn target, Ctx* c) {
    auto found = replacements.find(reinterpret_cast<void*>(target));
    if (found == replacements.end()) target(c);
    else reinterpret_cast<RecompFn>(found->second)(c);
}
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
const char* g_game_dir = nullptr;

void Log(const char* format, ...) {
    char text[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    logs.emplace_back(text);
}

int SettingInt(const char* key, int fallback) { return strcmp(key, "test_answer") == 0 ? 42 : fallback; }
std::string Setting(const char*, const char* fallback) { return fallback ? fallback : ""; }
const std::string& ModNativeRoot() { return native_root; }

void ModWriteLaunchError(const ModEntry& mod, const std::string& message) {
    launch_errors.push_back(mod.id + ": " + message);
}

bool ModFilesAddAlias(int, const char* guest, const std::string& host) {
    alias_key = guest ? guest : "";
    alias_path = host;
    return true;
}

uint32_t GuestCallArray(Ctx* outer, uint32_t va, const uint32_t* args, size_t count, uint32_t ecx) {
    call_outer = outer;
    call_va = va;
    call_ecx = ecx;
    call_arguments.assign(args, args + count);
    return 0x9876;
}
}  // namespace native

int main() {
    using namespace native;
    const std::string root = "/tmp/protodroid-plugin-test";
    native_root = root + "/native";
    std::string packages = root + "/packages";
    fs::create_directories(packages + "/example");
    fs::create_directories(packages + "/custom");
    {
        std::ofstream payload(packages + "/custom/payload.bin");
        payload << "payload";
    }
    std::string game = root + "/game";
    fs::create_directories(game);
    g_game_dir = game.c_str();

    void* memory = mmap(reinterpret_cast<void*>(0x1000000000ull), 2 * 1024 * 1024, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    Check(memory != MAP_FAILED, "guest test memory");
    targets[kEngineBase + 0x006dcfe0] = OriginalFov;
    targets[kEngineBase + 0x00001234] = GuestCallTarget;

    Plugin fallback;
    fallback.settings["number"] = "not-a-number";
    fallback.settings["boolean"] = "unexpected";
    fallback.settings["text"] = "saved";
    Check(ApiGetModNumber(&fallback, "number", 12.5) == 12.5, "malformed number uses plugin default");
    Check(ApiGetModBoolean(&fallback, "boolean", true), "malformed boolean uses plugin default");
    char text[16] = {};
    Check(ApiGetModString(&fallback, "text", "default", text, sizeof(text)) && std::string(text) == "saved",
          "per-mod string getter copies the saved value");
    char tiny[4] = {};
    Check(!ApiGetModString(&fallback, "text", "default", tiny, sizeof(tiny)) && tiny[0] == 0,
          "string getter rejects inadequate storage without truncation");
    Check(ApiGetModNumber(&fallback, "missing", 9.0) == 9.0 &&
              ApiGetModNumber(nullptr, "number", 4.0) == 4.0,
          "missing values and contexts are isolated defaults");

    ModEntry example;
    example.id = "example";
    example.root = packages + "/example";
    example.plugin = "libexample.so";
    example.pluginApi = 2;
    {
        std::ofstream settings(packages + "/example/protodroid-settings.ini");
        settings << "enabled=1\nfov_offset=7\nframe_logging=Every 300 frames\nauthor_note=Host option string\n";
    }
    {
        std::ofstream legacy_settings(packages + "/custom/protodroid-settings.ini");
        legacy_settings << "this legacy plugin does not consume settings\n";
    }
    ModEntry custom;
    custom.id = "custom";
    custom.root = packages + "/custom";
    custom.plugin = "libtest.so";
    std::vector<ModEntry> mods = {example, custom};
    ModPluginsInitialize(mods);
    Check(launch_errors.empty(), "both plugins initialize");
    Check(LogContains("example initialized") && LogContains("test plugin initialized") &&
              LogContains("Host option string"), "initialization logs and v2 text option");
    Check(alias_key == "data/test.bin", "plugin file alias key");
    Check(alias_path == packages + "/custom/payload.bin", "plugin alias remains inside package");

    Ctx context{};
    context.esp = 0x1000;
    *Ptr<float>(context.esp + 4) = 50.0f;
    Invoke(OriginalFov, &context);
    Check(original_fov == 58.0f, "legacy plugin +1 wraps configured v2 plugin +7 wraps original");
    Check(context.edx == 0x44, "plugin set_register reaches guest context");

    ModPluginsFrame();
    Check(call_outer == nullptr && call_va == kEngineBase + 0x1234 && call_ecx == 0x33, "guest call target/ecx");
    Check(call_arguments == std::vector<uint32_t>({0x11, 0x22}), "guest call stack arguments");
    Check(LogContains("test guest call ok"), "guest call result returned to plugin");
    for (int i = 1; i < 300; ++i) ModPluginsFrame();
    Check(LogContains("300 engine frames"), "example frame event");

    ModPluginsShutdown();
    Check(LogContains("test shutdown event"), "shutdown event");
    Check(LogContains("test plugin export shutdown") && LogContains("example ordinary shutdown"),
          "both exported shutdown callbacks");
    Check(launch_errors.empty(), "no lifecycle launch errors");

    std::cout << "mod plugin tests passed: " << checks << " checks\n";
    return 0;
}
