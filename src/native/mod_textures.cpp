#include "mod_platform.h"

#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "guest.h"

namespace native {
namespace {

constexpr uint32_t kA8R8G8B8 = 21;
constexpr uint32_t kX8R8G8B8 = 22;
constexpr size_t kMaxDdsBytes = 128u * 1024u * 1024u;

constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) |
           (uint32_t(uint8_t(d)) << 24);
}

uint32_t Read32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void Put32(uint8_t* p, uint32_t value) {
    p[0] = uint8_t(value);
    p[1] = uint8_t(value >> 8);
    p[2] = uint8_t(value >> 16);
    p[3] = uint8_t(value >> 24);
}

uint32_t RotateRight(uint32_t value, unsigned bits) { return (value >> bits) | (value << (32 - bits)); }

// Small self-contained SHA-256 used for the versioned texture-key format. No provider/OpenSSL dependency is added to
// the Android native library.
class Sha256 {
public:
    Sha256() : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

    void Update(const void* input, size_t size) {
        const auto* data = static_cast<const uint8_t*>(input);
        total_ += size;
        while (size) {
            size_t take = std::min(size, block_.size() - used_);
            memcpy(block_.data() + used_, data, take);
            used_ += take;
            data += take;
            size -= take;
            if (used_ == block_.size()) {
                Transform(block_.data());
                used_ = 0;
            }
        }
    }

