#include "bink_media.h"

#include "bink_abi.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <limits>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "guest.h"
#include "win.h"

namespace native::bink {
namespace {

constexpr uint64_t kMaxDirectoryBytes = 512ull << 20;
constexpr uint64_t kMaxExpandedMovie = 1024ull << 20;

uint32_t LE32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t LE64(const uint8_t* p) { return uint64_t(LE32(p)) | (uint64_t(LE32(p + 4)) << 32); }

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Normalize(std::string_view input) {
    std::string s(input);
    std::replace(s.begin(), s.end(), '\\', '/');
    bool drive = s.size() >= 2 && s[1] == ':';
    if (drive) s.erase(0, 2);
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    if (drive && s.size() >= 5 && Lower(s.substr(0, 5)) == "game/") s.erase(0, 5);
    std::vector<std::string> parts;
    size_t at = 0;
    while (at <= s.size()) {
        size_t end = s.find('/', at);
        if (end == std::string::npos) end = s.size();
        std::string part = Lower(s.substr(at, end - at));
        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (!parts.empty()) parts.pop_back();
            } else {
                parts.push_back(std::move(part));
            }
        }
        if (end == s.size()) break;
        at = end + 1;
    }
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out.push_back('/');
        out += p;
    }
    return out;
}

bool PreadExact(int fd, void* dst, size_t bytes, uint64_t offset) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    size_t done = 0;
    while (done < bytes) {
        if (offset + done > std::numeric_limits<off_t>::max()) return false;
        ssize_t n = pread(fd, out + done, bytes - done, static_cast<off_t>(offset + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

class FileSource final : public Source {
public:
    FileSource(int fd, uint64_t base, uint64_t size, std::string description)
        : fd_(fd), base_(base), size_(size), description_(std::move(description)) {}
    ~FileSource() override { if (fd_ >= 0) close(fd_); }
    int64_t Size() const override { return size_ > uint64_t(std::numeric_limits<int64_t>::max()) ? -1 : int64_t(size_); }
    int64_t Read(uint64_t offset, void* dst, size_t bytes) override {
        if (offset >= size_ || !bytes) return 0;
        bytes = static_cast<size_t>(std::min<uint64_t>(bytes, size_ - offset));
        if (base_ + offset > std::numeric_limits<off_t>::max()) return -1;
        ssize_t n;
        do {
            n = pread(fd_, dst, bytes, static_cast<off_t>(base_ + offset));
        } while (n < 0 && errno == EINTR);
        return n;
    }
    std::string Description() const override { return description_; }
private:
    int fd_ = -1;
    uint64_t base_ = 0, size_ = 0;
    std::string description_;
};

class MemorySource final : public Source {
public:
    explicit MemorySource(std::vector<uint8_t> bytes, std::string description)
        : owned_(std::move(bytes)), data_(owned_.data()), size_(owned_.size()), description_(std::move(description)) {}
    MemorySource(const uint8_t* data, uint64_t size, std::string description)
        : data_(data), size_(size), description_(std::move(description)) {}
    int64_t Size() const override { return size_ > uint64_t(std::numeric_limits<int64_t>::max()) ? -1 : int64_t(size_); }
    int64_t Read(uint64_t offset, void* dst, size_t bytes) override {
        if (offset >= size_ || !bytes) return 0;
        bytes = static_cast<size_t>(std::min<uint64_t>(bytes, size_ - offset));
        memcpy(dst, data_ + offset, bytes);
        return static_cast<int64_t>(bytes);
    }
    std::string Description() const override { return description_; }
private:
    std::vector<uint8_t> owned_;
    const uint8_t* data_ = nullptr;
    uint64_t size_ = 0;
    std::string description_;
};

// BinkSetIO installs a guest BINKIOOPEN. The opener fills a 0x144-byte BINKIO whose first seven fields are
// stdcall callbacks. All callback destinations and the optional aligned IO buffer therefore live in guest memory.
class CallbackSource final : public Source {
public:
    CallbackSource(uint32_t open, const char* name, uint32_t flags, uint32_t io_size, bool simulate)
        : open_(open), flags_(flags), capacity_(std::clamp(io_size, 4096u, 1024u * 1024u)),
          simulate_(simulate), description_(std::string("guest BINKIO ") + (name ? name : "")) {
        if (name) name_ = name;
    }
    ~CallbackSource() override {
        if (io_ && opened_) {
            uint32_t close = R32(io_ + 0x14);
            if (close) GuestCall(close, {io_});
        }
        if (buffer_raw_) HeapFree(buffer_raw_);
        if (read_buffer_) HeapFree(read_buffer_);
        if (io_) HeapFree(io_);
    }

    bool Open(std::string* error) {
        io_ = HeapAlloc(0x144);
        read_buffer_ = HeapAlloc(capacity_);
        if (!io_ || !read_buffer_) {
            if (error) *error = "could not allocate guest BINKIO state";
            return false;
        }
        memset(Ptr<void>(io_), 0, 0x144);
        uint32_t guest_name = GuestStrdup(name_.c_str());
        if (!guest_name) {
            if (error) *error = "could not copy the custom-I/O movie name";
            return false;
        }
        uint32_t result = GuestCall(open_, {io_, guest_name, flags_});
        HeapFree(guest_name);
        if (!result) {
            if (error) *error = "guest BINKIOOPEN rejected the movie";
            return false;
        }
        opened_ = true;
        read_header_ = R32(io_);
        if (!read_header_) {
            if (error) *error = "guest BINKIO has no ReadHeader callback";
            return false;
        }

        // Memory input and custom input both expose the embedded BIK directly. SMUSH media is scanned on the same
        // 512-byte boundaries as the original input path; subsequent callback offsets include this prefix.
        uint8_t header[48] = {};
        for (uint32_t offset = 0; offset <= (64u << 10); offset += 512) {
            if (ReadAbsolute(offset, header, sizeof(header)) != int64_t(sizeof(header))) break;
            if (header[0] == 'B' && header[1] == 'I' && header[2] == 'K') {
                media_offset_ = offset;
                break;
            }
        }
        if (header[0] != 'B' || header[1] != 'I' || header[2] != 'K') {
            if (error) *error = "custom I/O did not provide BIK/SMUSH media";
            return false;
        }
        media_size_ = uint64_t(LE32(header + 4)) + 8;
        if (media_size_ < sizeof(header) || media_size_ > kMaxExpandedMovie ||
            media_offset_ + media_size_ > 0x80000000ull) {
            if (error) *error = "custom-I/O BIK size is invalid or too large";
            return false;
        }

        uint32_t get_buffer_size = R32(io_ + 8);
        uint32_t set_info = R32(io_ + 0x0c);
        uint32_t buffer_size = capacity_;
        if (get_buffer_size) {
            uint32_t requested = GuestCall(get_buffer_size, {io_, capacity_});
            if (requested) buffer_size = requested;
        }
        if (buffer_size < 4096 || buffer_size > (16u << 20)) {
            if (error) *error = "guest BINKIO requested an invalid buffer size";
            return false;
        }
        if (set_info) {
            buffer_raw_ = HeapAlloc(buffer_size + 0x4000);
            if (!buffer_raw_) {
                if (error) *error = "could not allocate the guest BINKIO buffer";
                return false;
            }
            uint32_t aligned = (buffer_raw_ + 0x3fffu) & ~0x3fffu;
            GuestCall(set_info, {io_, aligned, buffer_size, uint32_t(media_offset_ + media_size_), simulate_});
        }
        return true;
    }

    int64_t Size() const override { return static_cast<int64_t>(media_size_); }
    int64_t Read(uint64_t offset, void* dst, size_t bytes) override {
        if (offset >= media_size_ || !bytes) return 0;
        bytes = static_cast<size_t>(std::min<uint64_t>(bytes, media_size_ - offset));
        return ReadAbsolute(media_offset_ + offset, dst, bytes);
    }
    std::string Description() const override { return description_; }
    void Service() override {
        if (opened_) {
            uint32_t idle = R32(io_ + 0x10);
            if (idle) GuestCall(idle, {io_});
        }
    }
    void ControlBackgroundIO(uint32_t control) override {
        if (opened_) {
            uint32_t bg = R32(io_ + 0x18);
            if (bg) GuestCall(bg, {io_, control});
        }
    }

private:
    int64_t ReadAbsolute(uint64_t offset, void* dst, size_t bytes) {
        auto* out = static_cast<uint8_t*>(dst);
        size_t total = 0;
        while (total < bytes) {
            if (offset + total > 0x7fffffffu) return total ? static_cast<int64_t>(total) : -1;
            // The guest's 16 KiB ReadHeader path is a synchronous positioned read. ReadFrame is different: the
            // game's implementation drives RAD's own background frame-cache state machine and can wait for a Bink
            // service thread when used as a generic FFmpeg AVIO callback. FFmpeg reads are not frame-attributed, so
            // keep them on ReadHeader and cap each request below the guest's internal 16 KiB buffering threshold.
            uint32_t chunk = static_cast<uint32_t>(std::min<size_t>({capacity_, size_t(16 * 1024), bytes - total}));
            uint32_t file_offset = uint32_t(offset + total);
            uint32_t got = GuestCall(read_header_, {io_, file_offset, read_buffer_, chunk});
            if (got > chunk) return total ? static_cast<int64_t>(total) : -1;
            if (!got) {
                if (R32(io_ + 0x20) && !total) return -1;
                break;
            }
            memcpy(out + total, Ptr<void>(read_buffer_), got);
            total += got;
            if (got < chunk) break;
        }
        return static_cast<int64_t>(total);
    }

    uint32_t open_ = 0, flags_ = 0, capacity_ = 0, simulate_ = 0;
    uint32_t io_ = 0, read_buffer_ = 0, buffer_raw_ = 0, read_header_ = 0;
    uint32_t media_offset_ = 0;
    uint64_t media_size_ = 0;
    bool opened_ = false;
    std::string name_, description_;
};

std::unique_ptr<Source> FileSlice(const std::string& path, uint64_t base, uint64_t size, std::string description,
                                  std::string* error) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (error) *error = strerror(errno);
        return nullptr;
    }
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size < 0 || base > uint64_t(st.st_size) || size > uint64_t(st.st_size) - base) {
        if (error) *error = "media slice lies beyond its backing file";
        close(fd);
        return nullptr;
    }
    return std::make_unique<FileSource>(fd, base, size, std::move(description));
}

