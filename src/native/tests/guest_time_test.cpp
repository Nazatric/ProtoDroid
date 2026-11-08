#include "guest_time.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "guest-time test failed: %s\n", message);
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace native;
    static_assert(kDiagnosticTourGuestClockScale == 4, "the deliberate tour scale changed");
    Check(ScaleGuestElapsedNanos(0, false) == 0, "normal Play changed a zero clock");
    Check(ScaleGuestElapsedNanos(123456789, false) == 123456789, "normal Play scaled real time");
    Check(ScaleGuestElapsedNanos(UINT64_MAX, false) == UINT64_MAX, "normal Play clamped its clock");
    Check(ScaleGuestElapsedNanos(0, true) == 0, "tour clock did not preserve its epoch");
    Check(ScaleGuestElapsedNanos(1000000000ull, true) == 4000000000ull, "tour clock is not 4x");
    Check(ScaleGuestElapsedNanos(1000, true) / 100 == 40, "QPC frequency/counter units diverged");
    Check(ScaleGuestElapsedNanos(1000000, true) / 1000000 == 4, "millisecond timer units diverged");
    const uint64_t limit = std::numeric_limits<uint64_t>::max() / kDiagnosticTourGuestClockScale;
    Check(ScaleGuestElapsedNanos(limit, true) == limit * kDiagnosticTourGuestClockScale,
          "boundary multiplication overflowed");
    Check(ScaleGuestElapsedNanos(limit + 1, true) == UINT64_MAX, "overflow did not saturate");
    Check(ScaleGuestElapsedNanos(UINT64_MAX, true) == UINT64_MAX, "maximum input did not saturate");
    SetGuestClockDiagnosticTour(false);
    Check(GuestElapsedNanos(123456789) == 123456789, "default ordinary entry scaled its timer");
    SetGuestClockDiagnosticTour(true);
    Check(GuestElapsedNanos(123456789) == 493827156, "tour entry did not scale its timer");
    SetGuestClockDiagnosticTour(false);
    Check(GuestElapsedNanos(123456789) == 123456789, "a later ordinary activity inherited the tour clock");
    std::puts("guest-time tests passed (normal 1x, tour 4x, entry reset, QPC/ms units, overflow saturation)");
    return 0;
}
