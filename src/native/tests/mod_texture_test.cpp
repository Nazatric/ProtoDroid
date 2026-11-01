// Host tests for the production keyed-DDS implementation. Including the source gives access to its private key/layout
// helpers while the public ModTextures* path is exercised exactly as D3D9 uses it. No Android/DXVK runtime is needed.
#include "../mod_textures.cpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace native {
// Unused platform services required by the included translation unit.
void Log(const char*, ...) {}
std::string Setting(const char*, const char* fallback) { return fallback ? fallback : ""; }
int SettingInt(const char*, int fallback) { return fallback; }
const std::string& ModsRoot() {
    static const std::string root = "/tmp/protodroid-mod-texture-test";
    return root;
}
}  // namespace native

namespace {

int checks = 0;

void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::string Hex(const std::array<uint8_t, 32>& digest) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(64, '0');
    for (size_t i = 0; i < digest.size(); ++i) {
        result[i * 2] = digits[digest[i] >> 4];
        result[i * 2 + 1] = digits[digest[i] & 15];
    }
    return result;
}

void Write(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Check(output.good(), "write fixture");
}

void Put(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    native::Put32(bytes.data() + offset, value);
}

std::vector<uint8_t> MakeDds(uint32_t format, uint32_t width, uint32_t height, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> result(128 + payload.size());
    memcpy(result.data(), "DDS ", 4);
    Put(result, 4, 124);
    Put(result, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000);
    Put(result, 12, height);
    Put(result, 16, width);
    Put(result, 28, 1);
    Put(result, 76, 32);
    Put(result, 80, 4);
    Put(result, 84, format);
    Put(result, 108, 0x1000);
    memcpy(result.data() + 128, payload.data(), payload.size());
    return result;
}

native::ModEntry Pack(const std::string& root, const std::string& id, const std::string& key,
                      const std::vector<uint8_t>& dds, int priority) {
    std::string directory = root + "/" + id;
    Check(mkdir(directory.c_str(), 0700) == 0, "create pack directory");
    Write(directory + "/replacement.dds", dds);
    std::ofstream mapping(directory + "/textures.ini");
    mapping << "[hashes]\n" << key << " = replacement.dds\n";
    mapping.close();
    native::ModEntry mod;
    mod.id = id;
    mod.root = directory;
    mod.textures = directory + "/textures.ini";
    mod.priority = priority;
    return mod;
}

}  // namespace

