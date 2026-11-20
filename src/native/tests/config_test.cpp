// protodroid.ini precedence: the launcher writes the app's external-files file, a user keeps hand-written keys in
// /storage/emulated/0/Prototype/protodroid.ini. Loading only the first file that existed made every launcher engine
// option silently inert on such a device, so the loader now merges every file in priority order and the first file to
// define a key wins. These checks pin that rule (and the parse details that go with it).
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "guest.h"

namespace native {

void Log(const char*, ...) {}
[[noreturn]] void Fatal(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    abort();
}
int ParseIniText(const std::string& text, std::map<std::string, std::string>& out, int* shadowed);

// Defined by main.cpp in the real build; only Candidates() reads them, and the test drives the parser directly.
const char* g_game_dir = nullptr;
const char* g_data_dir = nullptr;

}  // namespace native

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "config test: %s\n", message);
        abort();
    }
}

using native::ParseIniText;

}  // namespace

int main() {
    // The launcher's file is read first: its keys are authoritative.
    std::map<std::string, std::string> settings;
    int shadowed = 0;
    int taken = ParseIniText("; written by the launcher\n"
                             "render_height = 720\n"
                             "[graphics]\n"
                             "fps_limit = 60 ; a trailing comment\n"
                             "texture_skip_mips = 1\n",
                             settings, &shadowed);
    Check(taken == 3, "launcher file did not yield three keys");
    Check(shadowed == 0, "launcher file reported shadowed keys");
    Check(settings["render_height"] == "720", "launcher render_height was not stored");
    Check(settings["fps_limit"] == "60", "trailing comment was not stripped");
    Check(settings["texture_skip_mips"] == "1", "launcher texture_skip_mips was not stored");

    // The shared-storage file must not override a launcher key, but must still supply keys the launcher does not own.
    taken = ParseIniText("sync_futex = 0\n"
                         "render_height = 540\n",
                         settings, &shadowed);
    Check(taken == 1, "shared file did not contribute exactly its own key");
    Check(shadowed == 1, "shared file overriding a launcher key was not reported as shadowed");
    Check(settings["render_height"] == "720", "shared file overrode the launcher's render_height");
    Check(settings["sync_futex"] == "0", "shared file's own key was dropped");

    // Suffix and case handling: keys are lower-cased and trimmed; junk lines are ignored.
    std::map<std::string, std::string> odd;
    shadowed = 0;
    taken = ParseIniText("  Unlock_Dev_Menu  =  On  \r\n"
                         "not a key value line\n"
                         "= 3\n"
                         "\n"
                         "unlock_dev_menu = Off\n",
                         odd, &shadowed);
    Check(taken == 1, "case-folded duplicate key was stored twice");
    Check(shadowed == 1, "case-folded duplicate key was not counted");
    Check(odd["unlock_dev_menu"] == "On", "case-folded key kept the wrong value");

    puts("protodroid.ini precedence checks passed");
    return 0;
}
