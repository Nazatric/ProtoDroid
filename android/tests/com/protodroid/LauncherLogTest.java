package com.protodroid;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Comparator;

/** Host tests for bounded, read-only launcher log access. */
public final class LauncherLogTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void write(File file, byte[] bytes) throws Exception {
        File parent = file.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new AssertionError("mkdir " + parent);
        try (FileOutputStream output = new FileOutputStream(file)) {
            output.write(bytes);
        }
    }

    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("protodroid-launcher-logs").toFile();
        try {
            File app = new File(root, "app");
            File shared = new File(root, "Prototype");
            File privateLog = new File(app, "proto-native.log");
            File sharedLog = new File(shared, "logs/proto-native.log");
            write(privateLog, "private".getBytes(StandardCharsets.UTF_8));
            write(sharedLog, "shared-current".getBytes(StandardCharsets.UTF_8));
            check(privateLog.setLastModified(1000L), "set private timestamp");
            check(sharedLog.setLastModified(2000L), "set shared timestamp");

            LauncherLog.Entry current = LauncherLog.read(app, shared, "native");
            check(current.content.equals("shared-current"), "newest mirror was not selected");
            check(current.path.equals(sharedLog.getAbsolutePath()), "wrong real log path");
            check(!current.truncated && current.error.isEmpty(), "unexpected current-log state");

            byte[] large = new byte[LauncherLog.MAX_BYTES + 137];
            for (int i = 0; i < large.length; ++i) large[i] = (byte) ('a' + i % 26);
            File result = new File(app, "diagnostic-tour-logs/result.txt");
            write(result, large);
            LauncherLog.Entry tail = LauncherLog.read(app, shared, "result");
            check(tail.truncated, "large log was not marked truncated");
            check(tail.content.getBytes(StandardCharsets.UTF_8).length == LauncherLog.MAX_BYTES,
                    "log tail exceeds bridge budget");
            check(tail.content.charAt(0) == (char) ('a' + 137 % 26), "wrong tail boundary");
            check(Files.size(result.toPath()) == large.length, "reading changed the source log");

            LauncherLog.Entry missing = LauncherLog.read(app, shared, "journal");
            check(missing.content.isEmpty() && missing.error.contains("not been created"), "missing log was fabricated");
            check(LauncherLog.read(app, shared, "../../save").error.equals("Unknown log"),
                    "log id was not constrained to the whitelist");

            System.out.println("launcher real-log whitelist, mirror, tail, and read-only checks passed");
        } finally {
            Files.walk(root.toPath()).sorted(Comparator.reverseOrder()).forEach(path -> {
                try { Files.delete(path); } catch (Exception ignored) {}
            });
        }
    }
}
