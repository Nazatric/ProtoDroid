#include "save_path.h"

#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace native {

void Log(const char* fmt, ...);

const char* const kGuestSaveDir = "C:\\Users\\Player\\Documents\\Prototype";
const char* const kAndroidSharedSaveDir = "/storage/emulated/0/Prototype";

namespace {

std::string g_save_dir;

bool IsDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool Exists(const std::string& path) {
    struct stat st;
    return lstat(path.c_str(), &st) == 0;
}

bool EnsureDirs(const std::string& path) {
    if (path.empty()) return false;
    std::string current = path;
    for (size_t i = 1; i < current.size(); ++i) {
        if (current[i] != '/') continue;
        current[i] = 0;
        if (current[0] && mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) return false;
        current[i] = '/';
    }
    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) return false;
    return IsDirectory(path);
}

std::string MatchCase(const std::string& dir, const std::string& name) {
    if (Exists(dir + "/" + name)) return name;
    DIR* d = opendir(dir.c_str());
    if (!d) return name;
    std::string found = name;
    while (dirent* entry = readdir(d)) {
        if (strcasecmp(entry->d_name, name.c_str()) == 0) {
            found = entry->d_name;
            break;
        }
    }
    closedir(d);
    return found;
}

std::string LegacySaveDir(const char* data_dir, const char* player = "Player") {
    std::string path = std::string(data_dir) + "/native_c";
    for (const char* component : {"Users", player, "Documents", "Prototype"})
        path += "/" + MatchCase(path, component);
    return path;
}

bool WritableDirectory(const std::string& path) {
    if (!IsDirectory(path)) return false;
    std::string pattern = path + "/.save-write-test.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    int fd = mkstemp(name.data());
    if (fd < 0) return false;
    bool ok = close(fd) == 0;
    if (unlink(name.data()) != 0) ok = false;
    return ok;
}

bool WriteAll(int fd, const unsigned char* data, size_t size) {
    while (size) {
        ssize_t written = write(fd, data, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        data += written;
        size -= size_t(written);
    }
    return true;
}

bool CopyFile(const std::string& source, const std::string& target) {
    int in = open(source.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) return false;
    std::string pattern = target + ".migrate.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    int out = mkstemp(name.data());
    if (out < 0) {
        close(in);
        return false;
    }
    // External-storage FUSE may reject chmod even when creation and writes are allowed; mkstemp permissions are fine.
    bool ok = true;
    unsigned char buffer[16384];
    while (ok) {
        ssize_t count = read(in, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            ok = false;
            break;
        }
        if (count == 0) break;
        ok = WriteAll(out, buffer, size_t(count));
    }
    if (close(in) != 0) ok = false;
    // Migration runs before the engine starts. Make the copied bytes durable before recording that migration finished.
    if (ok && fsync(out) != 0) ok = false;
    if (close(out) != 0) ok = false;
    if (ok && rename(name.data(), target.c_str()) != 0) ok = false;
    if (!ok) unlink(name.data());
    return ok;
}

bool CopyTree(const std::string& source, const std::string& target, uint32_t& copied) {
    DIR* dir = opendir(source.c_str());
    if (!dir) return errno == ENOENT;
    bool ok = true;
    while (ok) {
        errno = 0;
        dirent* entry = readdir(dir);
        if (!entry) {
            if (errno) ok = false;
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        std::string from = source + "/" + entry->d_name;
        std::string to = target + "/" + MatchCase(target, entry->d_name);
        struct stat st;
        if (lstat(from.c_str(), &st) != 0) {
            ok = false;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            if (Exists(to) && !IsDirectory(to)) {
                ok = false;
                break;
            }
            ok = EnsureDirs(to) && CopyTree(from, to, copied);
        } else if (S_ISREG(st.st_mode) && !Exists(to)) {
            ok = CopyFile(from, to);
            if (ok) ++copied;
        }
        // Existing shared entries always win. Other file types and symlinks are not save files and are not followed.
    }
    if (closedir(dir) != 0) ok = false;
    return ok;
}

bool WriteMigrationMarker(const std::string& marker) {
    std::string pattern = marker + ".XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    int fd = mkstemp(name.data());
    if (fd < 0) return false;
    const char text[] = "shared save migration v1\n";
    bool ok = WriteAll(fd, reinterpret_cast<const unsigned char*>(text), sizeof(text) - 1);
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && rename(name.data(), marker.c_str()) != 0) ok = false;
    if (!ok) unlink(name.data());
    return ok;
}

}  // namespace

bool ConfigureSaveDirectory(const char* data_dir, const char* shared_dir) {
    g_save_dir.clear();
    if (!data_dir || !*data_dir || !shared_dir || !*shared_dir) return false;
    const std::string singular_legacy = LegacySaveDir(data_dir);
    const std::string plural_legacy = LegacySaveDir(data_dir, "Players");
    std::string legacy = singular_legacy;
    // Some existing installations used a lower-case/plural "users/players" directory. Migrate both spellings,
    // while preferring whichever one actually exists for the no-shared-access fallback.
    if (!IsDirectory(legacy) && IsDirectory(plural_legacy)) legacy = plural_legacy;
    const std::string marker = std::string(data_dir) + "/native_c/.shared-saves-v1";
    if (!EnsureDirs(shared_dir) || !WritableDirectory(shared_dir)) {
        if (EnsureDirs(legacy)) g_save_dir = legacy;
        return false;
    }

    if (!Exists(marker)) {
        if (!EnsureDirs(std::string(data_dir) + "/native_c")) {
            if (EnsureDirs(legacy)) g_save_dir = legacy;
            return false;
        }
        uint32_t copied = 0;
        if (!CopyTree(singular_legacy, shared_dir, copied) || !CopyTree(plural_legacy, shared_dir, copied) ||
            !WriteMigrationMarker(marker)) {
            // Do not switch to a partially migrated directory. A later launch retries missing entries without
            // overwriting anything, while this launch keeps using the intact legacy files.
            if (EnsureDirs(legacy)) g_save_dir = legacy;
            Log("shared save migration failed; using %s", legacy.c_str());
            return false;
        }
        if (copied) Log("migrated %u legacy save file(s) to %s", copied, shared_dir);
    }
    g_save_dir = shared_dir;
    return true;
}

const std::string& SaveDirectory() { return g_save_dir; }

bool IsGuestSavePath(const std::string& full_path) {
    const size_t size = strlen(kGuestSaveDir);
    return full_path.size() >= size && strncasecmp(full_path.c_str(), kGuestSaveDir, size) == 0 &&
           (full_path.size() == size || full_path[size] == '\\');
}

}  // namespace native
