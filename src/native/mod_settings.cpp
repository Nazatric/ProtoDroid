#include "mod_settings.h"

#include <sys/stat.h>

#include <cerrno>
#include <cctype>
#include <fstream>

namespace native {
namespace {

std::string Trim(std::string value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

bool SafeConfigKey(const std::string& key) {
    if (key.empty() || key.size() > 48 ||
        !(std::islower(static_cast<unsigned char>(key[0])) || std::isdigit(static_cast<unsigned char>(key[0])))) {
        return false;
    }
    for (char c : key) {
        if (!std::islower(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c)) &&
            c != '.' && c != '_' && c != '-') return false;
    }
    return true;
}

}  // namespace

const std::string* ModSettingsValue(const ModSettings& settings, const char* key) {
    if (!key || !SafeConfigKey(key)) return nullptr;
    auto found = settings.find(key);
    return found == settings.end() ? nullptr : &found->second;
}

bool ModSettingsLoad(const ModEntry& mod, ModSettings* output, std::string* error) {
    if (!output || !error) return false;
    output->clear();
    const std::string path = mod.root + "/protodroid-settings.ini";
    struct stat st {};
    if (lstat(path.c_str(), &st) != 0) {
        if (errno == ENOENT) return true;
        *error = "mod settings could not be inspected";
        return false;
    }
    if (!S_ISREG(st.st_mode) || st.st_size > 128 * 1024) {
        *error = "mod settings are not a bounded regular file";
        return false;
    }
    std::ifstream input(path);
    if (!input) {
        *error = "mod settings could not be read";
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            *error = "invalid mod settings line";
            return false;
        }
        std::string key = Trim(line.substr(0, equals));
        std::string value = Trim(line.substr(equals + 1));
        if (!SafeConfigKey(key) || value.size() > 4096) {
            *error = "invalid mod settings key or value";
            return false;
        }
        for (char c : value) if (std::iscntrl(static_cast<unsigned char>(c))) {
            *error = "control character in mod settings";
            return false;
        }
        if (!output->emplace(key, value).second) {
            *error = "duplicate mod setting " + key;
            return false;
        }
        if (output->size() > 256) {
            *error = "too many saved mod settings";
            return false;
        }
    }
    return true;
}

}  // namespace native
