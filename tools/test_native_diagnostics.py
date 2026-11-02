#!/usr/bin/env python3
"""Native diagnostics wiring plus functional tests of the production bounded log reader."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
JAVA = ROOT / "android/java/com/protodroid"
activity = (JAVA / "DiagnosticsActivity.java").read_text(encoding="utf-8")
setup = (JAVA / "SetupActivity.java").read_text(encoding="utf-8")
view = (JAVA / "SetupView.java").read_text(encoding="utf-8")
manifest = (ROOT / "android/AndroidManifest.xml").read_text(encoding="utf-8")

for marker in (
    "LauncherLog.read", "worker.execute", "loadGeneration", "worker.shutdownNow()",
    "ClipData.newPlainText", "REFRESH", "COPY LOG TAIL", "DriverScreenPolicy.deviceFamily",
    "Build.SUPPORTED_ABIS", "totalMemory()", "installedVersion()", "APP-SPECIFIC DATA", "SHARED SAVES",
    "They do not enable graphics features", "if (logsVisible) showLogs(false)",
):
    assert marker in activity, f"native diagnostics is missing {marker}"
assert "android.webkit" not in activity and "androidx" not in activity
assert "DiagnosticsActivity.class" in setup and "onDiagnostics()" in setup
assert 'drawButton(canvas, ACTION_DIAGNOSTICS' in view
assert "listener.onDiagnostics()" in view
assert view.count("ACTION_DIAGNOSTICS, ACTION_MODS, ACTION_DRIVERS, ACTION_EXIT") == 4
assert 'android:name="com.protodroid.DiagnosticsActivity"' in manifest

harness = r'''package com.protodroid;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

public final class LauncherLogTest {
    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("launcher-log-test");
        File app = root.resolve("app").toFile();
        File sharedLogs = root.resolve("shared/logs").toFile();
        File shared = root.resolve("shared").toFile();
        require(app.mkdirs() && sharedLogs.mkdirs(), "fixture directories");
        File appNative = new File(app, "proto-native.log");
        File sharedNative = new File(sharedLogs, "proto-native.log");
        Files.write(appNative.toPath(), "app".getBytes(StandardCharsets.UTF_8));
        Files.write(sharedNative.toPath(), "shared".getBytes(StandardCharsets.UTF_8));
        require(appNative.setLastModified(1000L) && sharedNative.setLastModified(2000L), "timestamps");
        LauncherLog.Entry newest = LauncherLog.read(app, shared, "native");
        require(newest.content.equals("shared"), "newest readable log was not selected");
        require(newest.path.equals(sharedNative.getAbsolutePath()), "selected path mismatch");

        byte[] large = new byte[LauncherLog.MAX_BYTES + 257];
        for (int i = 0; i < large.length; ++i) large[i] = (byte) ('A' + i % 26);
        File dxvk = new File(app, "app_process64_d3d9.log");
        Files.write(dxvk.toPath(), large);
        byte[] before = Files.readAllBytes(dxvk.toPath());
        long modified = dxvk.lastModified();
        LauncherLog.Entry tail = LauncherLog.read(app, shared, "dxvk");
        require(tail.truncated, "large log was not marked truncated");
        String expected = new String(Arrays.copyOfRange(large, 257, large.length), StandardCharsets.UTF_8);
        require(tail.content.equals(expected), "tail is not the exact final MAX_BYTES");
        require(tail.content.getBytes(StandardCharsets.UTF_8).length == LauncherLog.MAX_BYTES, "tail bound");
        require(Arrays.equals(before, Files.readAllBytes(dxvk.toPath())), "log read changed bytes");
        require(dxvk.lastModified() == modified, "log read changed modification time");

        LauncherLog.Entry missing = LauncherLog.read(app, shared, "tour");
        require(missing.error.contains("not been created"), "missing log was not explained");
        require(LauncherLog.read(app, shared, "invented").error.equals("Unknown log"), "unknown log accepted");
        System.out.println("LauncherLog functional checks passed");
    }
}
'''
if shutil.which("javac") and shutil.which("java"):
    with tempfile.TemporaryDirectory(prefix="protodroid-diagnostics-test-") as directory:
        root = Path(directory)
        source = root / "com/protodroid/LauncherLogTest.java"
        source.parent.mkdir(parents=True)
        source.write_text(harness, encoding="utf-8")
        classes = root / "classes"
        classes.mkdir()
        subprocess.run([
            "javac", "-encoding", "UTF-8", "-d", str(classes),
            str(JAVA / "LauncherLog.java"), str(source),
        ], check=True)
        subprocess.run(["java", "-cp", str(classes), "com.protodroid.LauncherLogTest"], check=True)
elif os.environ.get("REQUIRE_JAVA_COMPILER") == "1":
    raise RuntimeError("CI requires javac and java for the production LauncherLog harness")
else:
    print("Java compiler unavailable locally; the required CI LauncherLog harness was not run here")

print("Native About/device information and log-navigation checks passed")