int main() {
    using namespace native;
    Check(!ModTexturesEnabled(), "texture work is disabled by default");
    {
        Sha256 hash;
        Check(Hex(hash.Finish()) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA-256 empty vector");
    }
    {
        Sha256 hash;
        hash.Update("abc", 3);
        Check(Hex(hash.Finish()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA-256 abc vector");
    }

    constexpr uint32_t dxt1 = FourCC('D', 'X', 'T', '1');
    uint8_t original[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const std::string key = TextureKey(dxt1, 4, 4, original, 8, 8, 1);
    Check(key == "a29ed29c8d80a10358e62bdf60abd5e7f767472c9ebad26c2a098aa8c787c710",
          "exact versioned texture key");
    uint8_t padded[16];
    memset(padded, 0xee, sizeof(padded));
    memcpy(padded, original, 8);
    Check(TextureKey(dxt1, 4, 4, padded, 16, 8, 1) == key, "pitch padding excluded from key");
    uint32_t pitch = 0, rows = 0;
    Check(Layout(dxt1, 5, 5, &pitch, &rows) && pitch == 16 && rows == 2, "DXT1 block layout");
    Check(Layout(FourCC('D', 'X', 'T', '5'), 5, 5, &pitch, &rows) && pitch == 32 && rows == 2,
          "DXT5 block layout");
    Check(Layout(kA8R8G8B8, 5, 5, &pitch, &rows) && pitch == 20 && rows == 5, "RGBA layout");
    Check(!Layout(0, 4, 4, &pitch, &rows), "unknown format rejected");

    char temporary[] = "/tmp/protodroid-mod-texture-XXXXXX";
    Check(mkdtemp(temporary) != nullptr, "temporary directory");
    std::string root = temporary;
    std::vector<uint8_t> high_payload = {8, 9, 10, 11, 12, 13, 14, 15};
    std::vector<uint8_t> low_payload = {16, 17, 18, 19, 20, 21, 22, 23};
    ModEntry high = Pack(root, "high", key, MakeDds(dxt1, 4, 4, high_payload), 200);
    ModEntry low = Pack(root, "low", key, MakeDds(dxt1, 4, 4, low_payload), 100);
    std::vector<ModEntry> mods = {high, low};  // production ModEntries() is already priority-sorted
    ModTexturesInitialize(mods);
    Check(ModTexturesEnabled(), "valid texture mapping enables replacement work");

    int first_texture = 0;
    ModTexturesTrack(&first_texture, true);
    uint8_t destination[8];
    memcpy(destination, original, 8);
    ModTexturesReplace(&first_texture, 0, dxt1, 4, 4, 8, destination, true);
    Check(memcmp(destination, high_payload.data(), 8) == 0, "higher-priority replacement applied");
    ModTexturesForget(&first_texture);
    memcpy(destination, original, 8);
    ModTexturesReplace(&first_texture, 0, dxt1, 4, 4, 8, destination, true);
    Check(memcmp(destination, original, 8) == 0, "released texture is no longer tracked");

    int second_texture = 0;
    ModTexturesTrack(&second_texture, true);
    memcpy(destination, original, 8);
    ModTexturesReplace(&second_texture, 0, dxt1, 4, 4, 8, destination, false);
    Check(memcmp(destination, original, 8) == 0, "read-only lock never writes replacement");

    int third_texture = 0;
    ModTexturesTrack(&third_texture, true);
    ModTexturesReplace(&third_texture, 0, dxt1, 4, 4, 16, padded, true);
    Check(memcmp(padded, high_payload.data(), 8) == 0, "replacement expands to original pitch");
    for (size_t i = 8; i < sizeof(padded); ++i) Check(padded[i] == 0xee, "pitch padding preserved");

    Dds parsed;
    std::string error;
    Check(LoadDds(high.root + "/replacement.dds", &parsed, &error) && parsed.levels.size() == 1 &&
              parsed.levels[0].offset == 128 && parsed.levels[0].size == high_payload.size(),
          "valid DDS header and lazy level offset parsed");
    uint8_t lazy[8] = {};
    Check(ReadDdsLevel(parsed, 0, 8, lazy) && memcmp(lazy, high_payload.data(), 8) == 0,
          "lazy level read returns the expected payload");
    Write(root + "/truncated.dds", std::vector<uint8_t>(128));
    Check(!LoadDds(root + "/truncated.dds", &parsed, &error) && !error.empty(), "truncated DDS rejected");

    // Runtime dimension mismatch must preserve original data even when a mapping has the right content key.
    ModEntry mismatch = Pack(root, "mismatch", key, MakeDds(dxt1, 8, 8, std::vector<uint8_t>(32)), 300);
    ModTexturesInitialize(std::vector<ModEntry>{mismatch});
    int fourth_texture = 0;
    ModTexturesTrack(&fourth_texture, true);
    memcpy(destination, original, 8);
    ModTexturesReplace(&fourth_texture, 0, dxt1, 4, 4, 8, destination, true);
    Check(memcmp(destination, original, 8) == 0, "mismatched replacement preserves original");
    ModTexturesInitialize(std::vector<ModEntry>{});
    Check(!ModTexturesEnabled(), "empty catalog restores the disabled fast path");

    std::string cleanup = "rm -rf -- '" + root + "'";
    Check(std::system(cleanup.c_str()) == 0, "cleanup");
    std::cout << "mod texture tests passed: " << checks << " checks\n";
    return 0;
}
