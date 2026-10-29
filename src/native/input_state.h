#ifndef PROTORE_INPUT_STATE_H
#define PROTORE_INPUT_STATE_H

#include <cstdint>

namespace native {

enum class MenuTouchTransition : uint32_t { None, Down, Up };

struct MenuTouchFrame {
    bool active = false;
    bool moved = false;
    bool left_down = false;
    int32_t x = 0, y = 0;
    MenuTouchTransition transition = MenuTouchTransition::None;
};

struct MenuTouchBufferedEvent {
    uint32_t data = 0, timestamp = 0, sequence = 0;
};

// JNI stores normalized coordinates with a 16-bit fractional fixed-point scale. Conversion happens on the guest polling
// thread, not Android's UI thread, so a concurrent D3D client resize cannot race with touch delivery.
constexpr int32_t kMenuTouchCoordinateScale = 65536;
constexpr int32_t MenuTouchClientCoordinate(int32_t fixed, int32_t extent) {
    if (extent <= 1) return 0;
    if (fixed < 0) fixed = 0;
    if (fixed > kMenuTouchCoordinateScale) fixed = kMenuTouchCoordinateScale;
    int32_t value = int32_t(int64_t(fixed) * extent / kMenuTouchCoordinateScale);
    return value < extent ? value : extent - 1;
}

// DirectInput is polled once per guest frame. A tap deliberately takes at least four polls: absolute hover with the
// button released, two button-down polls, then button-up (or a later up when the finger remains held). The class has
// no host/input dependencies so the exact Scaleform ordering can be exercised by the host UBSAN tests.
class MenuTouchSequencer {
public:
    void Down(int32_t x, int32_t y) {
        if (!active_) {
            active_ = true;
            hover_sent_ = down_sent_ = false;
            down_polls_ = 0;
        }
        finger_down_ = true;
        cancelled_ = false;
        moved_ = true;  // also send an initial touch at client coordinate (0, 0)
        Move(x, y);
    }

    void Move(int32_t x, int32_t y) {
        if (!active_ || (x == x_ && y == y_)) return;
        x_ = x;
        y_ = y;
        moved_ = true;
    }

    void Up() { finger_down_ = false; }

    void Cancel() {
        finger_down_ = false;
        cancelled_ = true;
    }

    MenuTouchFrame Next() {
        if (!active_) return {};
        if (cancelled_ && !down_sent_) {
            active_ = false;
            return {};
        }

        MenuTouchFrame out;
        out.active = true;
        out.x = x_;
        out.y = y_;
        out.moved = moved_;
        moved_ = false;

        if (!hover_sent_) {
            hover_sent_ = true;
            return out;  // Frame N: exact position, buttons released.
        }
        if (!down_sent_) {
            down_sent_ = true;
            down_polls_ = 1;
            out.left_down = true;
            out.transition = MenuTouchTransition::Down;
            return out;  // Frame N+1.
        }
        if (cancelled_) {
            active_ = false;
            out.transition = MenuTouchTransition::Up;
            return out;
        }
        if (down_polls_ < 2) {
            ++down_polls_;
            out.left_down = true;
            return out;  // Frame N+2.
        }
        if (!finger_down_) {
            active_ = false;
            out.transition = MenuTouchTransition::Up;
            return out;  // Frame N+3 for a tap, or the first poll after a later release.
        }
        out.left_down = true;
        return out;
    }

private:
    bool active_ = false, finger_down_ = false, cancelled_ = false;
    bool hover_sent_ = false, down_sent_ = false, moved_ = false;
    uint32_t down_polls_ = 0;
    int32_t x_ = 0, y_ = 0;
};

}  // namespace native

#endif
