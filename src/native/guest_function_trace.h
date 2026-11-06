#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace native {

// Raw state captured at a generated function's entry. arguments are the first three stack dwords after the guest
// return address; their meaning is function-specific. Keeping them opaque avoids assigning unproven signatures.
struct GuestFunctionTraceEntry {
    uint32_t rva = 0;
    uint32_t ecx = 0;
    uint32_t caller = 0;
    std::array<uint32_t, 3> arguments{};
};

struct GuestFunctionTraceEvent {
    enum class Kind {
        kComplete,
        kOverflowEnter,
        kOverflowExit,
        kMismatch,
    };

    Kind kind = Kind::kComplete;
    GuestFunctionTraceEntry entry{};
    uint64_t start_ns = 0;
    uint64_t end_ns = 0;
    uint64_t duration_ns = 0;
    uint32_t depth = 0;
    uint32_t discarded_inner = 0;
    bool clock_regression = false;
};

// Fixed-storage, per-host-thread accounting for generation-time-selected recompiled functions. Entry and exit do
// no logging, allocation, or locking. Completed events are retained until the outermost selected call exits, so a
// nested event's file I/O cannot inflate its parent's measured duration. The runtime drains events only when
// quiescent() is true. An active call is intentionally not persisted: a crash cannot produce a fake completion.
class GuestFunctionTrace {
public:
    static constexpr size_t kMaxActive = 16;
    static constexpr size_t kMaxEvents = 64;

    void Enter(const GuestFunctionTraceEntry& entry, uint64_t now_ns) {
        if (depth_ == kMaxActive) {
            ++overflow_;
            GuestFunctionTraceEvent event;
            event.kind = GuestFunctionTraceEvent::Kind::kOverflowEnter;
            event.entry = entry;
            event.start_ns = now_ns;
            event.depth = static_cast<uint32_t>(depth_);
            Push(event);
            return;
        }
        frames_[depth_] = Frame{entry, now_ns};
        ++depth_;
    }

    void Exit(uint32_t rva, uint64_t now_ns) {
        if (overflow_ != 0) {
            --overflow_;
            GuestFunctionTraceEvent event;
            event.kind = GuestFunctionTraceEvent::Kind::kOverflowExit;
            event.entry.rva = rva;
            event.end_ns = now_ns;
            event.depth = static_cast<uint32_t>(depth_);
            Push(event);
            return;
        }
        if (depth_ == 0) {
            GuestFunctionTraceEvent event;
            event.kind = GuestFunctionTraceEvent::Kind::kMismatch;
            event.entry.rva = rva;
            event.end_ns = now_ns;
            Push(event);
            return;
        }

        size_t match = depth_;
        while (match != 0 && frames_[match - 1].entry.rva != rva) --match;
        if (match == 0) {
            GuestFunctionTraceEvent event;
            event.kind = GuestFunctionTraceEvent::Kind::kMismatch;
            event.entry.rva = rva;
            event.end_ns = now_ns;
            event.discarded_inner = static_cast<uint32_t>(depth_);
            depth_ = 0;
            Push(event);
            return;
        }

        const size_t index = match - 1;
        const Frame frame = frames_[index];
        const uint32_t discarded = static_cast<uint32_t>(depth_ - index - 1);
        depth_ = index;

        GuestFunctionTraceEvent event;
        event.kind = GuestFunctionTraceEvent::Kind::kComplete;
        event.entry = frame.entry;
        event.start_ns = frame.start_ns;
        event.end_ns = now_ns;
        event.depth = static_cast<uint32_t>(depth_);
        event.discarded_inner = discarded;
        if (now_ns >= frame.start_ns) {
            event.duration_ns = now_ns - frame.start_ns;
        } else {
            event.clock_regression = true;
        }
        Push(event);
    }

    bool quiescent() const { return depth_ == 0 && overflow_ == 0; }
    size_t active_depth() const { return depth_; }
    size_t overflow_depth() const { return overflow_; }
    size_t event_count() const { return event_count_; }
    uint64_t dropped_events() const { return dropped_events_; }
    const GuestFunctionTraceEvent& event(size_t index) const { return events_[index]; }

    // Called by the runtime only after draining a quiescent thread's records. Active frames are not cleared.
    void ClearEvents() {
        event_count_ = 0;
        dropped_events_ = 0;
    }

private:
    struct Frame {
        GuestFunctionTraceEntry entry{};
        uint64_t start_ns = 0;
    };

    void Push(const GuestFunctionTraceEvent& event) {
        if (event_count_ == kMaxEvents) {
            ++dropped_events_;
            return;
        }
        events_[event_count_++] = event;
    }

    std::array<Frame, kMaxActive> frames_{};
    std::array<GuestFunctionTraceEvent, kMaxEvents> events_{};
    size_t depth_ = 0;
    size_t overflow_ = 0;
    size_t event_count_ = 0;
    uint64_t dropped_events_ = 0;
};

}  // namespace native
