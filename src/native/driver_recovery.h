// Persistent recovery marker shared by the Android Vulkan selector and host tests. The write path deliberately uses
// only open/write/close-style operations so the existing fatal-signal handler can retain its small crash-time path.
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace native {

inline bool WriteDriverFailurePart(int fd, const char* data, size_t size) {
    while (size) {
        ssize_t written = write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        data += written;
        size -= size_t(written);
    }
    return true;
}

inline bool WriteDriverFailureMarker(const char* path, const char* build, const char* reason) {
    if (!path || !build || !reason) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    const bool ok = WriteDriverFailurePart(fd, build, strlen(build))
            && WriteDriverFailurePart(fd, "\n", 1)
            && WriteDriverFailurePart(fd, reason, strlen(reason))
            && WriteDriverFailurePart(fd, "\n", 1);
    return close(fd) == 0 && ok;
}

// A marker belongs only to the exact native build that wrote it. This preserves the existing behavior of retrying a
// driver after an update instead of letting an old failure disable it forever.
inline std::string ReadDriverFailureMarker(const char* path, const char* build) {
    if (!path || !build) return "";
    FILE* file = fopen(path, "r");
    if (!file) return "";
    char markerBuild[64] = {}, reason[256] = {};
    const bool readBuild = fgets(markerBuild, sizeof(markerBuild), file) != nullptr;
    const bool readReason = fgets(reason, sizeof(reason), file) != nullptr;
    fclose(file);
    if (!readBuild || !readReason) return "";
    markerBuild[strcspn(markerBuild, "\r\n")] = 0;
    reason[strcspn(reason, "\r\n")] = 0;
    if (strcmp(markerBuild, build) != 0) return "";
    return reason[0] ? reason : "unknown";
}

}  // namespace native
