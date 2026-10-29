#include "dxvk_state_cache_io.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "state-cache test failed: %s\n", message);
        std::exit(1);
    }
}

struct Header {
    uint32_t type;
    uint32_t size;
};
struct Hash {
    uint8_t bytes[4];
};

struct FakeStream {
    std::vector<std::string> writes;
    int flushes = 0;

    void write(const char* data, size_t size) { writes.emplace_back(data, size); }
    void flush() { ++flushes; }
};

template<typename T>
std::string Bytes(const T& value) {
    return std::string(reinterpret_cast<const char*>(&value), sizeof(value));
}

void TestIncrementalEntryFlushAndLayout() {
    const Header header = {7, 5};
    const Hash hash = {{0x91, 0x82, 0x73, 0x64}};
    const char payload[] = {'c', 'a', 'c', 'h', 'e'};
    FakeStream stream;

    dxvk::writeStateCacheEntryPayload(stream, header, hash, payload, sizeof(payload));
    Check(stream.flushes == 1, "one entry did not produce exactly one incremental flush");
    Check(stream.writes.size() == 3, "entry layout does not contain header, hash, and payload");
    Check(stream.writes[0] == Bytes(header), "entry header bytes changed");
    Check(stream.writes[1] == Bytes(hash), "entry hash bytes changed");
    Check(stream.writes[2] == std::string(payload, sizeof(payload)), "entry payload bytes changed");

    dxvk::writeStateCacheEntryPayload(stream, header, hash, payload, sizeof(payload));
    Check(stream.flushes == 2 && stream.writes.size() == 6,
          "second entry was buffered instead of flushed incrementally");
}

void TestWriterDrainPolicy() {
    Check(dxvk::stateCacheWriterShouldStop(true, true), "empty stopped writer did not exit");
    Check(!dxvk::stateCacheWriterShouldStop(false, true), "stop request discarded queued cache entries");
    Check(!dxvk::stateCacheWriterShouldStop(true, false), "idle writer exited without a stop request");
    Check(!dxvk::stateCacheWriterShouldStop(false, false), "active writer exited unexpectedly");
}

}  // namespace

int main() {
    TestIncrementalEntryFlushAndLayout();
    TestWriterDrainPolicy();
    std::puts("state-cache tests passed (entry layout, per-entry flush, shutdown drain)");
    return 0;
}
