#include "config_policy.h"

#include <cstdio>
#include <cstdlib>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "config policy test failed: %s\n", message);
        std::exit(1);
    }
}

}  // namespace

int main() {
    native::ConfigSettings effective;
    native::MergeConfigLayer(effective, {{"sync_futex", "0"}, {"unlock_dev_menu", "0"}, {"fps_limit", "60"}});
    native::MergeConfigLayer(effective, {{"unlock_dev_menu", "1"}});

    Check(effective.at("unlock_dev_menu") == "1",
          "launcher developer-menu choice did not override the shared configuration");
    Check(effective.at("sync_futex") == "0" && effective.at("fps_limit") == "60",
          "app-specific settings discarded unrelated base configuration values");

    native::MergeConfigLayer(effective, {{"unlock_dev_menu", "0"}});
    Check(effective.at("unlock_dev_menu") == "0", "an explicit Off choice did not override an older On value");

    std::puts("config policy tests passed");
    return 0;
}
