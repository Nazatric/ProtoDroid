#!/usr/bin/env python3
"""Guard the launcher touch ordering that previously crashed the Drivers button.

This is a host-side structural regression test, not a substitute for an Android device smoke test. It specifically
prevents touch cleanup from looking up a view parent after an action has replaced the activity's content view.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
setup = (ROOT / "android/java/com/protodroid/SetupView.java").read_text(encoding="utf-8")
activity = (ROOT / "android/java/com/protodroid/SetupActivity.java").read_text(encoding="utf-8")

show_drivers = activity.split("private void showDrivers()", 1)[1].split("private void closeDrivers()", 1)[0]
assert "setContentView(driverView);" in show_drivers, "The Drivers action no longer replaces the content view"

try:
    action_up = setup.split("case MotionEvent.ACTION_UP:", 1)[1].split(
        "case MotionEvent.ACTION_CANCEL:", 1
    )[0]
except IndexError as error:
    raise AssertionError("SetupView touch-up/cancel handlers are missing") from error

capture = "ViewParent parent = getParent();"
activate = "activate(action);"
release = "if (parent != null) parent.requestDisallowInterceptTouchEvent(false);"
assert capture in action_up, "Touch-up must retain its original parent before running an action"
assert activate in action_up, "Touch-up action dispatch is missing"
assert release in action_up, "Touch-up must release interception null-safely through the retained parent"
assert action_up.index(capture) < action_up.index(activate) < action_up.index(release), (
    "Parent capture must precede content-view replacement; cleanup must use that retained parent afterwards"
)
assert "getParent().requestDisallowInterceptTouchEvent(false)" not in action_up, (
    "A detached setup view can have no parent after Drivers opens"
)

print("Drivers navigation touch-detach regression checks passed")
