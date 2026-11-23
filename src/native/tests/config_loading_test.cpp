#include "guest.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace native {

const char* g_game_dir = nullptr;
const char* g_data_dir = nullptr;

void Log(const char*, ...) {}

}  // namespace native

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "config loading test failed: %s\n", message);
        std::exit(1);
    }
}

bool Readable(const char* path) {
    return access(path, R_OK) == 0;
}

}  // namespace

int main() {
    char template_path[] = "/tmp/protodroid-config-loading-XXXXXX";
    char* root_raw = mkdtemp(template_path);
    Check(root_raw != nullptr, "could not create temporary configuration directory");

    const std::filesystem::path root(root_raw);
    const std::filesystem::path game = root / "game";
    const std::filesystem::path app = root / "app-files";
    const std::filesystem::path work = root / "work";
    std::filesystem::create_directories(game);
    std::filesystem::create_directories(app);
    std::filesystem::create_directories(work);

    {
        std::ofstream base(game / "protodroid.ini");
        base << "unlock_dev_menu = 0\nproto_config_base_marker = preserved\n";
        Check(base.good(), "could not write base INI fixture");
    }
    {
        std::ofstream launcher(app / "protodroid.ini");
        launcher << "unlock_dev_menu = 1\n";
        Check(launcher.good(), "could not write app-specific INI fixture");
    }

    const std::filesystem::path previous_working_directory = std::filesystem::current_path();
    Check(chdir(work.c_str()) == 0, "could not isolate the configuration working directory");
    const std::string game_path = game.string();
    const std::string app_path = app.string();
    native::g_game_dir = game_path.c_str();
    native::g_data_dir = app_path.c_str();
    unsetenv("PROTODROID_UNLOCK_DEV_MENU");
    unsetenv("PROTODROID_PROTO_CONFIG_BASE_MARKER");

    Check(native::SettingInt("unlock_dev_menu", 0) == 1,
          "app-specific Developer menu On did not override the base INI");

    // On ordinary host/CI systems the temporary game INI is the selected base. If a shared-storage fixture happens
    // to exist in the runner, the app-specific override above is still tested and the unrelated-base assertion is
    // intentionally skipped rather than depending on external files.
    if (!Readable("/storage/emulated/0/Prototype/protodroid.ini")
        && !Readable("/storage/emulated/0/Prototype/game/protodroid.ini")) {
        Check(native::Setting("proto_config_base_marker", "missing") == "preserved",
              "launcher overlay discarded an unrelated base INI setting");
    }

    Check(chdir(previous_working_directory.c_str()) == 0, "could not restore the working directory");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::puts("config loading tests passed");
    return 0;
}
