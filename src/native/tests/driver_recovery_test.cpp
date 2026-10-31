#include "driver_recovery.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "driver-recovery test failed: %s\n", message);
        std::exit(1);
    }
}

}  // namespace

int main() {
    char directory[] = "/tmp/protodroid-driver-recovery-XXXXXX";
    Check(mkdtemp(directory) != nullptr, "could not create temporary directory");
    const std::string marker = std::string(directory) + "/turnip_failed";

    Check(native::ReadDriverFailureMarker(marker.c_str(), "build-a").empty(), "missing marker was accepted");
    Check(native::WriteDriverFailureMarker(marker.c_str(), "build-a", "D3D9 CreateDevice failed"),
          "could not record an initialization failure");
    Check(native::ReadDriverFailureMarker(marker.c_str(), "build-a") == "D3D9 CreateDevice failed",
          "same-build initialization failure was not retained");
    Check(native::ReadDriverFailureMarker(marker.c_str(), "build-b").empty(),
          "a different build was blocked by a stale failure");

    Check(native::WriteDriverFailureMarker(marker.c_str(), "build-b", "replacement driver failed"),
          "could not replace the previous failure marker");
    Check(native::ReadDriverFailureMarker(marker.c_str(), "build-b") == "replacement driver failed",
          "replacement failure was not retained");
    Check(native::ReadDriverFailureMarker(marker.c_str(), "build-a").empty(),
          "overwritten build identity was not updated");

    const std::string invalid = std::string(directory) + "/missing/marker";
    Check(!native::WriteDriverFailureMarker(invalid.c_str(), "build-b", "failure"),
          "an unwritable marker incorrectly promised recovery");
    Check(unlink(marker.c_str()) == 0 && rmdir(directory) == 0, "temporary cleanup failed");
    std::puts("driver-recovery marker tests passed");
    return 0;
}
