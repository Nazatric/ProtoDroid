// Win32 guest-path normalization and case-insensitive host-path resolution.
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstring>
#include <mutex>
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
std::string MatchCase(const std::string& dir, const std::string& name) {
    struct stat st;
    if (stat((dir + "/" + name).c_str(), &st) == 0) return name;
    DIR* d = opendir(dir.c_str());
    if (!d) return name;
    std::string found = name;
    while (dirent* e = readdir(d)) {
        if (strcasecmp(e->d_name, name.c_str()) == 0) {
            found = e->d_name;
            break;
        }
    }
    closedir(d);
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
