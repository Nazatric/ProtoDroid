// Port settings: protodroid.ini search priority (Android 11+ scoped storage aware).
//
// Two places can hold the file, and they are not the same file:
//   * the app's own external files folder - this is the one the launcher's settings page writes, and the only one
//     whose keys the launcher owns (`GameSettings.writeEngineSettings`). Android: /sdcard/Android/data/
//     com.protodroid/files/protodroid.ini, which is also the process working directory.
//   * shared storage - /storage/emulated/0/Prototype/protodroid.ini, where a user keeps hand-written keys
//     (sync_futex, detailed_diagnostics, ...) next to the game data.
//
// Reading only the first file that exists made every launcher toggle silently inert whenever the shared-storage file
// existed: the engine loaded /Prototype/protodroid.ini, and the launcher's file was never opened (a device log showed
// exactly one key, `sync_futex = 0`, while the launcher had written a full set of engine options). So every file in
// the list is loaded now, in this order, and the first file that defines a key wins:
//
//   1) <app external files>/protodroid.ini          (launcher's own file: always authoritative)
//   2) ./protodroid.ini                             (working directory = the same folder on Android)
//   3) /storage/emulated/0/Android/data/com.protodroid/files/protodroid.ini
//   4) /storage/emulated/0/Prototype/protodroid.ini (shared storage, hand-written keys)
//   5) /storage/emulated/0/Prototype/game/protodroid.ini
//   6) <game_dir>/protodroid.ini and <game_dir>/../protodroid.ini
//
// A key defined by a lower-priority file is still used when no higher-priority file defines it, so hand-written keys
// keep working. Each assignment is logged with its file, and a key that a higher-priority file already set is logged
// as shadowed, so a device log always shows which file the engine actually obeyed.
//
// Format: `key = value` per line, `#` or `;` comment, [section] headers tolerated (ignored).
// Environment variable PROTODROID_<KEY> (upper case) overrides every file.
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

std::string Lower(std::string v) {
    for (char& ch : v) ch = char(tolower((unsigned char)ch));
    return v;
}

}  // namespace

// Parses one file's text into `out`, which keeps the first value seen for a key (the higher-priority file wins), and
// counts the lines that repeated a key. Shared by Load() and the host test, so the precedence rule that is tested is
// the one the loader runs.
int ParseIniText(const std::string& text, std::map<std::string, std::string>& out, int* shadowed) {
    int accepted = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string s = text.substr(pos, end - pos);
        pos = end + 1;
        size_t cut = s.find_first_of("#;\r\n");
        if (cut != std::string::npos) s.resize(cut);
        s = Trim(s);
        if (s.empty()) continue;
        if (s.size() >= 2 && s.front() == '[' && s.back() == ']') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Lower(Trim(s.substr(0, eq)));
        std::string value = Trim(s.substr(eq + 1));
        if (key.empty()) continue;
        if (out.find(key) != out.end()) {
            if (shadowed) ++*shadowed;
            continue;
        }
        out[key] = value;
        ++accepted;
    }
    return accepted;
}


namespace {

// Priority-ordered candidates. Paths are duplicated in the list (the app folder is reachable three ways); a
// duplicate that resolves to the same file is loaded once because a key from the first load shadows the repeat.
std::vector<std::string> Candidates() {
    std::vector<std::string> candidates;
    if (g_data_dir && g_data_dir[0]) {
        candidates.push_back(std::string(g_data_dir) + "/protodroid.ini");
    }
    candidates.push_back("protodroid.ini");  // cwd is the app's external files folder on Android
    candidates.push_back("/storage/emulated/0/Android/data/com.protodroid/files/protodroid.ini");
    candidates.push_back("/storage/emulated/0/Prototype/protodroid.ini");
    candidates.push_back("/storage/emulated/0/Prototype/game/protodroid.ini");
    if (g_game_dir && g_game_dir[0]) {
        candidates.push_back(std::string(g_game_dir) + "/protodroid.ini");
        std::string gd(g_game_dir);
        size_t slash = gd.rfind('/');
        if (slash != std::string::npos) candidates.push_back(gd.substr(0, slash) + "/protodroid.ini");
    }
    return candidates;
}

bool ReadWholeFile(const std::string& path, std::string* out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out->append(buffer, n);
    fclose(f);
    return true;
}

std::map<std::string, std::string> Load() {
    std::map<std::string, std::string> m;

    int files_loaded = 0;
    for (const std::string& path : Candidates()) {
        std::string text;
        if (!ReadWholeFile(path, &text)) continue;
        ++files_loaded;
        Log("config: loaded %s", path.c_str());
        std::map<std::string, std::string> file_keys;
        int shadowed_in_file = 0;
        ParseIniText(text, file_keys, &shadowed_in_file);
        int accepted = 0;
        for (const auto& entry : file_keys) {
            auto existing = m.find(entry.first);
            if (existing != m.end()) {
                // A higher-priority file owns the key; say so instead of silently dropping this line.
                Log("config: %s = %s shadowed by %s (keeps %s)", entry.first.c_str(), entry.second.c_str(),
                    path.c_str(), existing->second.c_str());
                continue;
            }
            m[entry.first] = entry.second;
            ++accepted;
            Log("config: %s = %s", entry.first.c_str(), entry.second.c_str());
        }
        Log("config: %s: %d key(s) used, %d repeated line(s)", path.c_str(), accepted, shadowed_in_file);
    }
    if (!files_loaded) {
        // No config found - log the candidates when the app folder is known (after chdir), else stay quiet.
        if (g_data_dir && g_data_dir[0]) {
            for (const std::string& path : Candidates()) Log("config: no file at %s", path.c_str());
        }
        return m;
    }
    if (m.empty()) {
        Log("config: %d file(s) loaded but no valid entries", files_loaded);
    }
    return m;
}

// Kept alive for the process lifetime: Setting() hands out pointers into it.
const std::map<std::string, std::string>& File() {
    static std::once_flag once;
    static std::map<std::string, std::string> file;
    std::call_once(once, [] { file = Load(); });
    return file;
}

}  // namespace

std::string Setting(const char* key, const char* def) {
    std::string env = "PROTODROID_";
    for (const char* k = key; *k; ++k) env += char(toupper((unsigned char)*k));
    if (const char* v = getenv(env.c_str())) return v;
    const auto& file = File();
    auto it = file.find(key);
    return it != file.end() ? it->second : def;
}

int SettingInt(const char* key, int def) {
    std::string v = Setting(key, "");
    return v.empty() ? def : atoi(v.c_str());
}

}  // namespace native
