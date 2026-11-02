#include "smart_controls.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace {

using native::SmartSpecialContext;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "smart controls test failed: %s\n", message);
        std::exit(1);
    }
}

void Decode(bool condition, uint8_t expected, uint8_t message, SmartSpecialContext wanted,
            const char* description) {
    Check(native::DecodeConsumeProbe(true, condition, expected, message) == wanted, description);
    Check(native::DecodeConsumeProbe(false, condition, expected, message) == SmartSpecialContext::Unknown,
          "unresolved object must ignore the message/condition combination");
}

void PlayerGate() {
    Check(native::SmartPlayerHandleMatches(0x00020001u, 0x00020001u), "matching valid player handle rejected");
    Check(!native::SmartPlayerHandleMatches(0x00020002u, 0x00020001u), "non-player handle accepted");
    Check(!native::SmartPlayerHandleMatches(0, 0), "null player handle accepted");
    Check(!native::SmartPlayerHandleMatches(0x0000ffffu, 0x0000ffffu), "invalid player index accepted");
    Check(!native::SmartPlayerHandleMatches(0x1234ffffu, 0x1234ffffu),
          "invalid generated player index accepted");
}

void ProbeReduction() {
    // Ordinary positive condition: condition result equals the actual validity message.
    Decode(false, 1, 0, SmartSpecialContext::Special, "positive condition, no valid consume target");
    Decode(true, 1, 1, SmartSpecialContext::Consume, "positive condition, valid consume target");
    // Negated condition: a false condition result can mean that the actual message is true.
    Decode(true, 0, 0, SmartSpecialContext::Special, "negated condition, no valid consume target");
    Decode(false, 0, 1, SmartSpecialContext::Consume, "negated condition must not invert valid consume");
    // Every inconsistent result is rejected, including combinations that naïvely look like Consume.
    Decode(true, 1, 0, SmartSpecialContext::Unknown, "inconsistent positive false message");
    Decode(false, 1, 1, SmartSpecialContext::Unknown, "inconsistent positive true message");
    Decode(false, 0, 0, SmartSpecialContext::Unknown, "inconsistent negated false message");
    Decode(true, 0, 1, SmartSpecialContext::Unknown, "inconsistent negated true message");
    Check(native::DecodeConsumeProbe(true, true, 2, 1) == SmartSpecialContext::Unknown,
          "non-boolean expected value rejected");
    Check(native::DecodeConsumeProbe(true, true, 1, 2) == SmartSpecialContext::Unknown,
          "non-boolean message value rejected");
}

void SnapshotPacking() {
    constexpr int64_t second = 1000000000;
    Check(native::EncodeSmartContext(SmartSpecialContext::Unknown, second) == 0, "unknown must have no evidence");
    Check(native::EncodeSmartContext(SmartSpecialContext::Consume, -1) == 0, "negative sample time rejected");
    Check(native::EncodeSmartContext(static_cast<SmartSpecialContext>(3), second) == 0,
          "reserved context cannot be encoded");
    for (SmartSpecialContext context : {SmartSpecialContext::Special, SmartSpecialContext::Consume}) {
        const uint64_t packed = native::EncodeSmartContext(context, second);
        Check(native::DecodeSmartContext(packed, second) == context, "fresh snapshot round trip");
        Check(native::DecodeSmartContext(packed, second + native::kSmartContextFreshNs) == context,
              "freshness boundary must be inclusive");
        Check(native::DecodeSmartContext(packed, second + native::kSmartContextFreshNs + 1000000)
                  == SmartSpecialContext::Unknown,
              "stale context must become unknown");
        Check(native::DecodeSmartContext(packed, second - 1000000) == SmartSpecialContext::Unknown,
              "future-dated context must fail closed");
    }
    const uint64_t zero_time = native::EncodeSmartContext(SmartSpecialContext::Consume, 0);
    Check(native::DecodeSmartContext(zero_time, 0) == SmartSpecialContext::Consume,
          "zero monotonic origin remains representable");
    Check(native::DecodeSmartContext(0, second) == SmartSpecialContext::Unknown, "empty snapshot is unknown");
    Check(native::DecodeSmartContext(3, second) == SmartSpecialContext::Unknown, "reserved snapshot is unknown");
    Check(native::DecodeSmartContext(zero_time, -1) == SmartSpecialContext::Unknown,
          "negative snapshot clock is unknown");
    Check(native::DecodeSmartContext(std::numeric_limits<uint64_t>::max(), second)
              == SmartSpecialContext::Unknown,
          "overflowing/reserved packed value is unknown");
    Check(native::DecodeSmartContext(std::numeric_limits<uint64_t>::max() - 2, second)
              == SmartSpecialContext::Unknown,
          "overflowing Special timestamp is unknown");
    const uint64_t max_time = native::EncodeSmartContext(SmartSpecialContext::Consume,
                                                         std::numeric_limits<int64_t>::max());
    Check(native::DecodeSmartContext(max_time, std::numeric_limits<int64_t>::max())
              == SmartSpecialContext::Consume,
          "largest valid monotonic sample round trip");
}

}  // namespace

int main() {
    PlayerGate();
    ProbeReduction();
    SnapshotPacking();
    std::puts("smart controls tests passed: player gate, consume reduction, negation, consistency, freshness");
    return 0;
}
