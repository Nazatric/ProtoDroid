// Native package scanner/dependency test. Subsystems are recording stubs; production validation and ordering run unchanged.
#include "../mod_platform.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
int checks = 0;
std::vector<std::string> scripts;

void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void WriteMod(const fs::path& root, const std::string& id, int api, int priority,
              const std::string& depends, bool script, bool enabled = true) {
    fs::create_directories(root / id / "files");
    if (script) fs::create_directories(root / id / "scripts");
    std::ofstream output(root / id / "protodroid-mod.ini");
    output << "[mod]\napi=" << api << "\nid=" << id << "\nname=" << id << "\npriority=" << priority << "\n";
    if (!depends.empty()) output << "depends=" << depends << "\n";
    output << "[content]\n";
    if (script) output << "script=scripts/main.lua\nscript_api=1\n";
    else output << "files=files\n";
    if (script) std::ofstream(root / id / "scripts/main.lua") << "pd.log('test')\n";
    if (enabled) std::ofstream(root / id / "enabled") << "1";
}

std::string Error(const fs::path& root, const std::string& id) {
    std::ifstream input(root / id / "launch-error.txt");
    std::string line;
    std::getline(input, line);
    return line;
}
}  // namespace

namespace native {
const char* g_game_dir = nullptr;
void Log(const char*, ...) {}
void ModFilesInitialize(const std::vector<ModEntry>&) {}
void ModTexturesInitialize(const std::vector<ModEntry>&) {}
void ModFeaturesInitialize() {}
void ModPluginsInitialize(const std::vector<ModEntry>&) {}
void ModPluginsFrame() {}
void ModPluginsShutdown() {}
void ModScriptsInitialize(const std::vector<ModEntry>& mods) {
    for (const ModEntry& mod : mods) if (!mod.script.empty()) scripts.push_back(mod.id);
}
void ModScriptsFrame() {}
void ModScriptsShutdown() {}
bool ModScriptsEmit(const std::string&, const uint32_t*, size_t) { return true; }
}  // namespace native

int main() {
    using namespace native;
    const fs::path root = "/tmp/protodroid-platform-test";
    fs::remove_all(root);
    fs::create_directories(root);
    WriteMod(root, "core", 2, 100, "", false);
    WriteMod(root, "a.dependent", 2, 500, "core", false);
    WriteMod(root, "b.independent", 2, 300, "", false);
    WriteMod(root, "script.mod", 2, 100, "", true);
    WriteMod(root, "legacy", 1, 50, "", false);
    WriteMod(root, "missing", 2, 100, "not.installed", false);
    WriteMod(root, "cycle.a", 2, 100, "cycle.b", false);
    WriteMod(root, "cycle.b", 2, 100, "cycle.a", false);
    WriteMod(root, "downstream", 2, 100, "cycle.a", false);
    WriteMod(root, "disabled.dependency", 2, 100, "", false, false);
    WriteMod(root, "needs.disabled", 2, 100, "disabled.dependency", false);
    WriteMod(root, "bad.api", 1, 100, "", true);
    std::ofstream(root / "core/launch-error.txt") << "stale";
    std::ofstream(root / "outside.save") << "keep";
    setenv("PROTODROID_MODS", root.c_str(), 1);
    setenv("PROTODROID_MOD_NATIVE_DIR", (root / "native").c_str(), 1);

    ModPlatformInitialize();
    const auto& mods = ModEntries();
    Check(mods.size() == 5, "only valid enabled packages with resolved dependencies activate");
    const std::vector<std::string> expected = {"b.independent", "core", "a.dependent", "script.mod", "legacy"};
    for (size_t i = 0; i < expected.size() && i < mods.size(); ++i) Check(mods[i].id == expected[i], "deterministic dependency/priority order");
    Check(scripts == std::vector<std::string>({"script.mod"}), "valid Lua entrypoint reaches script subsystem");
    Check(Error(root, "missing").find("missing, disabled, or incompatible") != std::string::npos,
          "missing dependency gets a persistent launch error");
    Check(Error(root, "needs.disabled").find("missing, disabled, or incompatible") != std::string::npos,
          "disabled dependency gets a persistent launch error");
    Check(Error(root, "cycle.a").find("cycle") != std::string::npos &&
              Error(root, "cycle.b").find("cycle") != std::string::npos,
          "both cycle members are rejected");
    Check(Error(root, "downstream").find("cycle") != std::string::npos, "cycle dependent is rejected");
    Check(Error(root, "bad.api").find("package API 2") != std::string::npos, "API-1 script cannot activate");
    Check(Error(root, "core").empty(), "successful validation clears stale launch error");
    std::ifstream outside(root / "outside.save");
    std::string saved;
    outside >> saved;
    Check(saved == "keep", "scanner leaves unrelated data untouched");
    std::cout << "mod platform scanner tests passed: " << checks << " checks\n";
    return 0;
}
