package com.protodroid;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Read-only, bounded access to ProtoDroid's actual native, DXVK, and diagnostic-tour logs. */
final class LauncherLog {
    static final int MAX_BYTES = 512 * 1024;

    static final class Entry {
        final String name, path, content, error;
        final boolean truncated;

        Entry(String name, String path, String content, boolean truncated, String error) {
            this.name = name;
            this.path = path;
            this.content = content;
            this.truncated = truncated;
            this.error = error;
        }
    }

    private static final class Source {
        final String id, name;
        final String[] relativePaths;

        Source(String id, String name, String... relativePaths) {
            this.id = id;
            this.name = name;
            this.relativePaths = relativePaths;
        }
    }

    private static final Source[] SOURCES = {
            new Source("native", "proto-native.log",
                    "proto-native.log", "logs/proto-native.log"),
            new Source("prev", "proto-native.prev.log",
                    "proto-native.prev.log", "logs/proto-native.prev.log"),
            new Source("dxvk", "app_process64_d3d9.log",
                    "app_process64_d3d9.log", "logs/app_process64_d3d9.log"),
            new Source("tour", "tour/proto-native.log",
                    "diagnostic-tour-logs/proto-native.log", "diagnostic-tour/logs/proto-native.log"),
            new Source("journal", "tour/journal.log",
                    "diagnostic-tour-logs/journal.log", "diagnostic-tour/logs/journal.log"),
            new Source("result", "tour/result.txt",
                    "diagnostic-tour-logs/result.txt", "diagnostic-tour/logs/result.txt"),
    };

    static Entry read(File appBase, File sharedPrototype, String id) {
        Source source = null;
        for (Source candidate : SOURCES) if (candidate.id.equals(id)) source = candidate;
        if (source == null) return new Entry("", "", "", false, "Unknown log");

        List<File> candidates = new ArrayList<>();
        if (appBase != null) candidates.add(new File(appBase, source.relativePaths[0]));
        if (sharedPrototype != null) candidates.add(new File(sharedPrototype, source.relativePaths[1]));
        File selected = null;
        for (File candidate : candidates) {
            if (!candidate.isFile() || !candidate.canRead()) continue;
            if (selected == null || candidate.lastModified() >= selected.lastModified()) selected = candidate;
        }
        if (selected == null) return new Entry(source.name, "", "", false, "Log has not been created yet");
        try {
            return new Entry(source.name, selected.getAbsolutePath(), readTail(selected), wasTruncated(selected), "");
        } catch (IOException | SecurityException e) {
            String message = e.getMessage();
            return new Entry(source.name, selected.getAbsolutePath(), "", false,
                    message == null || message.isEmpty() ? "Could not read log" : message);
        }
    }

    private static String readTail(File file) throws IOException {
        // The Android bridge returns one bounded string. This never rotates, clears, or otherwise writes a log.
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long length = input.length();
            long start = Math.max(0L, length - MAX_BYTES);
            input.seek(start);
            byte[] bytes = new byte[(int) (length - start)];
            input.readFully(bytes);
            return new String(bytes, StandardCharsets.UTF_8);
        }
    }

    static boolean wasTruncated(File file) {
        return file != null && file.length() > MAX_BYTES;
    }

    private LauncherLog() {}
}