bool LooksLikeBik(const uint8_t* p, size_t n) {
    return (n >= 3 && p[0] == 'B' && p[1] == 'I' && p[2] == 'K') ||
           (n >= 4 && p[0] == 'S' && p[1] == 'M' && p[2] == 'U' && p[3] == 'S');
}

bool ReadPrefix(Source& source, uint8_t* out, size_t bytes) {
    return source.Size() >= 0 && uint64_t(source.Size()) >= bytes && source.Read(0, out, bytes) == int64_t(bytes);
}

std::unique_ptr<Source> ExpandRZ(Source& source, std::string description, std::string* error) {
    uint8_t header[16];
    if (!ReadPrefix(source, header, sizeof(header)) || memcmp(header, "RZ\0\0", 4) != 0) {
        if (error) *error = "invalid RZ header";
        return nullptr;
    }
    uint64_t uncompressed = LE64(header + 8);
    uint64_t compressed = source.Size() > 0 ? uint64_t(source.Size()) : 0;
    if (uncompressed < 16 || uncompressed > kMaxExpandedMovie || compressed < 16 || compressed - 16 > 0x7fffffff) {
        if (error) *error = "RZ media size is invalid or too large";
        return nullptr;
    }
    std::vector<uint8_t> packed(static_cast<size_t>(compressed - 16));
    if (source.Read(16, packed.data(), packed.size()) != int64_t(packed.size())) {
        if (error) *error = "short read in compressed movie";
        return nullptr;
    }
    std::vector<uint8_t> plain(static_cast<size_t>(uncompressed));
    uLongf out_size = static_cast<uLongf>(plain.size());
    int zr = uncompress(plain.data(), &out_size, packed.data(), static_cast<uLong>(packed.size()));
    if (zr != Z_OK || out_size != plain.size()) {
        if (error) *error = "zlib could not expand RZ movie";
        return nullptr;
    }
    if (!LooksLikeBik(plain.data(), std::min<size_t>(plain.size(), 4))) {
        if (error) *error = "RZ entry does not contain BIK/SMUSH media";
        return nullptr;
    }
    return std::make_unique<MemorySource>(std::move(plain), description + " [RZ]");
}

