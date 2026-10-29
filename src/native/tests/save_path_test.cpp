// Production guest-path routing and shared-save migration tests using real files and directories.
#include "save_path.h"
#include "win.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace native {
const char* g_game_dir = nullptr;
const char* g_data_dir = nullptr;
void Log(const char*, ...) {}
}  // namespace native

namespace {

namespace fs = std::filesystem;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "save-path test failed: %s\n", message);
        std::exit(1);
    }
}

struct TempDirectory {
    fs::path path;
    TempDirectory() {
        char pattern[] = "/tmp/protodroid-save-path-XXXXXX";
        char* created = mkdtemp(pattern);
        Check(created != nullptr, "could not create temporary directory");
        path = created;
    }
    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

void Write(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(contents.data(), std::streamsize(contents.size()));
    file.close();
    Check(file.good(), "could not write test file");
}

std::string Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool Exists(const fs::path& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string SavePath(const std::string& suffix) {
    return native::HostPath((std::string(native::kGuestSaveDir) + "\\" + suffix).c_str());
}

}  // namespace

int main() {
    TempDirectory temp;
    const fs::path game = temp.path / "game";
    const fs::path data = temp.path / "app-data";
    const fs::path shared = temp.path / "shared" / "Prototype";
    const fs::path legacy = data / "native_c" / "Users" / "Player" / "Documents" / "Prototype";
    const fs::path plural_legacy = data / "native_c" / "Users" / "Players" / "Documents" / "Prototype";
    fs::create_directories(game);
    fs::create_directories(data);
    native::g_game_dir = game.c_str();
    native::g_data_dir = data.c_str();
    native::SetGuestCurrentDir("C:\\Game");

    // Before Android configuration, the original app-specific route and game route remain intact.
    Check(native::SaveDirectory().empty(), "save directory was configured before initialization");
    Check(SavePath("profile.bin") == (legacy / "profile.bin").string(), "legacy save route changed");
    Write(game / "Engine.DLL", "game");
    Check(native::HostPath("C:\\Game\\engine.dll") == (game / "Engine.DLL").string(),
          "case-insensitive game path changed");

    Write(legacy / "profile.bin", "legacy-profile");
    Write(legacy / "slot-A.bin", "legacy-slot");
    Write(legacy / "settings" / "config.dat", "legacy-settings");
    Write(plural_legacy / "slot-P.bin", "plural-legacy-slot");
    Write(shared / "slot-A.bin", "shared-slot-wins");

    Check(native::ConfigureSaveDirectory(data.c_str(), shared.c_str()), "writable shared save directory was rejected");
    Check(native::SaveDirectory() == shared.string(), "shared save directory was not activated");
    Check(Read(shared / "profile.bin") == "legacy-profile", "legacy profile was not migrated");
    Check(Read(shared / "slot-A.bin") == "shared-slot-wins", "migration overwrote an existing shared save");
    Check(Read(shared / "settings" / "config.dat") == "legacy-settings", "nested save data was not migrated");
    Check(Read(shared / "slot-P.bin") == "plural-legacy-slot", "existing users/players save was not migrated");
    Check(Exists(data / "native_c" / ".shared-saves-v1"), "successful migration was not recorded");
    Check(Read(legacy / "profile.bin") == "legacy-profile", "legacy backup was destroyed during migration");

    // Mixed-case guest paths resolve externally managed files in the shared root, never in the legacy backup.
    Check(native::HostPath("C:\\users\\player\\documents\\prototype\\PROFILE.BIN") == (shared / "profile.bin").string(),
          "shared save path is not case-insensitive");
    Check(native::HostPath("C:\\Users\\Player\\Documents\\PrototypeBackup\\profile.bin") ==
              (legacy.parent_path() / "PrototypeBackup" / "profile.bin").string(),
          "save-path prefix matched a sibling directory");
    Check(native::HostPath("C:\\Game\\engine.dll") == (game / "Engine.DLL").string(),
          "shared saves changed game-file routing");
    native::SetGuestCurrentDir(native::kGuestSaveDir);
    Check(native::HostPath("slot-A.bin") == (shared / "slot-A.bin").string(),
          "relative save path did not use the shared save directory");
    native::SetGuestCurrentDir("C:\\Game");

    // Add a save externally, create another through the production host mapping, and delete the external one.
    Write(shared / "slot-B.bin", "added-externally");
    Check(Read(SavePath("slot-b.bin")) == "added-externally", "externally added save was not visible");
    const std::string created = SavePath("slot-C.bin");
    Write(created, "created-by-game");
    Check(Read(shared / "slot-C.bin") == "created-by-game", "new game save was not written to shared storage");
    Check(unlink((shared / "slot-B.bin").c_str()) == 0, "could not remove external-save fixture");
    Check(!Exists(SavePath("slot-B.bin")), "externally deleted save remained visible");

    // Reconfiguration must not resurrect a deleted shared file from the retained legacy backup.
    Check(unlink((shared / "profile.bin").c_str()) == 0, "could not delete migrated profile fixture");
    Check(native::ConfigureSaveDirectory(data.c_str(), shared.c_str()), "shared saves did not remain configured");
    Check(!Exists(shared / "profile.bin"), "one-time migration resurrected an externally deleted save");
    Check(Read(shared / "slot-A.bin") == "shared-slot-wins", "second migration overwrote a shared save");
    Write(shared / "slot-D.bin", "added-after-restart");
    Check(Read(SavePath("slot-D.bin")) == "added-after-restart", "save added after reconfiguration was not visible");

    // The exact lower-case/plural layout used by some existing installs is also found case-insensitively.
    const fs::path plural_data = temp.path / "plural-data";
    const fs::path plural_shared = temp.path / "plural-shared";
    const fs::path lower_legacy =
        plural_data / "native_c" / "users" / "players" / "documents" / "prototype";
    Write(lower_legacy / "profile.bin", "lower-plural-profile");
    native::g_data_dir = plural_data.c_str();
    Check(native::ConfigureSaveDirectory(plural_data.c_str(), plural_shared.c_str()),
          "lower-case/plural legacy layout prevented shared-save setup");
    Check(Read(plural_shared / "profile.bin") == "lower-plural-profile",
          "lower-case/plural legacy profile was not migrated");

    // If shared storage cannot be created, saves still work in the original app-specific location.
    const fs::path fallback_data = temp.path / "fallback-data";
    const fs::path blocked_shared = temp.path / "not-a-directory";
    fs::create_directories(fallback_data);
    Write(blocked_shared, "file blocks shared directory creation");
    native::g_data_dir = fallback_data.c_str();
    Check(!native::ConfigureSaveDirectory(fallback_data.c_str(), blocked_shared.c_str()),
          "a regular file was accepted as the shared save directory");
    const fs::path fallback_legacy =
        fallback_data / "native_c" / "Users" / "Player" / "Documents" / "Prototype";
    Check(native::SaveDirectory() == fallback_legacy.string(), "no-permission fallback did not retain legacy saves");
    Write(SavePath("slot-E.bin"), "fallback-save");
    Check(Read(fallback_legacy / "slot-E.bin") == "fallback-save", "legacy fallback save was not writable");

    std::puts("save-path tests passed (migration, add/create/delete, case folding, no resurrection, fallback)");
    return 0;
}
