#!/usr/bin/env python3
"""Structural and asset checks for the production native Compose launcher.

This does not replace Kotlin compilation, APK verification, or a physical-device smoke test. It prevents the removed
Web/preview host and fake settings model from silently returning, and verifies that every referenced offline resource
is present.
"""
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "android/launcher/src/commonMain/kotlin/com/protodroid/launcher/LauncherApp.kt"
MODELS = ROOT / "android/launcher/src/commonMain/kotlin/com/protodroid/launcher/LauncherModels.kt"
HOST = ROOT / "android/launcher/src/androidMain/kotlin/com/protodroid/ProtoDroidLauncherHost.kt"
ACTIVITY = ROOT / "android/launcher/src/androidMain/kotlin/com/protodroid/SetupActivity.kt"
GRADLE = ROOT / "android/launcher/build.gradle.kts"
RES = ROOT / "android/launcher/src/commonMain/composeResources"

app = APP.read_text(encoding="utf-8")
models = MODELS.read_text(encoding="utf-8")
host = HOST.read_text(encoding="utf-8")
activity = ACTIVITY.read_text(encoding="utf-8")
gradle = GRADLE.read_text(encoding="utf-8")
settings = (ROOT / "android/java/com/protodroid/GameSettings.java").read_text(encoding="utf-8")
manifest = (ROOT / "android/AndroidManifest.xml").read_text(encoding="utf-8")
packager = (ROOT / "android/build-apk.sh").read_text(encoding="utf-8")
workflow = (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8")

assert not (ROOT / "launcher").exists(), "the retired React launcher source returned"
assert not (ROOT / "android/assets/launcher").exists(), "the retired Web launcher asset returned"
production = "\n".join(
    path.read_text(encoding="utf-8")
    for path in [
        *sorted((ROOT / "android/java/com/protodroid").glob("*.java")),
        *sorted((ROOT / "android/launcher/src").rglob("*.kt")),
    ]
)
for forbidden in ("android.webkit", "JavascriptInterface", "addJavascriptInterface", "PreviewLauncherHost"):
    assert forbidden not in production, f"preview/Web behavior must not be packaged: {forbidden}"
assert "wasmJs" not in gradle and "com.protodroid.launcher\"" not in gradle
assert 'applicationId = "com.protodroid"' in gradle and "minSdk = 26" in gradle
assert "toIni" not in models and "saveSettings" not in models and "loadSettings" not in models
assert "clearLogs" not in models and "clearLogs" not in app

# Android's exact rows and defaults are authoritative; the preview's partial INI/default model must not return.
assert "GameSettings(activity)" in host and "gameSettings.setIndex" in host
assert "gameSettings.applyPreset" in host and "gameSettings.resetToDefaults" in host
assert "GameSettings.categoryOf(setting)" in host and "GameSettings.helpFor(setting)" in host
assert "host.setSetting(row.key, index)" in app and "host::resetSettings" in app
assert 'new Setting("Unlock menu FPS", "unlock_menu_fps", new String[] {"Off", "On"}' in settings
assert 'key.equals("unlock_menu_fps")' in settings and "return 1;" in settings
for section in ("Graphics", "Video", "Controls", "Display", "Engine"):
    assert re.search(rf"\b{section}\(", models), f"missing production settings section {section}"
for preview_only in ("launcherSounds", "vibrationStrength", "leftHanded", "audioDriver", "importProfile"):
    assert preview_only not in models, f"unsupported preview-only setting returned: {preview_only}"

# Real file detection, launch environment, save-safe tour, drivers and native tools.
for marker in (
    "GameFiles.firstComplete", "GameFiles.anyMissingUv", "PROTODROID_GAME",
    "PROTODROID_CUSTOM_DRIVER", "driverManager.driverForLaunch()", "ProtoActivity::class.java",
    "PROTODROID_DIAGNOSTIC_TOUR", "normal Play was not substituted",
    "ModManager.list", "ModManager.setEnabled", "worker.execute", "LauncherLog.read",
    "updateManager.onResume", "updateManager.onCheckUpdatesPressed", "updateManager.confirmUninstall",
    "DriverActivity::class.java", "ModsActivity::class.java", "TouchLayoutActivity::class.java",
):
    assert marker in host, f"production host is missing {marker}"
assert "onLogoTap" in app and "logoTourCode.onLogoTap" in host
assert "tourCode.onLetter" in host and "host.onLetter" in activity
assert 'if (gameData.gameDataReady) StatusTag("READY", Bone)' in app
assert "CHECK APP FOLDER" in app and "host.checkGameData()" in activity
assert "if (modsRequested) refreshMods()" in host, "startup must not scan every mod before the library is used"
assert "OPEN TOUCH EDITOR" in app and "SETTINGS → CONTROLS → TOUCH LAYOUT" in app
assert "host::openMods" in app and "host::openDriverPackages" in app
assert "host::openReleases" in app and "private fun UpdateWarning" in app
assert "LocalLauncherMotion" in app and "host.motionEnabled()" in app

# Resource accessor references must resolve to real offline files, not generated preview output.
for name in sorted(set(re.findall(r"Res\.drawable\.([a-z0-9_]+)", app))):
    candidates = [RES / "drawable" / f"{name}.webp", RES / "drawable" / f"{name}.png"]
    assert any(path.is_file() and path.stat().st_size > 0 for path in candidates), f"missing drawable {name}"
for name in sorted(set(re.findall(r"Res\.font\.([a-z0-9_]+)", app + (ROOT / "android/launcher/src/commonMain/kotlin/com/protodroid/launcher/LauncherTheme.kt").read_text(encoding="utf-8")))):
    path = RES / "font" / f"{name}.ttf"
    assert path.is_file() and path.stat().st_size > 0, f"missing font {name}"
resources = [path for path in RES.rglob("*") if path.is_file()]
assert len(resources) == 34, f"expected the supplied 34 offline assets, found {len(resources)}"
for path in resources:
    data = path.read_bytes()
    if path.suffix == ".webp":
        assert data[:4] == b"RIFF" and data[8:12] == b"WEBP", f"invalid WebP {path}"
    elif path.suffix == ".png":
        assert data.startswith(b"\x89PNG\r\n\x1a\n"), f"invalid PNG {path}"
    elif path.suffix == ".ttf":
        assert data[:4] in (b"\x00\x01\x00\x00", b"true", b"OTTO"), f"invalid font {path}"

assert ":launcher:assembleRelease" in packager and "launcher-release-unsigned.apk" in packager
assert "COMPOSE_BASE_APK" in packager, "CI must be able to package the exact early-compiled Compose base"
assert "npm " not in packager and "classes.dex lib" not in packager
assert "setup-node" not in workflow and "npm ci --prefix launcher" not in workflow
assert "  compose-launcher:\n" in workflow and "name: compose-launcher-base" in workflow
assert "needs: [build-deps, build-turnip, compose-launcher," in workflow
assert 'android:name="com.protodroid.SetupActivity"' in manifest
assert 'android:name="com.protodroid.DriverActivity"' in manifest
with zipfile.ZipFile(ROOT / "android/gradle/wrapper/gradle-wrapper.jar") as wrapper:
    assert "org/gradle/wrapper/GradleWrapperMain.class" in wrapper.namelist()

print("Native Compose host, production routes, offline assets, and Web-free packaging checks passed")
