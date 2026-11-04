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
manager = (ROOT / "android/java/com/protodroid/DriverManager.java").read_text(encoding="utf-8")

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

# The guided layout keeps the common choice simple and puts advanced package management behind explicit disclosure.
assert "private static final float MIN_TOUCH_DP = 48f;" in driver and \
       "Math.max(MIN_TOUCH_DP * density, MIN_TOUCH_DP * u)" in driver, (
    "Custom Canvas controls must retain a physical 48 dp floor even when landscape layout scaling is smaller"
)
assert "ACTION_PACKAGES" in driver and "showPackages = settings.driverMode() == 3;" in driver, (
    "Package management should be collapsed for ordinary Auto/System use and open for Custom"
)
assert "ACTION_FILTER" not in driver and "FILTER_NAMES" not in driver, (
    "The old dense four-filter library should not return"
)
assert "ACTION_TAB" in driver and '"INSTALLED (" + installed.size() + ")"' in driver and '"DOWNLOADS"' in driver, (
    "Package management needs clear Installed and Downloads destinations"
)
assert "DriverScreenPolicy.supportsBackend" in driver and "DriverScreenPolicy.showAllByDefault" in driver, (
    "Compatibility filtering must use the host-tested family policy"
)
assert '"Automatic"' in driver or "DriverScreenPolicy.modeName" in driver
assert '"Phone driver"' in driver or "DriverScreenPolicy.modeName" in driver
assert "Tap the card to use this driver" in driver and "CONFIRM REMOVE" in driver, (
    "Selection must be understandable and removal must remain explicitly confirmed"
)
assert "SAFE RECOVERY" in driver and "never replace the phone's global driver" in driver

# Animated drawing reuses geometry, font metrics, and primitive action lists after the first frame. Hit-testing still
# uses persistent actionBounds copies, so pooled draw temporaries cannot alter touch or keyboard focus behavior.
assert driver.count("new RectF(") == 2, "draw-temporary RectF allocations returned to DriverView"
assert "getFontMetrics()" not in driver and "paint.getFontMetrics(fontMetrics);" in driver
assert "ArrayList<Integer> usedActions" not in driver and "usedActions[usedActionCount++] = action;" in driver
assert "rectPoolUsed = 0;" in driver and "rectPool.get(rectPoolUsed++)" in driver
driver_draw = driver.split("protected void onDraw(Canvas canvas)", 1)[1]
assert driver_draw.index("rectPoolUsed = 0;") < driver_draw.index("drawBackground(canvas,"), (
    "the rectangle pool must reset before the first draw call, not only before later cards"
)
assert "actionBounds[action].set(bounds);" in driver

migration = manager.split("private void migrateLegacyIfNeeded()", 1)[1].split("private void migrateLegacy(", 1)[0]
assert "DriverRepository.shouldMigrateLegacy" in migration, (
    "Launch must use the host-tested policy that keeps legacy Custom file migration off Auto/System/Turnip"
)
assert migration.index("DriverRepository.shouldMigrateLegacy") < migration.index("if (selected() != null) return;")

# A same-build failure marker prevents an automatic crash loop, but an explicit driver choice is the user's recovery
# action. It must clear both the custom/Turnip and compatibility-wrapper guards; previously the wrapper marker had no
# launcher recovery path and survived every subsequent Auto launch on that build.
mode_action = driver.split("if (action >= ACTION_MODE && action < ACTION_MODE + 4)", 1)[1].split(
    "if (action >= ACTION_TAB && action < ACTION_TAB + 2)", 1
)[0]
assert "manager.clearFailureMarkers();" in mode_action, "Explicit driver selection does not retry guarded startup paths"
assert mode_action.index("manager.clearFailureMarkers();") < mode_action.index("settings.setDriverMode(mode);")
assert "void clearFailureMarkers()" in manager, "DriverManager has no explicit recovery-marker cleanup"
cleanup = manager.split("void clearFailureMarkers()", 1)[1].split("\n    }", 1)[0]
for marker in ("turnip_failed", "vulkan_wrapper_failed"):
    assert f'new File(files(), "{marker}").delete();' in cleanup, f"Explicit recovery leaves {marker} behind"
assert "private void clearFailureMarkers()" not in manager, "DriverView cannot invoke private recovery cleanup"

print("Drivers navigation, explicit recovery, launch-migration, guided-layout, and touch checks passed")
