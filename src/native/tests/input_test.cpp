// Host UBSAN tests for the absolute, multi-frame menu touch contract.
#include "input_state.h"

#include <cstdio>
#include <stdexcept>

using namespace native;

namespace {

void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void TestCoordinates() {
    Check(MenuTouchClientCoordinate(0, 1280) == 0, "normalized origin did not map to client origin");
    Check(MenuTouchClientCoordinate(kMenuTouchCoordinateScale / 2, 1280) == 640,
          "normalized midpoint did not map 1:1");
    Check(MenuTouchClientCoordinate(kMenuTouchCoordinateScale, 1280) == 1279,
          "normalized right edge escaped the client");
    Check(MenuTouchClientCoordinate(kMenuTouchCoordinateScale + 100, 720) == 719,
          "out-of-range fixed-point input was not clamped");
    Check(MenuTouchClientCoordinate(-1, 720) == 0, "negative fixed-point input was not clamped");
    Check(MenuTouchClientCoordinate(kMenuTouchCoordinateScale, 0) == 0,
          "zero-sized client produced an invalid coordinate");
}

void TestTap() {
    MenuTouchSequencer touch;
    touch.Down(513, 277);
    touch.Up();  // A quick finger release must not collapse hover/down/up into one guest frame.

    MenuTouchFrame f = touch.Next();
    Check(f.active && f.moved && !f.left_down && f.x == 513 && f.y == 277 &&
              f.transition == MenuTouchTransition::None,
          "frame N was not an exact absolute hover");
    f = touch.Next();
    Check(f.active && f.left_down && f.transition == MenuTouchTransition::Down,
          "frame N+1 did not emit left down");
    f = touch.Next();
    Check(f.active && f.left_down && f.transition == MenuTouchTransition::None,
          "frame N+2 did not hold left down");
    f = touch.Next();
    Check(f.active && !f.left_down && f.transition == MenuTouchTransition::Up,
          "frame N+3 did not emit left up");
    Check(!touch.Next().active, "completed tap did not leave the sequencer");
}

void TestHeldMoveAndCancel() {
    MenuTouchSequencer touch;
    touch.Down(0, 0);
    Check(touch.Next().moved, "origin touch did not emit its absolute hover");
    Check(touch.Next().transition == MenuTouchTransition::Down, "held touch did not go down");
    Check(touch.Next().left_down, "held touch missed its second down poll");
    MenuTouchFrame f = touch.Next();
    Check(f.active && f.left_down && f.transition == MenuTouchTransition::None,
          "held finger released before frame N+3");
    touch.Move(999, 700);
    f = touch.Next();
    Check(f.active && f.moved && f.left_down && f.x == 999 && f.y == 700,
          "held drag did not retain 1:1 coordinates");
    touch.Up();
    f = touch.Next();
    Check(f.active && !f.left_down && f.transition == MenuTouchTransition::Up,
          "release after a hold did not emit up");
    Check(!touch.Next().active, "held touch did not finish");

    touch.Down(20, 30);
    touch.Next();
    touch.Cancel();
    Check(!touch.Next().active, "cancel before down still generated a click");

    touch.Down(40, 50);
    touch.Next();
    touch.Next();
    touch.Cancel();
    f = touch.Next();
    Check(f.active && !f.left_down && f.transition == MenuTouchTransition::Up,
          "cancel after down left the button held");
    Check(!touch.Next().active, "cancelled touch did not finish");
}

}  // namespace

int main() {
    try {
        TestCoordinates();
        TestTap();
        TestHeldMoveAndCancel();
        std::puts("input tests passed (1:1 coordinates, absolute hover, multi-frame click, hold, drag, cancel)");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "input test failed: %s\n", e.what());
        return 1;
    }
}
