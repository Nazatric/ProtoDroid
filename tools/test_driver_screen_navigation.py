#!/usr/bin/env python3
"""Guard native Compose-to-driver navigation and DriverView touch cleanup.

This is a host-side structural regression test, not an Android device smoke test. The Compose launcher opens the
existing driver manager in its own activity; DriverView must still finish all touch cleanup before a BACK action can
detach it.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
driver = (ROOT / "android/java/com/protodroid/DriverView.java").read_text(encoding="utf-8")
activity = (ROOT / "android/java/com/protodroid/DriverActivity.java").read_text(encoding="utf-8")
host = (ROOT / "android/launcher/src/androidMain/kotlin/com/protodroid/ProtoDroidLauncherHost.kt").read_text(encoding="utf-8")
compose = (ROOT / "android/launcher/src/commonMain/kotlin/com/protodroid/launcher/LauncherApp.kt").read_text(encoding="utf-8")
manifest = (ROOT / "android/AndroidManifest.xml").read_text(encoding="utf-8")

assert "DriverActivity::class.java" in host and "override fun openDriverPackages()" in host
assert 'ActionButton("MANAGE DRIVER PACKAGES", onClick = host::openDriverPackages)' in compose
assert "new DriverView(this, new GameSettings(this), driverManager, this::finish)" in activity
assert "driverManager.setObserver(null);" in activity and "driverManager.close();" in activity
assert 'android:name="com.protodroid.DriverActivity"' in manifest
assert "listener.onClose();" in driver, "The DriverView BACK action is not connected to its activity"

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

print("Native driver navigation, guided-layout, touch-target, and touch-detach checks passed")
