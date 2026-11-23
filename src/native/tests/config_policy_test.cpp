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
    Check(native::kDefaultFpsLimit == 30, "the first-run engine FPS cap must default to 30");
    Check(native::ApplyBatterySaverFpsLimit(native::kDefaultFpsLimit, false) == 30,
          "the 30 FPS default changed without Battery saver");
    Check(native::ApplyBatterySaverFpsLimit(0, false) == 0,
          "an explicit Off selection was not preserved");
    Check(native::ApplyBatterySaverFpsLimit(60, false) == 60,
          "an explicit higher FPS cap was not preserved");
    Check(native::ApplyBatterySaverFpsLimit(0, true) == 30,
          "Battery saver did not cap an uncapped request at 30 FPS");
    Check(native::ApplyBatterySaverFpsLimit(60, true) == 30,
          "Battery saver did not cap a higher request at 30 FPS");
    Check(native::ApplyBatterySaverFpsLimit(30, true) == 30,
          "Battery saver changed an already-30-FPS setting");

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
