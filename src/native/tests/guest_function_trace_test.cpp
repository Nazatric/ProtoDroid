#include "guest_function_trace.h"

#include <cstdlib>
#include <iostream>

namespace {

using native::GuestFunctionTrace;
using native::GuestFunctionTraceEntry;
using Event = native::GuestFunctionTraceEvent;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

GuestFunctionTraceEntry Entry(uint32_t rva, uint32_t salt = 0) {
    GuestFunctionTraceEntry entry;
    entry.rva = rva;
    entry.ecx = 0x1000 + salt;
    entry.caller = 0x2000 + salt;
    entry.arguments = {0x3000 + salt, 0x4000 + salt, 0x5000 + salt};
    return entry;
}

}  // namespace

int main() {
    {
        GuestFunctionTrace trace;
        trace.Enter(Entry(0x4e5250, 1), 100);
        Check(trace.active_depth() == 1 && !trace.quiescent(), "entry was not active");
        trace.Exit(0x4e5250, 175);
        Check(trace.quiescent() && trace.event_count() == 1, "top-level exit did not become quiescent");
        const Event& event = trace.event(0);
        Check(event.kind == Event::Kind::kComplete, "simple event was not complete");
        Check(event.start_ns == 100 && event.end_ns == 175 && event.duration_ns == 75,
              "simple event timestamps differ");
        Check(event.entry.ecx == 0x1001 && event.entry.caller == 0x2001 &&
                  event.entry.arguments[2] == 0x5001,
              "entry metadata was not retained");
        Check(event.depth == 0 && event.discarded_inner == 0 && !event.clock_regression,
              "simple event flags differ");
    }

    {
        GuestFunctionTrace trace;
        trace.Enter(Entry(0x100, 1), 1000);
        trace.Enter(Entry(0x200, 2), 1010);
        trace.Exit(0x200, 1025);
        Check(!trace.quiescent() && trace.active_depth() == 1 && trace.event_count() == 1,
              "inner exit flushed or changed outer depth");
        Check(trace.event(0).duration_ns == 15 && trace.event(0).depth == 1,
              "inner completion differs");
        trace.Exit(0x100, 1050);
        Check(trace.quiescent() && trace.event_count() == 2, "outer completion missing");
        Check(trace.event(1).entry.rva == 0x100 && trace.event(1).duration_ns == 50 &&
                  trace.event(1).depth == 0,
              "outer completion differs");
    }

    {
        GuestFunctionTrace trace;
        for (uint32_t i = 0; i < GuestFunctionTrace::kMaxActive; ++i) trace.Enter(Entry(i + 1), i);
        trace.Enter(Entry(0xaa), 100);
        Check(trace.active_depth() == GuestFunctionTrace::kMaxActive && trace.overflow_depth() == 1,
              "active-stack overflow was not bounded");
        Check(trace.event_count() == 1 && trace.event(0).kind == Event::Kind::kOverflowEnter,
              "overflow entry was not recorded");
        trace.Exit(0xaa, 110);
        Check(trace.overflow_depth() == 0 && trace.active_depth() == GuestFunctionTrace::kMaxActive,
              "overflow exit consumed a measured frame");
        Check(trace.event(1).kind == Event::Kind::kOverflowExit, "overflow exit missing");
        trace.Exit(0x10, 120);
        Check(trace.active_depth() == GuestFunctionTrace::kMaxActive - 1 &&
                  trace.event(2).kind == Event::Kind::kComplete,
              "measured frame did not resume after overflow");
    }

    {
        GuestFunctionTrace trace;
        trace.Enter(Entry(0x100), 10);
        trace.Enter(Entry(0x200), 20);
        trace.Exit(0x100, 45);
        Check(trace.quiescent() && trace.event_count() == 1, "recovered mismatch did not unwind");
        Check(trace.event(0).kind == Event::Kind::kComplete && trace.event(0).discarded_inner == 1 &&
                  trace.event(0).duration_ns == 35,
              "recovered mismatch evidence differs");
    }

    {
        GuestFunctionTrace trace;
        trace.Enter(Entry(0x100), 10);
        trace.Enter(Entry(0x200), 20);
        trace.Exit(0x300, 30);
        Check(trace.quiescent() && trace.event_count() == 1, "unmatched exit did not reset stale frames");
        Check(trace.event(0).kind == Event::Kind::kMismatch && trace.event(0).discarded_inner == 2,
              "unmatched exit did not report discarded frames");
        trace.Exit(0x400, 40);
        Check(trace.event_count() == 2 && trace.event(1).kind == Event::Kind::kMismatch &&
                  trace.event(1).discarded_inner == 0,
              "empty exit mismatch differs");
    }

    {
        GuestFunctionTrace trace;
        trace.Enter(Entry(0x100), 100);
        trace.Exit(0x100, 99);
        Check(trace.event(0).clock_regression && trace.event(0).duration_ns == 0,
              "clock regression produced a fabricated duration");
    }

    {
        GuestFunctionTrace trace;
        for (size_t i = 0; i < GuestFunctionTrace::kMaxEvents + 3; ++i) trace.Exit(0x500, i);
        Check(trace.event_count() == GuestFunctionTrace::kMaxEvents && trace.dropped_events() == 3,
              "event queue was not bounded with an exact dropped residual");
        trace.ClearEvents();
        Check(trace.event_count() == 0 && trace.dropped_events() == 0 && trace.quiescent(),
              "event drain reset active state or residuals");
        trace.Exit(0x600, 100);
        Check(trace.event_count() == 1 && trace.dropped_events() == 0, "queue did not resume after drain");
    }

    std::cout << "guest function trace tests passed\n";
    return 0;
}
