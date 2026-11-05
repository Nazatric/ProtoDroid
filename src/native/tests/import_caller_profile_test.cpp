#include "import_caller_profile.h"

#include <cstdlib>
#include <iostream>

using native::ImportCallerProfile;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::string GoldenLine() {
    ImportCallerProfile profile;
    profile.Add(9, 0x008a6151, 40);
    profile.Add(9, 0x008a6161, 30);
    profile.Add(9, 0x008a6171, 20);
    profile.Add(9, 0xffffffffu, 10);
    const auto lines = native::BuildImportCallerLines(profile, 7,
        {{9, "IDirect3DDevice9::DrawIndexedPrimitive", 100, 4}});
    Check(lines.size() == 1, "golden formatter line count");
    return lines[0];
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--emit-golden") {
        std::cout << GoldenLine() << "\n";
        return 0;
    }
    Check(argc == 1, "unknown test argument");
    ImportCallerProfile profile;
    profile.Add(1, 0x100, 100);
    profile.Add(1, 0x200, 50);
    profile.Add(1, 0x100, 25);
    profile.Add(2, 0x100, 70);
    profile.Add(3, 0xffffffffu, 9);
    Check(profile.entries().size() == 4, "import/caller keys were not independent");
    Check(profile.entries()[0].total_ns == 125 && profile.entries()[0].calls == 2,
          "same caller did not accumulate");
    Check(profile.entries()[1].caller_rva == 0x200 && profile.entries()[1].total_ns == 50,
          "second caller was lost");
    Check(profile.entries()[2].import_index == 2 && profile.entries()[2].total_ns == 70,
          "same RVA crossed import buckets");
    Check(profile.entries()[3].caller_rva == 0xffffffffu && profile.entries()[3].calls == 1,
          "external caller marker was not retained");

    profile.Reset();
    for (uint32_t i = 0; i < ImportCallerProfile::kMaxEntries; ++i)
        profile.Add(7, i, 1);
    Check(profile.entries().size() == ImportCallerProfile::kMaxEntries, "entry cap was not reached");
    profile.Add(7, 0, 2);  // Existing keys must still update after the cap.
    profile.Add(7, 0x5000, 10);
    profile.Add(7, 0x5000, 10);
    profile.Add(8, 0x5001, 7);
    Check(profile.entries().size() == ImportCallerProfile::kMaxEntries, "entry cap grew");
    const auto overflow7 = profile.overflow(7);
    const auto overflow8 = profile.overflow(8);
    Check(overflow7.calls == 2 && overflow7.total_ns == 20, "import 7 overflow accounting");
    Check(overflow8.calls == 1 && overflow8.total_ns == 7, "import 8 overflow accounting");
    Check(profile.overflow(9).calls == 0 && profile.overflow(9999).total_ns == 0,
          "missing overflow bucket was not zero");
    uint64_t import7_calls = overflow7.calls;
    for (const auto& entry : profile.entries()) {
        if (entry.import_index == 7) import7_calls += entry.calls;
    }
    Check(import7_calls == ImportCallerProfile::kMaxEntries + 3,
          "overflow or post-cap update lost calls");

    profile.Reset();
    Check(profile.entries().empty() && profile.overflow(7).calls == 0, "window reset retained data");
    profile.Add(9, 0x12345678, 44);
    Check(profile.entries().size() == 1 && profile.entries()[0].caller_rva == 0x12345678 &&
              profile.entries()[0].total_ns == 44 && profile.entries()[0].calls == 1,
          "profile did not accept entries after reset");
    Check(GoldenLine() == "import-callers tid 7 method IDirect3DDevice9::DrawIndexedPrimitive "
                          "total_ns=100 total_calls=4 caller=008a6151:40:1 caller=008a6161:30:1 "
                          "caller=008a6171:20:1 unlisted_ns=10 unlisted_calls=1",
          "golden top-three/residual format");
    std::puts("Import caller-profile tests passed (independence, cap, overflow, post-cap updates, reset, golden format)");
    return 0;
}
