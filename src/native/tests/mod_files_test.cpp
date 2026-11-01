// Production loose-overlay and guest/save path routing tests with real files and symlinks.
#include "mod_platform.h"
#include "save_path.h"
#include "win.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace native {
const char* g_game_dir = nullptr;
const char* g_data_dir = nullptr;
void Log(const char*, ...) {}
int SettingInt(const char*, int fallback) { return fallback; }
std::string Setting(const char*, const char* fallback) { return fallback ? fallback : ""; }
}  // namespace native

namespace {
namespace fs = std::filesystem;
int checks = 0;

void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

struct Temp {
    fs::path path;
    Temp() {
        char pattern[] = "/tmp/protodroid-mod-files-XXXXXX";
        path = mkdtemp(pattern);
        Check(!path.empty(), "temporary directory");
    }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};

void Write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
    output.close();
    Check(output.good(), "write fixture");
}

std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

native::ModEntry Overlay(const fs::path& root, int priority) {
    native::ModEntry mod;
    mod.id = priority == 200 ? "high" : "low";
    mod.root = root.string();
    mod.files = root.string();
    mod.priority = priority;
    return mod;
}

}  // namespace

int main() {
    using namespace native;
    Temp temp;
    fs::path game = temp.path / "game";
    fs::path data = temp.path / "app-data";
    fs::path high = temp.path / "high";
    fs::path low = temp.path / "low";
    fs::create_directories(game / "data");
    fs::create_directories(data);
    g_game_dir = game.c_str();
    g_data_dir = data.c_str();
    SetGuestCurrentDir("C:\\Game");

    Write(game / "data" / "text.txt", "original");
    Write(high / "Data" / "Text.txt", "high");
    Write(low / "data" / "text.txt", "low");
    ModFilesInitialize(std::vector<ModEntry>{Overlay(high, 200), Overlay(low, 100)});
    Check(ModFilesHaveOverlays(), "overlay sources registered");

    const char* name = "C:\\Game\\DATA\\TEXT.TXT";
    std::string original = HostPath(name);
    std::string resolved = ModFilesResolveReadPath(name, original);
    Check(resolved == (high / "Data" / "Text.txt").string(), "case-insensitive high-priority overlay");
    Check(Read(game / "data" / "text.txt") == "original", "original game file remains unchanged");

    fs::path alias = temp.path / "alias.bin";
    Write(alias, "alias");
    Check(ModFilesAddAlias(300, "data/text.txt", alias.string()), "native plugin alias registered");
    Check(ModFilesResolveReadPath(name, original) == alias.string(), "higher-priority alias wins");
    Check(ModFilesHasLooseCandidate(name), "loose candidate visible to file detour");
    Check(!ModFilesAddAlias(300, "../outside", alias.string()), "alias traversal rejected");
    Check(!ModFilesAddAlias(300, "data/missing", (temp.path / "missing").string()), "missing alias file rejected");

    std::string save = HostPath((std::string(kGuestSaveDir) + "\\profile.bin").c_str());
    Check(ModFilesResolveReadPath((std::string(kGuestSaveDir) + "\\profile.bin").c_str(), save) == save,
          "save path excluded from overlay");
    std::string traversal = HostPath("C:\\Game\\..\\Saves\\profile.bin");
    Check(ModFilesResolveReadPath("C:\\Game\\..\\Saves\\profile.bin", traversal) == traversal,
          "normalized traversal cannot enter overlay");

    fs::path outside = temp.path / "outside.bin";
    Write(outside, "outside");
    fs::create_symlink(outside, high / "Link.bin");
    std::string link_original = HostPath("C:\\Game\\Link.bin");
    Check(ModFilesResolveReadPath("C:\\Game\\Link.bin", link_original) == link_original,
          "symlink cannot escape mod root");

    ModFilesInitialize(std::vector<ModEntry>{});
    Check(!ModFilesHaveOverlays(), "empty catalog disables overlays");
    Check(ModFilesResolveReadPath(name, original) == original, "disabled overlays return original path");
    Check(!ModFilesHasLooseCandidate(name), "ordinary loose preference remains off by default");

    std::cout << "mod file tests passed: " << checks << " checks\n";
    return 0;
}