// Accept direct BIK/SMUSH data and transparently expand RCF's optional RZ wrapper.
std::unique_ptr<Source> FinishSource(std::unique_ptr<Source> source, std::string* error) {
    if (!source) return nullptr;
    uint8_t prefix[16] = {};
    size_t n = source->Size() >= 0 ? static_cast<size_t>(std::min<int64_t>(16, source->Size())) : 0;
    if (n < 4 || source->Read(0, prefix, n) != int64_t(n)) {
        if (error) *error = "movie header is truncated";
        return nullptr;
    }
    if (!memcmp(prefix, "RZ\0\0", 4)) return ExpandRZ(*source, source->Description(), error);
    if (!LooksLikeBik(prefix, n)) {
        if (error) *error = "file is not BIK or SMUSH media";
        return nullptr;
    }
    return source;
}

struct ArchiveEntry {
    std::string archive;
    std::string name;
    uint32_t offset = 0, size = 0;
    int priority = 1;
};

struct ArchiveIndex {
    std::unordered_map<std::string, ArchiveEntry> exact;
    std::unordered_map<std::string, ArchiveEntry> basename;
};
ArchiveIndex g_index;
std::once_flag g_index_once;

uint32_t HashName(std::string name) {
    if (!name.empty() && name.front() == '\\') name.erase(name.begin());
    uint32_t hash = 0;
    for (unsigned char byte : name) {
        int c = byte >= 128 ? int(byte) - 256 : int(byte);
        if (c < 0x61) c += 0x20;
        hash = hash * 31u + static_cast<uint32_t>(c);
    }
    return hash;
}

