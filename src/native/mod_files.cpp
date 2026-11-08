#include "mod_platform.h"

#include <dirent.h>
#include <limits.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "guest.h"
#include "win.h"

namespace native {
namespace {

struct Overlay {
    int priority = 0;
    int order = 0;
    std::string root;       // directory overlay when alias_key is empty
    std::string alias_key;  // exact normalized guest relative path for an alias
    std::string alias_path;
};

std::mutex g_mutex;
std::atomic<bool> g_active{false};
std::vector<Overlay> g_overlays;
std::unordered_map<std::string, std::string> g_cache;
int g_order = 0;

std::string NormalizeRelative(std::string value) {
    for (char& c : value) {
        if (c == '\\') c = '/';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (!value.empty() && value.front() == '/') value.erase(value.begin());
    return value;
}

bool SafeRelative(const std::string& path) {
    if (path.empty() || path.size() > 512 || path.front() == '/' || path.find(':') != std::string::npos) return false;
    size_t begin = 0;
    while (begin <= path.size()) {
        size_t end = path.find('/', begin);
        std::string part = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}

bool GuestRelative(const char* guest_name, std::string* relative) {
    if (!guest_name) return false;
    std::string full = FullGuestPath(guest_name);
    const std::string game = "C:\\Game";
    if (strncasecmp(full.c_str(), game.c_str(), game.size()) != 0
        || (full.size() > game.size() && full[game.size()] != '\\')) return false;
    if (full.size() <= game.size()) return false;
    *relative = NormalizeRelative(full.substr(game.size() + 1));
    return SafeRelative(*relative);
}

bool WithinRoot(const std::string& root, const std::string& candidate) {
    char r[PATH_MAX], c[PATH_MAX];
    if (!realpath(root.c_str(), r) || !realpath(candidate.c_str(), c)) return false;
    std::string root_real(r), path(c);
    return path.size() > root_real.size() && path.compare(0, root_real.size(), root_real) == 0
        && path[root_real.size()] == '/';
}

std::string MatchCase(const std::string& directory, const std::string& name) {
    std::string exact = directory + "/" + name;
    struct stat st {};
    if (stat(exact.c_str(), &st) == 0) return exact;
    DIR* dir = opendir(directory.c_str());
    if (!dir) return "";
    std::string found;
    while (dirent* entry = readdir(dir)) {
        if (strcasecmp(entry->d_name, name.c_str()) == 0) {
            found = directory + "/" + entry->d_name;
            break;
        }
    }
    closedir(dir);
    return found;
}

std::string ResolveCaseInsensitive(const std::string& root, const std::string& relative) {
    std::string current = root;
    size_t begin = 0;
    while (begin < relative.size()) {
        size_t end = relative.find('/', begin);
        std::string part = relative.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        current = MatchCase(current, part);
        if (current.empty()) return "";
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    struct stat st {};
    if (stat(current.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || !WithinRoot(root, current)) return "";
    return current;
}

void SortOverlays() {
    std::stable_sort(g_overlays.begin(), g_overlays.end(), [](const Overlay& a, const Overlay& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.order < b.order;
    });
}

}  // namespace

void ModFilesInitialize(const std::vector<ModEntry>& mods) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_active.store(false, std::memory_order_release);
    g_overlays.clear();
    g_cache.clear();
    for (const ModEntry& mod : mods) {
        if (mod.files.empty()) continue;
        Overlay overlay;
        overlay.priority = mod.priority;
        overlay.order = g_order++;
        overlay.root = mod.files;
        g_overlays.push_back(std::move(overlay));
    }
    SortOverlays();
    g_active.store(!g_overlays.empty(), std::memory_order_release);
    Log("mods: %zu loose-file overlay source(s)", g_overlays.size());
}

bool ModFilesHaveOverlays() { return g_active.load(std::memory_order_acquire); }

std::string ModFilesResolveReadPath(const char* guest_name, const std::string& original_host) {
    if (!g_active.load(std::memory_order_acquire)) return original_host;
    std::string relative;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_overlays.empty()) return original_host;
    }
    if (!GuestRelative(guest_name, &relative)) return original_host;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto cached = g_cache.find(relative);
    if (cached != g_cache.end()) return cached->second.empty() ? original_host : cached->second;
    std::string resolved;
    for (const Overlay& overlay : g_overlays) {
        if (!overlay.alias_key.empty()) {
            if (relative == overlay.alias_key) {
                resolved = overlay.alias_path;
                break;
            }
        } else {
            resolved = ResolveCaseInsensitive(overlay.root, relative);
            if (!resolved.empty()) break;
        }
    }
    if (g_cache.size() >= 8192) g_cache.clear();
    g_cache[relative] = resolved;
    if (!resolved.empty()) Log("mods: overlay %s -> %s", relative.c_str(), resolved.c_str());
    return resolved.empty() ? original_host : resolved;
}

bool ModFilesHasLooseCandidate(const char* guest_name) {
    std::string original = HostPath(guest_name);
    if (ModFilesResolveReadPath(guest_name, original) != original) return true;
    static const bool prefer = SettingInt("prefer_loose_files", 0) != 0;
    if (!prefer) return false;
    struct stat st {};
    return stat(original.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool ModFilesAddAlias(int priority, const char* guest_relative, const std::string& host_file) {
    if (!guest_relative) return false;
    std::string key = NormalizeRelative(guest_relative);
    if (!SafeRelative(key)) return false;
    struct stat st {};
    if (stat(host_file.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    Overlay overlay;
    overlay.priority = priority;
    overlay.order = g_order++;
    overlay.alias_key = key;
    overlay.alias_path = host_file;
    g_overlays.push_back(std::move(overlay));
    SortOverlays();
    g_cache.clear();
    g_active.store(true, std::memory_order_release);
    return true;
}

}  // namespace native
