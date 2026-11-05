#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "timing_window.h"

namespace native {

// Diagnostics-thread persistence for the raw Present samples already collected by PresentStatsTracker. The producer
// path is unchanged: no Present call opens, writes, flushes, or allocates for this trace. Rows are a valid prefix if
// the process dies or the byte cap is reached; the native log reports caps/errors and the CSV uses comments for
// metadata/trailers. Monotonic timestamps are per-device and are not wall-clock time or GPU timestamps.
class FrameIntervalTrace {
public:
    static constexpr uint64_t kDefaultMaxBytes = 64ull * 1024 * 1024;

    FrameIntervalTrace(bool enabled, const std::string& path, uint64_t max_bytes = kDefaultMaxBytes)
        : path_(path), max_bytes_(max_bytes) {
        if (!enabled) return;
        if (path.empty()) {
            error_ = "empty output path";
            return;
        }
        const std::string previous = path + ".prev";
        if (std::rename(path.c_str(), previous.c_str()) != 0 && errno != ENOENT) {
            error_ = std::string("rotation failed: ") + std::strerror(errno);
            return;  // Never truncate the existing capture if its previous-generation rotation failed.
        }
        file_ = std::fopen(path.c_str(), "wb");
        if (!file_) {
            error_ = std::strerror(errno);
            return;
        }
        const char metadata[] = "# protodroid_frame_intervals_v1; completed Present-to-Present intervals; not GPU time\n";
        const char columns[] = "sequence,present_end_ns,interval_ns\n";
        WriteRaw(metadata, sizeof(metadata) - 1);
        WriteRaw(columns, sizeof(columns) - 1);
        if (std::fflush(file_) != 0) error_ = "initial flush failed";
    }

    ~FrameIntervalTrace() { Close("worker_exit"); }
    FrameIntervalTrace(const FrameIntervalTrace&) = delete;
    FrameIntervalTrace& operator=(const FrameIntervalTrace&) = delete;

    bool enabled() const { return file_ != nullptr; }
    bool capped() const { return capped_; }
    uint64_t rows() const { return rows_; }
    uint64_t bytes() const { return bytes_; }
    const std::string& error() const { return error_; }
    const std::string& path() const { return path_; }

    void Write(const TimingWindowSnapshot& snapshot) {
        if (!file_ || capped_ || !error_.empty() || snapshot.samples.empty()) return;
        if (snapshot.count != snapshot.samples.size() || snapshot.begin_ns < 0 ||
            snapshot.end_ns < snapshot.begin_ns ||
            snapshot.total_ns != uint64_t(snapshot.end_ns - snapshot.begin_ns)) {
            error_ = "inconsistent interval window";
            return;
        }

        int64_t at_ns = snapshot.begin_ns;
        for (int64_t duration_ns : snapshot.samples) {
            if (duration_ns < 0 || at_ns > std::numeric_limits<int64_t>::max() - duration_ns) {
                error_ = "invalid interval sample";
                return;
            }
            at_ns += duration_ns;
            char row[96];
            const int size = std::snprintf(row, sizeof(row), "%llu,%lld,%lld\n",
                                           (unsigned long long)rows_, (long long)at_ns, (long long)duration_ns);
            if (size <= 0 || size_t(size) >= sizeof(row)) {
                error_ = "row formatting failed";
                return;
            }
            if (!WriteRaw(row, size_t(size))) return;
            ++rows_;
        }
        if (at_ns != snapshot.end_ns) {
            error_ = "interval timestamp mismatch";
            return;
        }
        // One flush per diagnostics window, never per frame. No fsync is used in the measurement path.
        if (std::fflush(file_) != 0) error_ = "window flush failed";
    }

    void Close(const char* reason) {
        if (!file_) return;
        if (!capped_ && error_.empty()) {
            const std::string trailer = "# closed=" + std::string(reason ? reason : "unknown") +
                                        " rows=" + std::to_string(rows_) + "\n";
            WriteRaw(trailer.data(), trailer.size());
        }
        if (std::fflush(file_) != 0 && error_.empty()) error_ = "final flush failed";
        if (std::fclose(file_) != 0 && error_.empty()) error_ = "close failed";
        file_ = nullptr;
    }

private:
    bool WriteRaw(const char* data, size_t size) {
        if (!file_ || capped_ || !error_.empty()) return false;
        if (size > max_bytes_ || bytes_ > max_bytes_ - size) {
            capped_ = true;
            return false;
        }
        if (std::fwrite(data, 1, size, file_) != size) {
            error_ = "write failed";
            return false;
        }
        bytes_ += size;
        return true;
    }

    std::string path_;
    uint64_t max_bytes_ = kDefaultMaxBytes;
    FILE* file_ = nullptr;
    uint64_t rows_ = 0;
    uint64_t bytes_ = 0;
    bool capped_ = false;
    std::string error_;
};

}  // namespace native