void IndexArchive(const std::string& path, int priority) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    struct stat st {};
    uint8_t h[0x3c];
    bool valid = fstat(fd, &st) == 0 && st.st_size >= 0x3c && PreadExact(fd, h, sizeof(h), 0) &&
                 memcmp(h, "ATG CORE CEMENT LIBRARY", 24) == 0 && h[0x20] == 2 && h[0x22] == 0 && h[0x23] == 1 &&
                 (h[0x21] == 1 || (h[0x21] == 0 && LE32(h + 0x18) == LE32(h + 0x1c)));
    // RCF 2.0 uses 20-byte wide entries. The leading hash/offset fields are shared with compact 2.1 entries;
    // value_c (+0x0c) is the range size used by CementLibraryFindAndOpen, and value_10 is legacy metadata.
    const uint32_t entry_stride = valid && h[0x21] == 0 ? 20 : 12;
    uint64_t count = valid ? LE32(h + 0x38) : 0;
    if (valid) {
        uint64_t directory = LE32(h + 0x24), directory_size = LE32(h + 0x28);
        uint64_t names_offset = LE32(h + 0x2c), names_size = LE32(h + 0x30);
        uint64_t file_size = static_cast<uint64_t>(st.st_size);
        valid = count <= 2000000 && directory_size == count * entry_stride && directory <= file_size &&
                directory_size <= file_size - directory && names_offset <= file_size &&
                names_size <= file_size - names_offset && names_size <= kMaxDirectoryBytes && names_size >= 8;
    }
    if (valid) {
        std::vector<uint8_t> dirs(static_cast<size_t>(LE32(h + 0x28)));
        std::vector<uint8_t> names(static_cast<size_t>(LE32(h + 0x30)));
        valid = PreadExact(fd, dirs.data(), dirs.size(), LE32(h + 0x24)) &&
                PreadExact(fd, names.data(), names.size(), LE32(h + 0x2c));
        if (valid) {
            struct Item { uint32_t hash, offset, size; std::string name; };
            std::vector<Item> items;
            items.reserve(static_cast<size_t>(count));
            for (uint32_t i = 0; i < count; ++i) {
                const uint8_t* e = dirs.data() + size_t(i) * entry_stride;
                uint64_t offset = LE32(e + 4);
                uint32_t range_size = entry_stride == 20 ? LE32(e + 12) : LE32(e + 8);
                if (offset > uint64_t(st.st_size) || range_size > uint64_t(st.st_size) - offset) { valid = false; break; }
                items.push_back({LE32(e), LE32(e + 4), range_size, {}});
            }
            std::vector<std::string> ordered_names;
            size_t pos = 8;
            for (uint32_t i = 0; valid && i < count; ++i) {
                if (pos > names.size() || names.size() - pos < 16) { valid = false; break; }
                uint32_t length = LE32(names.data() + pos + 12);
                pos += 16;
                if (length > names.size() - pos || names.size() - pos - length < 3) { valid = false; break; }
                std::string name(reinterpret_cast<const char*>(names.data() + pos), length);
                while (!name.empty() && name.back() == '\0') name.pop_back();
                ordered_names.push_back(std::move(name));
                pos += length + 3;
            }
            if (valid && ordered_names.size() == items.size()) {
                std::unordered_map<uint32_t, size_t> by_hash;
                for (size_t i = 0; i < items.size(); ++i) by_hash.emplace(items[i].hash, i);
                bool hashed = true;
                for (size_t i = 0; i < items.size(); ++i) {
                    auto it = by_hash.find(HashName(ordered_names[i]));
                    if (it == by_hash.end()) { hashed = false; break; }
                    items[it->second].name = ordered_names[i];
                }
                if (!hashed) {
                    std::vector<size_t> order(items.size());
                    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
                    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return items[a].offset < items[b].offset; });
                    for (size_t i = 0; i < order.size(); ++i) items[order[i]].name = ordered_names[i];
                }
                for (auto& item : items) {
                    if (item.name.empty()) continue;
                    std::string normalized = Normalize(item.name);
                    std::string base = normalized.substr(normalized.find_last_of('/') + 1);
                    ArchiveEntry entry{path, normalized, item.offset, item.size, priority};
                    g_index.exact.emplace(normalized, entry);
                    g_index.basename.emplace(base, entry);
                }
            }
        }
    }
    if (!valid) Log("[bink] ignoring malformed or unsupported RCF %s\n", path.c_str());
    close(fd);
}

