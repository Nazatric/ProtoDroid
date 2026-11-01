#include "mod_platform.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits.h>
#include <map>
#include <sstream>

#include "guest.h"

namespace native {
namespace {

std::vector<ModEntry> g_mods;
std::string g_mods_root;
std::string g_native_root;

std::string Trim(std::string value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

std::string Lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool SafeId(const std::string& id) {
    if (id.empty() || id.size() > 64
        || !(std::isdigit(static_cast<unsigned char>(id[0])) || std::islower(static_cast<unsigned char>(id[0])))) {
        return false;
    }
    for (char c : id) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && !std::islower(static_cast<unsigned char>(c)) && c != '.'
            && c != '_' && c != '-') return false;
    }
    auto ends = [&](const char* suffix) {
        size_t n = strlen(suffix);
        return id.size() >= n && id.compare(id.size() - n, n, suffix) == 0;
    };
    return id.rfind("import-", 0) != 0 && !ends(".tmp") && !ends(".previous");
}

bool SafeRelative(const std::string& path, bool allow_dot) {
    if (allow_dot && path == ".") return true;
    if (path.empty() || path.size() > 240 || path[0] == '/' || path[0] == '\\') return false;
    if (path.find('\\') != std::string::npos || path.find(':') != std::string::npos) return false;
    size_t begin = 0;
    while (begin <= path.size()) {
        size_t end = path.find('/', begin);
        std::string part = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (part.empty() || part == "." || part == "..") return false;
        for (char c : part) if (std::iscntrl(static_cast<unsigned char>(c))) return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}

bool Directory(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool RealDirectory(const std::string& path) {
    struct stat st {};
    return lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool File(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool Within(const std::string& root, const std::string& candidate) {
    char root_real[PATH_MAX], candidate_real[PATH_MAX];
    if (!realpath(root.c_str(), root_real) || !realpath(candidate.c_str(), candidate_real)) return false;
    std::string r(root_real), c(candidate_real);
    return c == r || (c.size() > r.size() && c.compare(0, r.size(), r) == 0 && c[r.size()] == '/');
}

std::map<std::string, std::string> ReadIni(const std::string& path, std::string* error) {
    std::map<std::string, std::string> values;
    std::ifstream input(path);
    if (!input) {
        *error = "metadata could not be read";
        return values;
    }
    std::string section, line;
    int number = 0;
    while (std::getline(input, line)) {
        ++number;
        if (number == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef
            && static_cast<unsigned char>(line[1]) == 0xbb && static_cast<unsigned char>(line[2]) == 0xbf) {
            line.erase(0, 3);
        }
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = Lower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            *error = "invalid metadata line " + std::to_string(number);
            values.clear();
            return values;
        }
        std::string key = section + "." + Lower(Trim(line.substr(0, equals)));
        if (!values.emplace(key, Trim(line.substr(equals + 1))).second) {
            *error = "duplicate metadata key " + key;
            values.clear();
            return values;
        }
    }
    return values;
}

std::string Value(const std::map<std::string, std::string>& values, const std::string& key) {
    auto it = values.find(key);
    return it == values.end() ? "" : it->second;
}

bool ParseInt(const std::string& text, int fallback, int minimum, int maximum, int* output) {
    if (text.empty()) {
        *output = fallback;
        return true;
    }
    char* end = nullptr;
    errno = 0;
    long value = std::strtol(text.c_str(), &end, 10);
    if (errno || !end || *end || value < minimum || value > maximum) return false;
    *output = static_cast<int>(value);
    return true;
}

bool ReadMod(const std::string& root, const std::string& id, ModEntry* mod, std::string* error) {
    if (!SafeId(id)) {
        *error = "unsafe mod ID";
        return false;
    }
    std::string metadata = root + "/" + id + "/" + "protodroid-mod.ini";
    auto values = ReadIni(metadata, error);
    if (!error->empty()) return false;
    std::string schema = Value(values, "mod.schema");
    int api = 1;
    if ((!schema.empty() && schema != "1") || !ParseInt(Value(values, "mod.api"), 1, 1, 1, &api)) {
        *error = "unsupported mod schema or API";
        return false;
    }
    if (Value(values, "mod.id") != id) {
        *error = "mod ID does not match its folder";
        return false;
    }
    std::string engine = Lower(Value(values, "mod.engine_sha256"));
    if (!engine.empty() && engine != kModEngineSha256) {
        *error = "mod requires a different engine build";
        return false;
    }
    mod->id = id;
    mod->root = root + "/" + id;
    if (!ParseInt(Value(values, "mod.priority"), 100, -10000, 10000, &mod->priority)) {
        *error = "invalid mod priority";
        return false;
    }
    std::string files = Value(values, "content.files");
    std::string textures = Value(values, "content.textures");
    std::string plugin = Value(values, "content.plugin");
    if (!SafeRelative(files, true) && !files.empty()) {
        *error = "unsafe loose-file path";
        return false;
    }
    if (!SafeRelative(textures, false) && !textures.empty()) {
        *error = "unsafe texture mapping path";
        return false;
    }
    if (!SafeRelative(plugin, false) && !plugin.empty()) {
        *error = "unsafe native plugin path";
        return false;
    }
    if (files.empty() && textures.empty() && plugin.empty()) {
        *error = "mod has no content";
        return false;
    }
    if (!files.empty()) {
        std::string path = mod->root + "/" + files;
        if (!Directory(path) || !Within(mod->root, path)) {
            *error = "loose-file directory is missing or unsafe";
            return false;
        }
        mod->files = files == "." ? mod->root : path;
    }
    if (!textures.empty()) {
        std::string path = mod->root + "/" + textures;
        if (!File(path) || !Within(mod->root, path)) {
            *error = "texture mapping is missing or unsafe";
            return false;
        }
        mod->textures = path;
    }
    if (!plugin.empty()) {
        constexpr const char* prefix = "lib/arm64-v8a/";
        if (plugin.compare(0, strlen(prefix), prefix) != 0 || plugin.size() <= strlen(prefix) + 3
            || plugin.substr(plugin.size() - 3) != ".so") {
            *error = "native plugin must be below lib/arm64-v8a";
            return false;
        }
        std::string source = mod->root + "/" + plugin;
        if (!File(source) || !Within(mod->root, source)) {
            *error = "native plugin is missing or unsafe";
            return false;
        }
        mod->plugin = plugin.substr(strlen(prefix));
    }
    return true;
}

void ScanMods() {
    const char* configured = getenv("PROTODROID_MODS");
#ifdef __ANDROID__
    g_mods_root = configured && *configured ? configured : "/storage/emulated/0/Prototype/mods";
#else
    g_mods_root = configured && *configured
        ? configured
        : (g_game_dir ? std::string(g_game_dir) + "/../mods" : "mods");
#endif
    const char* native = getenv("PROTODROID_MOD_NATIVE_DIR");
    g_native_root = native ? native : "";
    DIR* dir = opendir(g_mods_root.c_str());
    if (!dir) {
        Log("mods: no package directory at %s", g_mods_root.c_str());
        return;
    }
    while (dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        std::string id = entry->d_name;
        std::string root = g_mods_root + "/" + id;
        if (!RealDirectory(root) || !File(root + "/enabled")) continue;
        ModEntry mod;
        std::string error;
        if (!ReadMod(g_mods_root, id, &mod, &error)) {
            Log("mods: %s rejected: %s", id.c_str(), error.c_str());
            ModEntry failed;
            failed.id = id;
            failed.root = root;
            ModWriteLaunchError(failed, error);
            continue;
        }
        unlink((root + "/launch-error.txt").c_str());
        g_mods.push_back(std::move(mod));
    }
    closedir(dir);
    std::sort(g_mods.begin(), g_mods.end(), [](const ModEntry& a, const ModEntry& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.id < b.id;
    });
    Log("mods: %zu active package(s), root %s", g_mods.size(), g_mods_root.c_str());
    for (const ModEntry& mod : g_mods) {
        Log("mods: active %s priority=%d files=%s textures=%s plugin=%s", mod.id.c_str(), mod.priority,
            mod.files.empty() ? "no" : "yes", mod.textures.empty() ? "no" : "yes",
            mod.plugin.empty() ? "no" : mod.plugin.c_str());
    }
}

}  // namespace

const std::vector<ModEntry>& ModEntries() { return g_mods; }
const std::string& ModsRoot() { return g_mods_root; }
const std::string& ModNativeRoot() { return g_native_root; }

void ModWriteLaunchError(const ModEntry& mod, const std::string& message) {
    if (mod.root.empty()) return;
    // Do not follow a manually planted symlink/hard link into original game or save data.
    unlink((mod.root + "/launch-error.txt").c_str());
    std::ofstream output(mod.root + "/launch-error.txt", std::ios::trunc);
    if (output) output << message << '\n';
}

void ModPlatformInitialize() {
    ScanMods();
    ModFilesInitialize(g_mods);
    ModTexturesInitialize(g_mods);
    ModFeaturesInitialize();
    ModPluginsInitialize(g_mods);
}

void ModPlatformFrame() { ModPluginsFrame(); }
void ModPlatformShutdown() { ModPluginsShutdown(); }

}  // namespace native
