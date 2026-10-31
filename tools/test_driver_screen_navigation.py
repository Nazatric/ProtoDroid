#!/usr/bin/env python3
"""Guard Drivers entry/exit navigation and touch cleanup across content-view replacement.

This is a host-side structural regression test, not a substitute for an Android device smoke test. Opening Drivers must
not dereference the launcher's removed parent, and closing it must not use a touch tracker already recycled by detach.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
setup = (ROOT / "android/java/com/protodroid/SetupView.java").read_text(encoding="utf-8")
driver = (ROOT / "android/java/com/protodroid/DriverView.java").read_text(encoding="utf-8")
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

# Both the on-screen and Android system back paths must return to the existing launcher, never finish the activity.
close_drivers = activity.split("private void closeDrivers()", 1)[1].split("private File appGame()", 1)[0]
assert "setContentView(setupView);" in close_drivers, "Closing Drivers must restore the launcher homepage"
assert "finish(" not in close_drivers, "Closing Drivers must not finish the launcher"
back_pressed = activity.split("public void onBackPressed()", 1)[1]
assert back_pressed.index("if (driverView != null)") < back_pressed.index("closeDrivers();") < back_pressed.index(
    "super.onBackPressed();"
), "System Back must close Drivers before the Activity's default exit behavior"
assert "listener.onClose();" in driver, "The DriverView BACK action is not connected to the launcher"

# Closing Drivers from its own BACK button detaches the view synchronously. onDetachedFromWindow recycles the tracker,
# so ACTION_UP must finish all touch cleanup before activate() and every recycle path must tolerate an earlier detach.
driver_up = driver.split("case MotionEvent.ACTION_UP:", 1)[1].split("case MotionEvent.ACTION_CANCEL:", 1)[0]
driver_detach = driver.split("protected void onDetachedFromWindow()", 1)[1]
recycle = "recycleVelocityTracker();"
assert recycle in driver_up and recycle in driver_detach, "Touch-up and detach must share null-safe tracker cleanup"
assert driver_up.index(recycle) < driver_up.index("activate(action);"), (
    "ACTION_UP used its touch tracker after the BACK action detached DriverView"
)
recycle_helper = driver.split("private void recycleVelocityTracker()", 1)[1].split("}", 1)[0]
assert "if (velocityTracker == null) return;" in recycle_helper, "Tracker cleanup must be safe after detachment"
assert "velocityTracker = null;" in recycle_helper, "Tracker cleanup must clear the recycled reference"

print("Drivers navigation and touch-detach regression checks passed")
