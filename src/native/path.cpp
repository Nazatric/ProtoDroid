// Win32 guest-path normalization and case-insensitive host-path resolution.
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <string>
#include <vector>

#include "save_path.h"
#include "win.h"

namespace native {

const char* const kGuestGameDir = "C:\\Game";

namespace {

std::mutex g_cwd_mutex;
std::string g_cwd = kGuestGameDir;

std::string HostRootFor(char drive) {
    // C:\\Game is the game directory; everything else on C: lives under native_c (legacy saves, settings): in the
    // app's data folder on Android (the game folder may be shared storage), next to the game folder elsewhere.
    (void)drive;
    return g_data_dir ? std::string(g_data_dir) + "/native_c" : std::string(g_game_dir) + "/../native_c";
}

// Finds `name` in `dir` ignoring case; returns the real name or `name` itself when nothing matches.
// Item 50: the result is cached per (directory, name). The engine opens the same game files over and over with
// Windows-style casing against lower-case asset names, so every open used to cost one stat() plus, on a miss, a full
// opendir/readdir/strcasecmp scan of the directory - each of those a FUSE round trip. A directory's case mapping only
// changes when files are added or renamed, and the game's read-only asset tree does not, so the cache is only dropped
// for the writable folders (saves, logs) when an entry is not found.
namespace {
struct CaseCacheKey {
    std::string dir, name;
    bool operator==(const CaseCacheKey& o) const { return dir == o.dir && name == o.name; }
};
struct CaseCacheHash {
    size_t operator()(const CaseCacheKey& k) const {
        return std::hash<std::string>()(k.dir) * 1099511628211ull ^ std::hash<std::string>()(k.name);
    }
};
std::shared_mutex g_case_mutex;
std::unordered_map<CaseCacheKey, std::string, CaseCacheHash> g_case_cache;

bool WritablePath(const std::string& dir) {
    return dir.find("/Saves") != std::string::npos || dir.find("saves") != std::string::npos ||
           dir.find("/logs") != std::string::npos || dir.find("native_c") != std::string::npos;
}
}  // namespace

std::string MatchCase(const std::string& dir, const std::string& name) {
    const CaseCacheKey key{dir, name};
    {
        std::shared_lock<std::shared_mutex> lock(g_case_mutex);
        auto it = g_case_cache.find(key);
        if (it != g_case_cache.end()) return it->second;
    }
    std::string found = name;
    struct stat st;
    if (stat((dir + "/" + name).c_str(), &st) != 0) {
        if (DIR* d = opendir(dir.c_str())) {
            while (dirent* e = readdir(d)) {
                if (strcasecmp(e->d_name, name.c_str()) == 0) {
                    found = e->d_name;
                    break;
                }
            }
            closedir(d);
        }
    }
    {
        std::unique_lock<std::shared_mutex> lock(g_case_mutex);
        if (g_case_cache.size() > 65536) g_case_cache.clear();  // bound, in case of a pathological stream of names
        if (!WritablePath(dir) || found != name) g_case_cache[key] = found;
    }
    return found;
}

std::string Normalize(const std::string& path) {
    // Absolute Windows path with "." and ".." resolved, backslashes.
    std::vector<std::string> parts;
    std::string drive = path.substr(0, 2);
    size_t i = 2;
    while (i <= path.size()) {
        size_t j = path.find_first_of("\\/", i);
        if (j == std::string::npos) j = path.size();
        std::string p = path.substr(i, j - i);
        if (p == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!p.empty() && p != ".") {
            parts.push_back(p);
        }
        i = j + 1;
    }
    std::string r = drive;
    for (auto& p : parts) r += "\\" + p;
    if (parts.empty()) r += "\\";
    return r;
}

}  // namespace

std::string GuestCurrentDir() {
    std::lock_guard<std::mutex> lock(g_cwd_mutex);
    return g_cwd;
}

void SetGuestCurrentDir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(g_cwd_mutex);
    g_cwd = dir;
}

std::string FullGuestPath(const char* win_path) {
    std::string p(win_path);
    if (p.size() >= 2 && p[1] == ':') return Normalize(p);
    if (!p.empty() && (p[0] == '\\' || p[0] == '/')) return Normalize(GuestCurrentDir().substr(0, 2) + p);
    return Normalize(GuestCurrentDir() + "\\" + p);
}

std::string HostPath(const char* win_path) {
    std::string full = FullGuestPath(win_path);
    std::string rest;
    std::string host;
    std::string game = kGuestGameDir;
    if (!SaveDirectory().empty() && IsGuestSavePath(full)) {
        // Save/profile files live in the user-managed shared Prototype/Saves folder when all-files access is
        // available. The same case-folding below sees files added externally and never consults the legacy backup.
        host = SaveDirectory();
        rest = full.substr(strlen(kGuestSaveDir));
    } else if (strncasecmp(full.c_str(), game.c_str(), game.size()) == 0 &&
               (full.size() == game.size() || full[game.size()] == '\\')) {
        host = g_game_dir;
        rest = full.substr(game.size());
    } else {
        host = HostRootFor(full[0]);
        rest = full.substr(2);
    }
    size_t i = 0;
    while (i < rest.size()) {
        if (rest[i] == '\\') {
            ++i;
            continue;
        }
        size_t j = rest.find('\\', i);
        if (j == std::string::npos) j = rest.size();
        host += "/" + MatchCase(host, rest.substr(i, j - i));
        i = j;
    }
    return host;
}

}  // namespace native