void CollectArchives(const std::string& dir, std::vector<std::pair<std::string, int>>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> dirs, files;
    while (dirent* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string path = dir + "/" + e->d_name;
        struct stat st {};
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) dirs.emplace_back(e->d_name);
        else if (S_ISREG(st.st_mode)) {
            std::string lower = Lower(e->d_name);
            if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".rcf") == 0) {
                int priority = (lower.find("movie") != std::string::npos || lower.find("woi") != std::string::npos) ? 0 : 1;
                files.emplace_back(e->d_name);
                out.emplace_back(path, priority);
            }
        }
    }
    closedir(d);
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return Lower(a) < Lower(b); });
    for (const auto& name : dirs) CollectArchives(dir + "/" + name, out);
}

void BuildIndex() {
    std::vector<std::pair<std::string, int>> archives;
    CollectArchives(g_game_dir ? g_game_dir : ".", archives);
    std::stable_sort(archives.begin(), archives.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second < b.second;
        return Lower(a.first) < Lower(b.first);
    });
    for (const auto& a : archives) IndexArchive(a.first, a.second);
    Log("[bink] indexed %zu RCF paths from %zu archives\n", g_index.exact.size(), archives.size());
}

std::unique_ptr<Source> OpenArchiveSource(std::string_view guest_name, std::string* error) {
    std::call_once(g_index_once, BuildIndex);
    std::string normalized = Normalize(guest_name);
    std::string base = normalized.substr(normalized.find_last_of('/') + 1);
    const ArchiveEntry* found = nullptr;
    auto it = g_index.exact.find(normalized);
    if (it != g_index.exact.end()) found = &it->second;
    if (!found) {
        auto b = g_index.basename.find(base);
        if (b != g_index.basename.end()) found = &b->second;
    }
    if (!found) {
        auto rz = g_index.exact.find(normalized + ".rz");
        if (rz != g_index.exact.end()) found = &rz->second;
        if (!found) {
            auto b = g_index.basename.find(base + ".rz");
            if (b != g_index.basename.end()) found = &b->second;
        }
    }
    if (!found) return nullptr;
    Log("[bink] RCF input %s -> %s:%s +%u (%u bytes)\n", std::string(guest_name).c_str(), found->archive.c_str(),
        found->name.c_str(), found->offset, found->size);
    return FinishSource(FileSlice(found->archive, found->offset, found->size,
                                  found->archive + ":" + found->name, error), error);
}

}  // namespace

