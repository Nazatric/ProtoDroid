#include "frame_interval_trace.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

native::TimingWindowSnapshot Window(int64_t begin, std::initializer_list<int64_t> samples) {
    native::TimingWindowSnapshot out;
    out.begin_ns = begin;
    out.samples.assign(samples);
    out.count = out.samples.size();
    for (int64_t sample : out.samples) out.total_ns += uint64_t(sample);
    out.end_ns = begin + int64_t(out.total_ns);
    return out;
}
}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("protodroid-frame-trace-" + std::to_string(getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / "proto-frame-intervals.csv";
    {
        std::ofstream(path) << "previous-run\n";
        native::FrameIntervalTrace trace(true, path.string(), 4096);
        Check(trace.enabled() && trace.error().empty(), "trace did not open");
        Check(Read(path.string() + ".prev") == "previous-run\n", "previous trace was not rotated");
        trace.Write(Window(1000, {30, 10, 20}));
        Check(trace.rows() == 3 && trace.error().empty(), "first raw window failed");
        const std::string flushed = Read(path);
        Check(flushed.find("0,1030,30\n1,1040,10\n2,1060,20\n") != std::string::npos,
              "raw rows were not chronological or flushed per window");
        trace.Write(Window(1060, {25, 15}));
        Check(trace.rows() == 5, "second raw window failed");
        trace.Close("test");
        Check(!trace.enabled() && trace.error().empty(), "trace close failed");
    }
    const std::string output = Read(path);
    Check(output.find("# protodroid_frame_intervals_v1") == 0, "missing format/header");
    Check(output.find("sequence,present_end_ns,interval_ns\n") != std::string::npos, "missing columns");
    Check(output.find("3,1085,25\n4,1100,15\n") != std::string::npos, "sequence did not continue");
    Check(output.find("# closed=test rows=5\n") != std::string::npos, "missing final trailer");

    native::FrameIntervalTrace disabled(false, "");
    Check(!disabled.enabled() && disabled.error().empty(), "disabled trace attempted I/O");

    const auto guarded_path = root / "guarded.csv";
    {
        std::ofstream(guarded_path) << "keep-me\n";
        std::filesystem::create_directory(guarded_path.string() + ".prev");
        native::FrameIntervalTrace guarded(true, guarded_path.string(), 4096);
        Check(!guarded.enabled() && guarded.error().find("rotation failed") == 0,
              "failed rotation did not disable the trace");
    }
    Check(Read(guarded_path) == "keep-me\n", "failed rotation truncated the previous capture");

    const auto invalid_path = root / "invalid.csv";
    native::FrameIntervalTrace invalid(true, invalid_path.string(), 4096);
    auto bad = Window(1000, {10, 20});
    bad.count = 3;
    invalid.Write(bad);
    Check(invalid.rows() == 0 && invalid.error() == "inconsistent interval window",
          "inconsistent window was serialized");

    const auto measured_path = root / "measured.csv";
    native::FrameIntervalTrace measured(true, measured_path.string(), 4096);
    measured.Write(Window(1000, {30, 10, 20}));
    const uint64_t full_size = measured.bytes();
    measured.Close("measure");

    const auto capped_path = root / "capped.csv";
    native::FrameIntervalTrace capped(true, capped_path.string(), full_size - 1);
    capped.Write(Window(1000, {30, 10, 20}));
    Check(capped.capped() && capped.rows() == 2 && capped.error().empty(),
          "byte cap did not preserve an exact row prefix");
    capped.Close("cap");
    const std::string capped_output = Read(capped_path);
    Check(capped_output.find("1,1040,10\n") != std::string::npos &&
              capped_output.find("2,1060,20\n") == std::string::npos,
          "capped output is not a valid prefix");

    std::filesystem::remove_all(root);
    std::puts("frame interval trace tests passed (rotation, raw order, timestamps, residual, cap, invalid input)");
    return 0;
}
