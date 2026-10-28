#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace native::bink {

// Stateless bounded input. FFmpeg's AVIO wrapper owns the cursor, so every read is checked against this logical size.
class Source {
public:
    virtual ~Source() = default;
    virtual int64_t Size() const = 0;
    virtual int64_t Read(uint64_t offset, void* dst, size_t bytes) = 0;
    virtual std::string Description() const = 0;
    virtual void Service() {}
    virtual void ControlBackgroundIO(uint32_t) {}
};

// Resolves a guest Windows filename to loose media, a once-per-process RCF index, or a kernel BINKFILEHANDLE.
// Both ordinary BIK/SMUSH entries and optional RZ-compressed entries are supported. Compressed entries are expanded
// once at open and all subsequent FFmpeg reads stay inside the resulting bounded memory buffer.
std::unique_ptr<Source> OpenFileSource(const char* guest_name, uint32_t open_flags, std::string* error);
std::unique_ptr<Source> OpenCallbackSource(uint32_t io_open, const char* guest_name, uint32_t open_flags,
                                          uint32_t io_size, bool simulate, std::string* error);
std::unique_ptr<Source> OpenMemorySource(const char* guest_address, uint32_t open_flags, std::string* error);

}  // namespace native::bink
