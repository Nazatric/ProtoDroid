#!/usr/bin/env python3
"""Structural and offline-asset checks for the restored pre-WebView native Android launcher."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JAVA = ROOT / "android/java/com/protodroid"
ACTIVITY = JAVA / "SetupActivity.java"
VIEW = JAVA / "SetupView.java"
ASSETS = ROOT / "android/assets/setup"

activity = ACTIVITY.read_text(encoding="utf-8")
view = VIEW.read_text(encoding="utf-8")
packager = (ROOT / "android/build-apk.sh").read_text(encoding="utf-8")
workflow = (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8")
manifest = (ROOT / "android/AndroidManifest.xml").read_text(encoding="utf-8")

assert "public class SetupActivity extends Activity" in activity
assert "final class SetupView extends View" in view
assert "private SetupView setupView;" in activity
assert "new SetupView(this, new SetupView.Listener()" in activity
assert not (ROOT / "android/launcher").exists(), "rejected Compose source returned"
assert not (ROOT / "launcher").exists(), "rejected React source returned"
assert not (ROOT / "android/assets/launcher").exists(), "rejected Web launcher asset returned"
for path in ("android/build.gradle.kts", "android/settings.gradle.kts", "android/gradlew"):
    assert not (ROOT / path).exists(), f"unneeded Gradle presentation build returned: {path}"
for forbidden in ("android.webkit", "addJavascriptInterface", "JavascriptInterface"):
    assert forbidden not in "\n".join(path.read_text(encoding="utf-8") for path in JAVA.glob("*.java"))

# The original canvas launcher retains real Android services and native routes.
for marker in (
    "GameFiles.firstComplete", "GameFiles.anyMissingUv", "PROTODROID_GAME", "PROTODROID_CUSTOM_DRIVER",
    "driverManager.driverForLaunch()", "ProtoActivity.class", "PROTODROID_DIAGNOSTIC_TOUR",
    "normal Play was not substituted", "ModsActivity.class", "TouchLayoutActivity.class", "DiagnosticsActivity.class",
    "updateManager.onUpdatePressed", "updateManager.confirmUninstall", "updateManager.onReleasesPressed",
):
    assert marker in activity, f"native launcher route is missing {marker}"
for marker in (
    "ACTION_TOUCH_LAYOUT", "listener.onTouchLayout();", "ACTION_MODS", "listener.onMods();",
    "ACTION_DRIVERS", "listener.onDrivers();", "GameSettings.categoryOf", "GameSettings.helpFor",
    "drawUpdateWarning", "onLogoTap", "closeUpdateWarning", "closeSettings",
):
    assert marker in view, f"original native view is missing {marker}"
assert "if (updateWarningVisible) {" in view and "return ACTION_NONE;" in view, "modal input must not click through"
assert 'android:name="com.protodroid.SetupActivity"' in manifest
assert 'android:name="com.protodroid.DriverActivity"' not in manifest
assert 'android:name="com.protodroid.ProtoActivity"' in manifest

# Original offline artwork/fonts remain byte-present; no Web/Compose resource bundle is required.
resources = sorted(path for path in ASSETS.rglob("*") if path.is_file())
assert len(resources) == 9, f"expected nine original setup assets, found {len(resources)}"
for path in resources:
    data = path.read_bytes()
    assert data, f"empty setup asset {path}"
    if path.suffix == ".jpg":
        assert data.startswith(b"\xff\xd8\xff"), f"invalid JPEG {path}"
    elif path.suffix == ".png":
        assert data.startswith(b"\x89PNG\r\n\x1a\n"), f"invalid PNG {path}"
    elif path.suffix == ".ttf":
        assert data[:4] in (b"\x00\x01\x00\x00", b"true", b"OTTO"), f"invalid font {path}"

assert "javac --release 11" in packager and '"$BUILD_TOOLS/aapt2" link' in packager
assert 'zip -q -r "$OUT/base.apk" classes.dex lib' in packager, "non-Gradle builds must package their compiled DEX"
assert "gradlew" not in packager and "COMPOSE_BASE_APK" not in packager
assert "sdl_version.py\" verify-class" in packager and "signed SDL" in packager
assert "compose-launcher" not in workflow and "test_compose_launcher.py" not in workflow
assert "python3 tools/test_native_launcher.py" in workflow

print("Original native launcher, real Android routes, offline assets, and Web/Compose-free packaging checks passed")
