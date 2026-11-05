// Opt-in records for recompiled functions selected at generation time. No probe is generated for ordinary functions.
#include "guest_function_trace.h"

#include <atomic>
#include <cinttypes>

#include "guest.h"
#include "timing_window.h"

namespace native {
namespace {

// A process-wide cap prevents an unexpectedly frequent selected function from filling the normal ProtoDroid log.
// Records beyond it are explicitly truncated rather than presented as a complete capture.
constexpr uint64_t kMaxLoggedRecords = 4096;
std::atomic<uint64_t> g_next_record{0};

bool Enabled() {
    static const bool enabled = SettingInt("guest_function_trace", 0) != 0;
    return enabled;
}

GuestFunctionTrace& ThreadTrace() {
    thread_local GuestFunctionTrace trace;
    return trace;
}

bool ReserveRecord(uint64_t* sequence) {
    const uint64_t ticket = g_next_record.fetch_add(1, std::memory_order_relaxed);
    if (ticket >= kMaxLoggedRecords) {
        if (ticket == kMaxLoggedRecords) {
            Log("guest-function-v1 event=cap suppressed_records_at_least=1");
        }
        return false;
    }
    *sequence = ticket + 1;
    return true;
}

void Emit(const GuestFunctionTraceEvent& event) {
    uint64_t sequence = 0;
    if (!ReserveRecord(&sequence)) return;
    const GuestFunctionTraceEntry& entry = event.entry;
    const char* kind = "unknown";
    switch (event.kind) {
        case GuestFunctionTraceEvent::Kind::kComplete: kind = "complete"; break;
        case GuestFunctionTraceEvent::Kind::kOverflowEnter: kind = "overflow-enter"; break;
        case GuestFunctionTraceEvent::Kind::kOverflowExit: kind = "overflow-exit"; break;
        case GuestFunctionTraceEvent::Kind::kMismatch: kind = "mismatch"; break;
    }
    Log("guest-function-v1 event=%s sequence=%" PRIu64 " rva=%08x start_ns=%" PRIu64
        " end_ns=%" PRIu64 " duration_ns=%" PRIu64 " depth=%u discarded_inner=%u"
        " clock_regression=%u ecx=%08x caller=%08x arg0=%08x arg1=%08x arg2=%08x",
        kind, sequence, entry.rva, event.start_ns, event.end_ns, event.duration_ns, event.depth,
        event.discarded_inner, event.clock_regression ? 1u : 0u, entry.ecx, entry.caller,
        entry.arguments[0], entry.arguments[1], entry.arguments[2]);
}

void Flush(GuestFunctionTrace* trace) {
    for (size_t i = 0; i < trace->event_count(); ++i) Emit(trace->event(i));
    if (const uint64_t dropped = trace->dropped_events()) {
        uint64_t sequence = 0;
        if (ReserveRecord(&sequence)) {
            Log("guest-function-v1 event=drop sequence=%" PRIu64 " dropped_events=%" PRIu64,
                sequence, dropped);
        }
    }
    trace->ClearEvents();
}

}  // namespace
}  // namespace native

extern "C" void RecompGuestFunctionEnter(Ctx* context, uint32_t rva) {
    using namespace native;
    if (!Enabled()) return;

    GuestFunctionTraceEntry entry;
    entry.rva = rva;
    entry.ecx = context->ecx;
    entry.caller = SR32(context->esp);
    for (size_t i = 0; i < entry.arguments.size(); ++i) {
        entry.arguments[i] = Arg(context, static_cast<int>(i));
    }
    GuestFunctionTrace& trace = ThreadTrace();
    // Take the timestamp after raw argument capture. A small fixed collector/timestamp cost remains, but neither
    // logging nor persistent file I/O occurs inside this entry probe.
    trace.Enter(entry, static_cast<uint64_t>(MonotonicNanos()));
}

extern "C" void RecompGuestFunctionExit(Ctx* context, uint32_t rva) {
    using namespace native;
    (void)context;
    if (!Enabled()) return;

    GuestFunctionTrace& trace = ThreadTrace();
    trace.Exit(rva, static_cast<uint64_t>(MonotonicNanos()));
    // In particular, do not flush an inner completion while its traced parent is still running: Log() flushes the
    // persistent ProtoDroid log and would otherwise be included in the parent's inclusive duration.
    if (trace.quiescent()) Flush(&trace);
}
