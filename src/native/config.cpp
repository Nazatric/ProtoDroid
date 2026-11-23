// Port settings: protodroid.ini search priority (Android 11+ scoped storage aware):
//   1) /storage/emulated/0/Prototype/protodroid.ini (shared storage, always accessible via MANAGE_EXTERNAL_STORAGE)
//   2) /storage/emulated/0/Prototype/game/protodroid.ini (when game files are in subfolder)
//   3) <game_dir>/protodroid.ini and <game_dir>/../protodroid.ini (detected game folder)
//   4) ./protodroid.ini (working dir = app external files on Android: /sdcard/Android/data/com.protodroid/files)
//   5) /storage/emulated/0/Android/data/com.protodroid/files/protodroid.ini (explicit fallback)
// The first readable file remains the base, preserving the legacy search behavior. ProtoDroid's launcher writes its
// engine settings to app-specific external storage; that file is then layered over the base so launcher choices win
// for duplicate keys without discarding unrelated values from a shared/game-directory INI.
// Format: `key = value` per line, `#` or `;` comment, [section] headers tolerated (ignored).
// Environment variable PROTODROID_<KEY> (upper case) overrides file.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "config_policy.h"
#include "guest.h"

namespace native {

namespace {

std::string Trim(const std::string& v) {
    size_t start = 0;
    while (start < v.size() && isspace((unsigned char)v[start])) ++start;
    size_t end = v.size();
    while (end > start && isspace((unsigned char)v[end - 1])) --end;
    return v.substr(start, end - start);
}

bool SameFile(const std::string& left, const std::string& right) {
    if (left == right) return true;
    struct stat left_stat {}, right_stat {};
    return stat(left.c_str(), &left_stat) == 0 && stat(right.c_str(), &right_stat) == 0
        && left_stat.st_dev == right_stat.st_dev && left_stat.st_ino == right_stat.st_ino;
}

bool LoadConfigFile(const std::string& path, ConfigSettings* settings) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    Log("config: loaded %s", path.c_str());

    ConfigSettings layer;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        // Strip trailing newline/cr and comments.
        size_t cut = s.find_first_of("#;\r\n");
        if (cut != std::string::npos) s.resize(cut);
        s = Trim(s);
        if (s.empty()) continue;
        // Section header: [graphics] etc. - ignore but allow following keys
        if (s.size() >= 2 && s.front() == '[' && s.back() == ']') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(s.substr(0, eq));
        std::string value = Trim(s.substr(eq + 1));
        if (key.empty()) continue;
        for (char& ch : key) ch = char(tolower((unsigned char)ch));
        layer[key] = value;
        Log("config: %s = %s (from %s)", key.c_str(), value.c_str(), path.c_str());
    }
    fclose(f);
    if (layer.empty()) Log("config: %s empty or no valid entries", path.c_str());
    *settings = std::move(layer);
    return true;
}

std::map<std::string, std::string> Load() {
    std::vector<std::string> candidates;
    // Shared storage (primary on Android 11+)
    candidates.push_back("/storage/emulated/0/Prototype/protodroid.ini");
    candidates.push_back("/storage/emulated/0/Prototype/game/protodroid.ini");
    // Detected game directory (if set before first Setting() call, after main sets g_game_dir)
    if (g_game_dir && g_game_dir[0]) {
        candidates.push_back(std::string(g_game_dir) + "/protodroid.ini");
        candidates.push_back(std::string(g_game_dir) + "/../protodroid.ini");
        // Also try upper directory when game_dir is .../Prototype/game -> ini in .../Prototype
        std::string gd(g_game_dir);
        size_t slash = gd.rfind('/');
        if (slash != std::string::npos) candidates.push_back(gd.substr(0, slash) + "/protodroid.ini");
    }
    // App private external files (cwd on Android after chdir(base))
    candidates.push_back("protodroid.ini");
    candidates.push_back("/storage/emulated/0/Android/data/com.protodroid/files/protodroid.ini");
    // Also check g_data_dir if available (external files)
    if (g_data_dir && g_data_dir[0]) candidates.push_back(std::string(g_data_dir) + "/protodroid.ini");

    ConfigSettings effective;
    std::string primary_path;
    for (const auto& path : candidates) {
        ConfigSettings layer;
        if (LoadConfigFile(path, &layer)) {
            effective = std::move(layer);
            primary_path = path;
            break;
        }
    }

    // GameSettings.writeEngineSettings() persists only the launcher-managed engine keys in getExternalFilesDir(null).
    // The search above deliberately retains its old first-file behavior for the base configuration; overlay the
    // app-specific file separately so `unlock_dev_menu` and other launcher choices reach the native runtime even when
    // a shared /Prototype/protodroid.ini exists. Higher layers replace only keys they actually contain.
    std::vector<std::string> app_settings;
    if (g_data_dir && g_data_dir[0]) app_settings.push_back(std::string(g_data_dir) + "/protodroid.ini");
    app_settings.push_back("/storage/emulated/0/Android/data/com.protodroid/files/protodroid.ini");
    app_settings.push_back("protodroid.ini");
    for (const auto& path : app_settings) {
        if (!primary_path.empty() && SameFile(primary_path, path)) break;
        ConfigSettings layer;
        if (!LoadConfigFile(path, &layer)) continue;
        MergeConfigLayer(effective, layer);
        Log("config: app-specific settings from %s override matching base keys", path.c_str());
        break;
    }
    return effective;
}

}  // namespace

std::string Setting(const char* key, const char* def) {
    std::string env = "PROTODROID_";
    for (const char* k = key; *k; ++k) env += char(toupper((unsigned char)*k));
    if (const char* v = getenv(env.c_str())) return v;
    static std::once_flag once;
    static std::map<std::string, std::string> file;
    std::call_once(once, [] { file = Load(); });
    auto it = file.find(key);
    return it != file.end() ? it->second : def;
}

int SettingInt(const char* key, int def) {
    std::string v = Setting(key, "");
    return v.empty() ? def : atoi(v.c_str());
}

}  // namespace native
