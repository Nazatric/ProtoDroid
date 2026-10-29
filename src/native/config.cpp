// Port settings: protore.ini search priority (Android 11+ scoped storage aware):
//   1) /storage/emulated/0/Prototype/protore.ini (shared storage, always accessible via MANAGE_EXTERNAL_STORAGE)
//   2) /storage/emulated/0/Prototype/game/protore.ini (when game files are in subfolder)
//   3) <game_dir>/protore.ini and <game_dir>/../protore.ini (detected game folder)
//   4) ./protore.ini (working dir = app external files on Android: /sdcard/Android/data/com.protodroid/files)
//   5) /storage/emulated/0/Android/data/com.protodroid/files/protore.ini (explicit fallback)
// Format: `key = value` per line, `#` or `;` comment, [section] headers tolerated (ignored).
// Environment variable PROTORE_<KEY> (upper case) overrides file.
#include <cctype>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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

std::map<std::string, std::string> Load() {
    std::map<std::string, std::string> m;

    std::vector<std::string> candidates;
    // Shared storage (primary on Android 11+)
    candidates.push_back("/storage/emulated/0/Prototype/protore.ini");
    candidates.push_back("/storage/emulated/0/Prototype/game/protore.ini");
    // Detected game directory (if set before first Setting() call, after main sets g_game_dir)
    if (g_game_dir && g_game_dir[0]) {
        candidates.push_back(std::string(g_game_dir) + "/protore.ini");
        candidates.push_back(std::string(g_game_dir) + "/../protore.ini");
        // Also try upper directory when game_dir is .../Prototype/game -> ini in .../Prototype
        std::string gd(g_game_dir);
        size_t slash = gd.rfind('/');
        if (slash != std::string::npos) {
            candidates.push_back(gd.substr(0, slash) + "/protore.ini");
        }
    }
    // App private external files (cwd on Android after chdir(base))
    candidates.push_back("protore.ini");
    candidates.push_back("/storage/emulated/0/Android/data/com.protodroid/files/protore.ini");
    // Also check g_data_dir if available (external files)
    if (g_data_dir && g_data_dir[0]) {
        candidates.push_back(std::string(g_data_dir) + "/protore.ini");
    }

    std::string loaded_path;
    FILE* f = nullptr;
    for (const auto& path : candidates) {
        f = fopen(path.c_str(), "r");
        if (f) {
            loaded_path = path;
            break;
        }
    }
    if (!f) {
        // No config found - still log candidates for diagnostics if Log available
        // Try to log only if g_data_dir is set (after chdir), otherwise silently continue
        return m;
    }

    // Log the exact path loaded
    Log("config: loaded %s", loaded_path.c_str());

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        // Strip trailing newline/cr already handled via comment cut
        size_t cut = s.find_first_of("#;\r\n");
        if (cut != std::string::npos) s.resize(cut);
        s = Trim(s);
        if (s.empty()) continue;
        // Section header: [graphics] etc. - ignore but allow following keys
        if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(s.substr(0, eq));
        std::string value = Trim(s.substr(eq + 1));
        if (key.empty()) continue;
        for (char& ch : key) ch = char(tolower((unsigned char)ch));
        m[key] = value;
        Log("config: %s = %s", key.c_str(), value.c_str());
    }
    fclose(f);
    if (m.empty()) {
        Log("config: %s empty or no valid entries", loaded_path.c_str());
    }
    return m;
}

}  // namespace

std::string Setting(const char* key, const char* def) {
    std::string env = "PROTORE_";
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
