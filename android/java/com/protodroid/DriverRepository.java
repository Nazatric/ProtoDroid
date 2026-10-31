package com.protodroid;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.UUID;

/**
 * Private, multi-package driver storage. Selection is deliberately kept in preferences by the Android UI;
 * this class only deals with validated files, which also makes the storage rules host-testable.
 */
final class DriverRepository {
    static final String LEGACY_ID = "legacy";
    private static final String LIBRARY_DIRECTORY = "driver-library";

    static final class Entry {
        final String id;
        final File container;
        final DriverPackage.Installed installed;

        Entry(String id, File container, DriverPackage.Installed installed) {
            this.id = id;
            this.container = container;
            this.installed = installed;
        }

        boolean isLegacy() {
            return LEGACY_ID.equals(id);
        }

        String catalogId() {
            return id.startsWith("catalog-") ? id.substring("catalog-".length()) : "";
        }
    }

    static File libraryRoot(File filesDir) {
        return new File(filesDir, LIBRARY_DIRECTORY);
    }

    static File legacyRoot(File filesDir) {
        return new File(filesDir, "driver");
    }

    /** Invalid or half-installed slots are ignored, never substituted for a valid package. */
    static List<Entry> list(File filesDir) {
        ArrayList<Entry> result = new ArrayList<>();
        Entry legacy = find(filesDir, LEGACY_ID);
        if (legacy != null) result.add(legacy);
        File[] children = libraryRoot(filesDir).listFiles();
        if (children != null) {
            for (File child : children) {
                if (!child.isDirectory() || child.getName().startsWith(".")) continue;
                Entry entry = readEntry(child.getName(), child);
                if (entry != null) result.add(entry);
            }
        }
        Collections.sort(result, new Comparator<Entry>() {
            @Override public int compare(Entry left, Entry right) {
                int name = left.installed.name.compareToIgnoreCase(right.installed.name);
                return name != 0 ? name : left.id.compareTo(right.id);
            }
        });
        return result;
    }

    static Entry find(File filesDir, String id) {
        if (LEGACY_ID.equals(id)) {
            File root = legacyRoot(filesDir);
            DriverPackage.Installed installed = DriverPackage.read(root);
            return installed == null ? null : new Entry(LEGACY_ID, root, installed);
        }
        if (!validId(id)) return null;
        File container = new File(libraryRoot(filesDir), id);
        return readEntry(id, container);
    }

    /**
     * Installs into a named catalog slot or a new import slot. Existing slots are replaced only after validation;
     * other installed drivers are untouched. Repeated imports of the same library are deduplicated by its SHA-256.
     */
    static Entry install(File filesDir, String catalogId, InputStream input, String sourceName, int currentApi)
            throws IOException {
        boolean named = catalogId != null && !catalogId.isEmpty();
        if (named && !validId(catalogId)) throw new IOException("Invalid driver catalog id");
        String id = named ? "catalog-" + catalogId : "incoming-" + UUID.randomUUID().toString().replace("-", "");
        File container = new File(libraryRoot(filesDir), id);
        boolean existed = container.exists();
        try {
            DriverPackage.Installed installed = DriverPackage.install(input, sourceName,
                    new File(container, "package"), currentApi);
            Entry current = new Entry(id, container, installed);
            if (named || !installed.sha256.matches("[0-9a-f]{64}")) return current;

            File duplicate = new File(libraryRoot(filesDir), "import-" + installed.sha256);
            if (duplicate.exists()) {
                Entry previous = readEntry(duplicate.getName(), duplicate);
                if (previous != null && installed.sha256.equals(previous.installed.sha256)) {
                    DriverPackage.deleteTree(container);
                    return previous;
                }
                // A same-named but invalid/corrupt slot must not block a valid import.
                DriverPackage.deleteTree(duplicate);
            }
            if (container.renameTo(duplicate)) {
                Entry renamed = readEntry(duplicate.getName(), duplicate);
                return renamed != null ? renamed : new Entry(duplicate.getName(), duplicate, installed);
            }
            return current;
        } catch (IOException e) {
            if (!existed) deleteQuietly(container);
            throw e;
        } catch (RuntimeException e) {
            if (!existed) deleteQuietly(container);
            throw e;
        }
    }

    static boolean remove(File filesDir, String id) throws IOException {
        Entry entry = find(filesDir, id);
        if (entry == null) return false;
        if (entry.isLegacy()) {
            DriverPackage.deleteTree(legacyRoot(filesDir));
        } else {
            DriverPackage.deleteTree(entry.container);
        }
        return true;
    }

    private static Entry readEntry(String id, File container) {
        if (!validId(id) || !container.isDirectory()) return null;
        DriverPackage.Installed installed = DriverPackage.read(new File(container, "package"));
        return installed == null ? null : new Entry(id, container, installed);
    }

    private static boolean validId(String id) {
        return id != null && id.length() <= 160 && id.matches("[A-Za-z0-9][A-Za-z0-9._-]*")
                && !id.equals(".") && !id.equals("..");
    }

    private static void deleteQuietly(File file) {
        try {
            DriverPackage.deleteTree(file);
        } catch (IOException ignored) {
            // The installer has already reported the useful error; never hide it behind cleanup failure.
        }
    }

    private DriverRepository() {}
}