    void U32(uint32_t value) {
        uint8_t bytes[4] = {uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
        Update(bytes, sizeof(bytes));
    }

    std::array<uint8_t, 32> Finish() {
        uint64_t bits = total_ * 8;
        uint8_t padding[128]{};
        padding[0] = 0x80;
        size_t count = used_ < 56 ? 56 - used_ : 120 - used_;
        Update(padding, count);
        uint8_t length[8];
        for (unsigned i = 0; i < 8; ++i) length[i] = uint8_t(bits >> (56 - i * 8));
        Update(length, sizeof(length));
        std::array<uint8_t, 32> result{};
        for (size_t i = 0; i < state_.size(); ++i) {
            result[i * 4] = uint8_t(state_[i] >> 24);
            result[i * 4 + 1] = uint8_t(state_[i] >> 16);
            result[i * 4 + 2] = uint8_t(state_[i] >> 8);
            result[i * 4 + 3] = uint8_t(state_[i]);
        }
        return result;
    }

private:
    void Transform(const uint8_t* block) {
        static constexpr uint32_t constants[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (unsigned i = 0; i < 16; ++i) {
            const uint8_t* p = block + i * 4;
            w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
        }
        for (unsigned i = 16; i < 64; ++i) {
            uint32_t s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (unsigned i = 0; i < 64; ++i) {
            uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            uint32_t choice = (e & f) ^ (~e & g);
            uint32_t first = h + s1 + choice + constants[i] + w[i];
            uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            uint32_t second = s0 + majority;
            h = g; g = f; f = e; e = d + first;
            d = c; c = b; b = a; a = first + second;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<uint32_t, 8> state_;
    std::array<uint8_t, 64> block_{};
    uint64_t total_ = 0;
    size_t used_ = 0;
};

bool Layout(uint32_t format, uint32_t width, uint32_t height, uint32_t* pitch, uint32_t* rows) {
    if (!width || !height || width > 32768 || height > 32768) return false;
    uint64_t line = 0;
    if (format == FourCC('D', 'X', 'T', '1')) {
        line = uint64_t((width + 3) / 4) * 8;
        *rows = (height + 3) / 4;
    } else if (format == FourCC('D', 'X', 'T', '3') || format == FourCC('D', 'X', 'T', '5')) {
        line = uint64_t((width + 3) / 4) * 16;
        *rows = (height + 3) / 4;
    } else if (format == kA8R8G8B8 || format == kX8R8G8B8) {
        line = uint64_t(width) * 4;
        *rows = height;
    } else {
        return false;
    }
    if (!line || line > 0xffffffffu || uint64_t(line) * *rows > kMaxDdsBytes) return false;
    *pitch = uint32_t(line);
    return true;
}

std::string TextureKey(uint32_t format, uint32_t width, uint32_t height, const uint8_t* data, uint32_t pitch,
                       uint32_t tight_pitch, uint32_t rows) {
    Sha256 hash;
    static constexpr char domain[] = "ProtoDroidTextureV1";
    hash.Update(domain, sizeof(domain));  // includes the contract's terminating NUL
    hash.U32(format);
    hash.U32(width);
    hash.U32(height);
    hash.U32(tight_pitch);
    hash.U32(rows);
    for (uint32_t row = 0; row < rows; ++row) hash.Update(data + uint64_t(row) * pitch, tight_pitch);
    auto digest = hash.Finish();
    static constexpr char hex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (size_t i = 0; i < digest.size(); ++i) {
        result[i * 2] = hex[digest[i] >> 4];
        result[i * 2 + 1] = hex[digest[i] & 15];
    }
    return result;
}

std::string Trim(std::string value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

std::string Lower(std::string value) {
    for (char& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool SafeTexturePath(const std::string& path) {
    if (path.empty() || path.size() > 240 || path.front() == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos || Lower(path).size() < 4 ||
        Lower(path).compare(Lower(path).size() - 4, 4, ".dds") != 0) return false;
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

bool Within(const std::string& root, const std::string& candidate) {
    char r[PATH_MAX], c[PATH_MAX];
    if (!realpath(root.c_str(), r) || !realpath(candidate.c_str(), c)) return false;
    std::string root_real(r), path(c);
    return path.size() > root_real.size() && path.compare(0, root_real.size(), root_real) == 0 &&
           path[root_real.size()] == '/';
}

struct DdsLevel {
    uint32_t width = 0, height = 0, pitch = 0, rows = 0;
    uint64_t offset = 0;
    uint32_t size = 0;
};

struct Dds {
    std::string path;
    uint32_t format = 0, width = 0, height = 0;
    std::vector<DdsLevel> levels;
};

bool LoadDds(const std::string& path, Dds* output, std::string* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { *error = "cannot open DDS"; return false; }
    std::streamoff end = input.tellg();
    if (end < 128 || end > std::streamoff(kMaxDdsBytes)) { *error = "invalid DDS size"; return false; }
    std::array<uint8_t, 128> bytes{};
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!input) { *error = "cannot read DDS header"; return false; }
    if (memcmp(bytes.data(), "DDS ", 4) != 0 || Read32(bytes.data() + 4) != 124 ||
        Read32(bytes.data() + 76) != 32) { *error = "invalid DDS header"; return false; }
    uint32_t height = Read32(bytes.data() + 12), width = Read32(bytes.data() + 16);
    uint32_t depth = Read32(bytes.data() + 24), count = std::max(1u, Read32(bytes.data() + 28));
    uint32_t pf_flags = Read32(bytes.data() + 80), fourcc = Read32(bytes.data() + 84);
    uint32_t caps2 = Read32(bytes.data() + 112);
    if (caps2 != 0 || depth > 1 || count > 32) { *error = "only 2D DDS textures are supported"; return false; }
    uint32_t format = 0;
    if (pf_flags & 0x4) {
        if (fourcc == FourCC('D', 'X', 'T', '1') || fourcc == FourCC('D', 'X', 'T', '3') ||
            fourcc == FourCC('D', 'X', 'T', '5')) format = fourcc;
    } else if ((pf_flags & 0x40) && Read32(bytes.data() + 88) == 32 &&
               Read32(bytes.data() + 92) == 0x00ff0000 && Read32(bytes.data() + 96) == 0x0000ff00 &&
               Read32(bytes.data() + 100) == 0x000000ff) {
        uint32_t alpha = Read32(bytes.data() + 104);
        if (alpha == 0xff000000) format = kA8R8G8B8;
        else if (alpha == 0) format = kX8R8G8B8;
    }
    Dds parsed;
    parsed.path = path;
    parsed.format = format;
    parsed.width = width;
    parsed.height = height;
    uint64_t offset = 128;
    for (uint32_t level = 0; level < count; ++level) {
        DdsLevel item;
        item.width = level < 32 ? std::max(1u, width >> level) : 1;
        item.height = level < 32 ? std::max(1u, height >> level) : 1;
        if (!Layout(format, item.width, item.height, &item.pitch, &item.rows)) {
            *error = "unsupported DDS format or dimensions";
            return false;
        }
        uint64_t size = uint64_t(item.pitch) * item.rows;
        if (!size || size > kMaxDdsBytes || offset + size > static_cast<uint64_t>(end)) {
            *error = "truncated DDS mip chain";
            return false;
        }
        item.offset = offset;
        item.size = static_cast<uint32_t>(size);
        offset += size;
        parsed.levels.push_back(item);
    }
    *output = std::move(parsed);
    return true;
}

// Only the requested mip is temporary. Large packs do not accumulate a second permanent copy of every DDS payload.
bool ReadDdsLevel(const Dds& dds, uint32_t level, uint32_t destination_pitch, void* destination) {
    if (!destination || level >= dds.levels.size()) return false;
    const DdsLevel& source = dds.levels[level];
    if (!source.size || destination_pitch < source.pitch) return false;
    try {
        std::vector<uint8_t> bytes(source.size);
        std::ifstream input(dds.path, std::ios::binary);
        if (!input) return false;
        input.seekg(static_cast<std::streamoff>(source.offset));
        input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        if (!input) return false;
        auto* output = static_cast<uint8_t*>(destination);
        for (uint32_t row = 0; row < source.rows; ++row) {
            memcpy(output + uint64_t(row) * destination_pitch, bytes.data() + uint64_t(row) * source.pitch,
                   source.pitch);
        }
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

constexpr size_t kMaxTextureMappings = 65536;

struct Replacement {
    std::string path;
    bool attempted = false;
    bool valid = false;
    Dds dds;
};

struct TextureState {
    bool identified = false;
    uint32_t format = 0, width = 0, height = 0;
    std::string key;
    Replacement* replacement = nullptr;
    bool logged = false;
};

std::mutex g_mutex;
std::unordered_map<std::string, Replacement> g_replacements;
std::unordered_set<void*> g_tracked;
std::unordered_map<void*, TextureState> g_states;
std::unordered_set<std::string> g_dumped;
bool g_enabled = false;
bool g_dump = false;
std::string g_dump_dir;

void LoadMappings(const std::vector<ModEntry>& mods) {
    for (const ModEntry& mod : mods) {
        if (mod.textures.empty()) continue;
        std::ifstream input(mod.textures);
        if (!input) {
            Log("mods: cannot read texture mapping %s", mod.textures.c_str());
            continue;
        }
        size_t slash = mod.textures.rfind('/');
        std::string parent = slash == std::string::npos ? "." : mod.textures.substr(0, slash);
        std::string line, section;
        unsigned number = 0, accepted = 0;
        while (std::getline(input, line)) {
            ++number;
            line = Trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            if (line.front() == '[' && line.back() == ']') {
                section = Lower(Trim(line.substr(1, line.size() - 2)));
                continue;
            }
            if (!section.empty() && section != "hashes") continue;
            size_t equals = line.find('=');
            if (equals == std::string::npos) {
                Log("mods: %s:%u invalid texture mapping", mod.textures.c_str(), number);
                continue;
            }
            std::string key = Lower(Trim(line.substr(0, equals)));
            std::string relative = Trim(line.substr(equals + 1));
            if (key.size() != 64 || !std::all_of(key.begin(), key.end(), [](char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) || (c >= 'a' && c <= 'f');
                }) || !SafeTexturePath(relative)) {
                Log("mods: %s:%u invalid texture key/path", mod.textures.c_str(), number);
                continue;
            }
            if (g_replacements.size() >= kMaxTextureMappings) {
                Log("mods: global texture mapping limit reached");
                return;
            }
            std::string path = parent + "/" + relative;
            struct stat st{};
            if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || uint64_t(st.st_size) > kMaxDdsBytes ||
                !Within(mod.root, path)) {
                Log("mods: unsafe or missing texture replacement %s", path.c_str());
                continue;
            }
            if (accepted >= kMaxTextureMappings) {
                Log("mods: %s texture mapping limit reached", mod.id.c_str());
                break;
            }
            Replacement replacement;
            replacement.path = path;
            if (g_replacements.emplace(key, std::move(replacement)).second) ++accepted;
        }
        Log("mods: %s texture mappings=%u", mod.id.c_str(), accepted);
    }
}

void DumpOriginal(const std::string& key, uint32_t format, uint32_t width, uint32_t height, const uint8_t* data,
                  uint32_t pitch, uint32_t tight_pitch, uint32_t rows) {
    mkdir(g_dump_dir.c_str(), 0700);
    std::string path = g_dump_dir + "/" + key + ".dds";
    struct stat existing{};
    if (stat(path.c_str(), &existing) == 0) return;  // never overwrite an author's edited dump
    std::array<uint8_t, 128> header{};
    memcpy(header.data(), "DDS ", 4);
    Put32(header.data() + 4, 124);
    bool compressed = format == FourCC('D', 'X', 'T', '1') || format == FourCC('D', 'X', 'T', '3') ||
                      format == FourCC('D', 'X', 'T', '5');
    Put32(header.data() + 8, 0x1 | 0x2 | 0x4 | 0x1000 | (compressed ? 0x80000 : 0x8));
    Put32(header.data() + 12, height);
    Put32(header.data() + 16, width);
    Put32(header.data() + 20, compressed ? tight_pitch * rows : tight_pitch);
    Put32(header.data() + 28, 1);
    Put32(header.data() + 76, 32);
    if (compressed) {
        Put32(header.data() + 80, 0x4);
        Put32(header.data() + 84, format);
    } else {
        Put32(header.data() + 80, format == kA8R8G8B8 ? 0x41 : 0x40);
        Put32(header.data() + 88, 32);
        Put32(header.data() + 92, 0x00ff0000);
        Put32(header.data() + 96, 0x0000ff00);
        Put32(header.data() + 100, 0x000000ff);
        if (format == kA8R8G8B8) Put32(header.data() + 104, 0xff000000);
    }
    Put32(header.data() + 108, 0x1000);
    std::string temporary = path + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(header.data()), header.size());
    for (uint32_t row = 0; row < rows; ++row) {
        output.write(reinterpret_cast<const char*>(data + uint64_t(row) * pitch), tight_pitch);
    }
    output.close();
    if (!output || rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        Log("mods: texture dump failed for %s", key.c_str());
        return;
    }
    std::string mapping = g_dump_dir + "/textures.ini";
    bool present = false;
    std::ifstream existing_mapping(mapping);
    std::string line;
    while (std::getline(existing_mapping, line)) {
        if (line.compare(0, key.size(), key) == 0 && line.find('=') != std::string::npos) {
            present = true;
            break;
        }
    }
    if (!present) {
        std::ofstream append(mapping, std::ios::app);
        append << key << " = " << key << ".dds\n";
    }
}

}  // namespace

void ModTexturesInitialize(const std::vector<ModEntry>& mods) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_replacements.clear();
    g_tracked.clear();
    g_states.clear();
    g_dumped.clear();
    g_dump = SettingInt("dump_textures", 0) != 0;
    g_dump_dir = ModsRoot() + "/../texture-dump";
    LoadMappings(mods);
    g_enabled = g_dump || !g_replacements.empty();
    Log("mods: texture replacement %s, %zu key(s), dumping=%d", g_enabled ? "enabled" : "disabled",
        g_replacements.size(), g_dump ? 1 : 0);
}

bool ModTexturesEnabled() { return g_enabled; }

void ModTexturesTrack(void* host, bool eligible) {
    if (!g_enabled || !eligible || !host) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_tracked.insert(host);
}

void ModTexturesForget(void* host) {
    if (!g_enabled) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_tracked.erase(host);
    g_states.erase(host);
}

void ModTexturesReplace(void* host, uint32_t level, uint32_t format, uint32_t width, uint32_t height,
                        uint32_t pitch, void* data, bool write_back) {
    if (!g_enabled || !host || !data) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_tracked.find(host) == g_tracked.end()) return;
    uint32_t tight_pitch = 0, rows = 0;
    if (!Layout(format, width, height, &tight_pitch, &rows) || pitch < tight_pitch ||
        uint64_t(pitch) * rows > kMaxDdsBytes) return;
    const auto* pixels = static_cast<const uint8_t*>(data);
    TextureState& state = g_states[host];
    if (!state.identified) {
        if (level != 0) return;
        state.identified = true;
        state.format = format;
        state.width = width;
        state.height = height;
        state.key = TextureKey(format, width, height, pixels, pitch, tight_pitch, rows);
        auto found = g_replacements.find(state.key);
        if (found != g_replacements.end()) state.replacement = &found->second;
        if (g_dump && g_dumped.insert(state.key).second) {
            DumpOriginal(state.key, format, width, height, pixels, pitch, tight_pitch, rows);
        }
    }
    Replacement* replacement = state.replacement;
    if (!replacement || !write_back) return;
    if (!replacement->attempted) {
        replacement->attempted = true;
        std::string error;
        replacement->valid = LoadDds(replacement->path, &replacement->dds, &error) &&
                             replacement->dds.format == state.format && replacement->dds.width == state.width &&
                             replacement->dds.height == state.height;
        if (!replacement->valid) {
            if (error.empty()) error = "format or base dimensions do not match the original";
            Log("mods: texture %s rejected: %s", replacement->path.c_str(), error.c_str());
            state.replacement = nullptr;
            return;
        }
    }
    if (!replacement->valid || level >= replacement->dds.levels.size()) return;
    const DdsLevel& source = replacement->dds.levels[level];
    uint32_t expected_width = level < 32 ? std::max(1u, state.width >> level) : 1;
    uint32_t expected_height = level < 32 ? std::max(1u, state.height >> level) : 1;
    if (source.width != expected_width || source.height != expected_height || pitch < source.pitch) return;
    if (!ReadDdsLevel(replacement->dds, level, pitch, data)) {
        replacement->valid = false;
        state.replacement = nullptr;
        Log("mods: texture %s read failed after validation", replacement->path.c_str());
        return;
    }
    if (!state.logged) {
        state.logged = true;
        Log("mods: texture %s replaced with %s", state.key.c_str(), replacement->path.c_str());
    }
}

}  // namespace native