std::unique_ptr<Source> OpenFileSource(const char* guest_name, uint32_t open_flags, std::string* error) {
    if (error) error->clear();
    if (open_flags & kFileHandle) {
        uint32_t handle = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(guest_name));
        int fd = DuplicateFileHandle(handle);
        if (fd < 0) {
            if (error) *error = "invalid BINKFILEHANDLE";
            return nullptr;
        }
        struct stat st {};
        if (fstat(fd, &st) != 0 || st.st_size < 0) {
            close(fd);
            if (error) *error = "could not size BINKFILEHANDLE";
            return nullptr;
        }
        return FinishSource(std::make_unique<FileSource>(fd, 0, uint64_t(st.st_size), "guest file handle"), error);
    }
    if (!guest_name || !*guest_name) {
        if (error) *error = "empty movie filename";
        return nullptr;
    }

    // Loose files always win over archives. HostPath performs the same case-insensitive component matching as the
    // rest of the Windows emulation; RCF lookup below additionally normalizes slashes and case in member names.
    std::string loose = HostPath(guest_name);
    struct stat st {};
    if (stat(loose.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        auto source = FinishSource(FileSlice(loose, 0, uint64_t(st.st_size), loose, error), error);
        if (source) return source;
    }
    std::string rz_path = loose + ".rz";
    if (stat(rz_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        auto source = FinishSource(FileSlice(rz_path, 0, uint64_t(st.st_size), rz_path, error), error);
        if (source) return source;
    }
    std::string archive_error;
    if (auto source = OpenArchiveSource(guest_name, &archive_error)) return source;
    if (error) *error = archive_error.empty() ? "movie not found in loose files or RCF archives" : archive_error;
    return nullptr;
}

std::unique_ptr<Source> OpenCallbackSource(uint32_t io_open, const char* guest_name, uint32_t open_flags,
                                          uint32_t io_size, bool simulate, std::string* error) {
    if (error) error->clear();
    if (!io_open || !guest_name || !*guest_name) {
        if (error) *error = "invalid custom BINKIO opener or movie filename";
        return nullptr;
    }
    auto source = std::make_unique<CallbackSource>(io_open, guest_name, open_flags, io_size, simulate);
    if (!source->Open(error)) return nullptr;
    Log("[bink] custom input %s (%lld bytes)\n", guest_name, static_cast<long long>(source->Size()));
    return source;
}

std::unique_ptr<Source> OpenMemorySource(const char* guest_address, uint32_t open_flags, std::string* error) {
    (void)open_flags;
    if (error) error->clear();
    uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(guest_address));
    if (!address) {
        if (error) *error = "null BINKFROMMEMORY address";
        return nullptr;
    }
    if (address > 0xffffffffu - 0x10003u) {
        if (error) *error = "BINKFROMMEMORY header crosses the guest address space";
        return nullptr;
    }
    const uint8_t* p = Ptr<uint8_t>(address);
    // BIK memory input has no length argument in this ABI. Its header supplies the exact byte length, including the
    // four size bytes but excluding the signature. SMUSH files place the BIK header on a later 512-byte boundary.
    for (uint32_t offset = 0; offset <= (64u << 10); offset += 512) {
        const uint8_t* h = p + offset;
        if (h[0] == 'B' && h[1] == 'I' && h[2] == 'K') {
            uint64_t size = uint64_t(LE32(h + 4)) + 8;
            if (size >= 48 && size <= kMaxExpandedMovie && size <= 0x100000000ull - (uint64_t(address) + offset))
                return std::make_unique<MemorySource>(h, size, "guest memory");
            break;
        }
    }
    if (error) *error = "invalid BINKFROMMEMORY header";
    return nullptr;
}

}  // namespace native::bink
