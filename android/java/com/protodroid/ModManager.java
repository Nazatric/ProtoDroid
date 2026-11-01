package com.protodroid;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.OpenableColumns;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;
import java.util.zip.ZipEntry;
import java.util.zip.ZipException;
import java.util.zip.ZipFile;

/** Local mod import, lifecycle and launch preparation. Original game files are never import targets. */
final class ModManager {
    private static final String TAG = "ProtoDroidMods";
    private static final long MAX_ARCHIVE_BYTES = 512L * 1024L * 1024L;
    private static final long MAX_EXTRACTED_BYTES = 1024L * 1024L * 1024L;
    private static final int MAX_ENTRIES = 4096;
    private static final int BUFFER = 64 * 1024;

    private ModManager() {}

    static File modsDir(Context context) {
        File appBase = context.getExternalFilesDir(null);
        File fallback = appBase == null ? new File(context.getFilesDir(), "mods") : new File(appBase, "mods");
        boolean sharedAccess = Build.VERSION.SDK_INT >= 30
                ? Environment.isExternalStorageManager()
                : context.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                        == PackageManager.PERMISSION_GRANTED;
        if (sharedAccess) {
            File shared = new File(Environment.getExternalStorageDirectory(), "Prototype/mods");
            if ((shared.isDirectory() || shared.mkdirs()) && shared.canWrite()) return shared;
        }
        if (!fallback.isDirectory()) fallback.mkdirs();
        return fallback;
    }

    static File nativeLaunchDir(Context context) {
        // A fresh path prevents a failed cleanup from resurrecting a disabled or removed plugin on the next process.
        return new File(context.getNoBackupFilesDir(), "mod-native-" + UUID.randomUUID());
    }

    static List<ModManifest> list(Context context) {
        ArrayList<ModManifest> result = new ArrayList<>();
        File[] children = modsDir(context).listFiles();
        if (children != null) {
            for (File child : children) {
                if (!child.isDirectory() || child.getName().startsWith(".")) continue;
                result.add(ModManifest.read(child));
            }
        }
        Collections.sort(result, (a, b) -> {
            int priority = Integer.compare(b.priority, a.priority);
            if (priority != 0) return priority;
            return a.name.compareToIgnoreCase(b.name);
        });
        return result;
    }

    static int activeCount(List<ModManifest> mods) {
        int count = 0;
        for (ModManifest mod : mods) if (mod.enabled && mod.error.isEmpty()) ++count;
        return count;
    }

    static void setEnabled(Context context, ModManifest mod, boolean enabled) throws IOException {
        setEnabled(modsDir(context), mod.id, enabled);
    }

    static void setEnabled(File root, String id, boolean enabled) throws IOException {
        File directory = directChild(root, id);
        if (Files.isSymbolicLink(directory.toPath())) throw new IOException("Mod folder must not be a symbolic link");
        ModManifest current = ModManifest.read(directory);
        if (enabled && !current.error.isEmpty()) throw new IOException(current.error);
        File marker = new File(directory, ModManifest.ENABLED_NAME);
        if (!enabled) {
            if (marker.exists() && !marker.delete()) throw new IOException("Could not disable mod");
            new File(directory, "launch-error.txt").delete();
            return;
        }
        File temporary = new File(directory, "enabled.tmp");
        // Never write through a manually planted link (including a dangling symlink) into game/save data.
        if ((temporary.exists() || Files.isSymbolicLink(temporary.toPath())) && !temporary.delete()) {
            throw new IOException("Could not replace the temporary enabled marker");
        }
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write('1');
            output.getFD().sync();
        }
        if (marker.exists() && !marker.delete()) {
            temporary.delete();
            throw new IOException("Could not replace the enabled marker");
        }
        if (!temporary.renameTo(marker)) {
            temporary.delete();
            throw new IOException("Could not enable mod");
        }
        new File(directory, "launch-error.txt").delete();  // a valid package may retry a previous native load failure
    }

    static void remove(Context context, ModManifest mod) throws IOException {
        remove(modsDir(context), mod.id);
    }

    static void remove(File root, String id) throws IOException {
        File directory = directChild(root, id);
        deleteTree(directory);
    }

    private static File directChild(File root, String id) throws IOException {
        if (!ModManifest.isSafeId(id)) throw new IOException("Unsafe mod ID");
        File child = new File(root, id);
        if (!child.getCanonicalFile().getParentFile().equals(root.getCanonicalFile())) {
            throw new IOException("Mod path escapes the mod directory");
        }
        return child;
    }

    static ModManifest importMod(Context context, Uri source) throws IOException {
        File root = modsDir(context);
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Mod directory is unavailable");
        String displayName = displayName(context, source);
        File staging = new File(root, ".import-" + UUID.randomUUID());
        File archive = new File(context.getCacheDir(), "mod-import-" + UUID.randomUUID() + ".bin");
        boolean success = false;
        try {
            if (!staging.mkdir()) throw new IOException("Could not create import staging directory");
            copyUri(context, source, archive, MAX_ARCHIVE_BYTES);
            if (isZip(archive)) {
                extractArchive(archive, staging);
            } else {
                if (displayName.toLowerCase(Locale.ROOT).endsWith(".zip")) {
                    throw new IOException("The selected ZIP is damaged or unsupported");
                }
                importSingleFile(archive, staging, displayName);
            }
            ensureGeneratedManifest(staging, displayName);
            rejectWindowsBinaries(staging);
            validateNativeTree(staging);
            ModManifest manifest = ModManifest.parseValidated(staging, true);
            File target = directChild(root, manifest.id);
            if (target.exists()) throw new IOException("A mod with ID '" + manifest.id + "' is already installed");
            if (!staging.renameTo(target)) throw new IOException("Could not promote the imported mod");
            success = true;
            return ModManifest.read(target);
        } catch (ZipException e) {
            throw new IOException("Invalid or unsupported ZIP: " + safeMessage(e), e);
        } finally {
            archive.delete();
            if (!success) deleteTreeQuietly(staging);
        }
    }

    private static void copyUri(Context context, Uri source, File destination, long maximum) throws IOException {
        try (InputStream input = context.getContentResolver().openInputStream(source);
             OutputStream output = new BufferedOutputStream(new FileOutputStream(destination))) {
            if (input == null) throw new IOException("The selected file could not be opened");
            copyLimited(input, output, maximum, "Selected file");
        } catch (SecurityException e) {
            throw new IOException("File access was denied; select the file again", e);
        }
    }

    private static long copyLimited(InputStream input, OutputStream output, long maximum, String label)
            throws IOException {
        byte[] buffer = new byte[BUFFER];
        long total = 0;
        int read;
        while ((read = input.read(buffer)) >= 0) {
            total += read;
            if (total > maximum) throw new IOException(label + " exceeds the safe import size limit");
            output.write(buffer, 0, read);
        }
        return total;
    }

    private static boolean isZip(File file) throws IOException {
        byte[] magic = new byte[4];
        try (InputStream input = new FileInputStream(file)) {
            int count = input.read(magic);
            return count == 4 && magic[0] == 'P' && magic[1] == 'K'
                    && ((magic[2] == 3 && magic[3] == 4) || (magic[2] == 5 && magic[3] == 6)
                    || (magic[2] == 7 && magic[3] == 8));
        }
    }

    static void extractArchive(File archive, File staging) throws IOException {
        String prefix = "";
        int manifests = 0;
        try (ZipFile zip = new ZipFile(archive)) {
            if (zip.size() > MAX_ENTRIES) throw new IOException("ZIP contains too many entries");
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                String name = safeZipName(entry.getName());
                if (name.endsWith("/" + ModManifest.FILE_NAME) || name.equals(ModManifest.FILE_NAME)) {
                    ++manifests;
                    prefix = name.substring(0, name.length() - ModManifest.FILE_NAME.length());
                    if (prefix.split("/", -1).length > 2) {
                        throw new IOException("Mod metadata must be at the ZIP root or one top-level folder");
                    }
                }
            }
            if (manifests > 1) throw new IOException("ZIP contains more than one mod manifest");
            final String contentPrefix = prefix;
            Set<String> paths = new HashSet<>();
            long[] total = {0};
            entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                String name = safeZipName(entry.getName());
                if (!contentPrefix.isEmpty()) {
                    if (!name.startsWith(contentPrefix)) continue;
                    name = name.substring(contentPrefix.length());
                }
                if (name.isEmpty()) continue;
                if (name.equals(ModManifest.FILE_NAME) && contentPrefix.isEmpty()) {
                    // Extracted below like every other root file.
                }
                File output = new File(staging, name);
                String rootPath = staging.getCanonicalPath();
                String canonical = output.getCanonicalPath();
                if (!canonical.startsWith(rootPath + File.separator)) {
                    throw new IOException("ZIP entry escapes the staging directory");
                }
                String collisionKey = canonical.substring(rootPath.length() + 1).toLowerCase(Locale.ROOT);
                if (!paths.add(collisionKey)) {
                    throw new IOException("ZIP has a case-insensitive/canonical path collision: " + name);
                }
                if (entry.isDirectory()) {
                    if (!output.isDirectory() && !output.mkdirs()) throw new IOException("Could not create mod folder");
                    continue;
                }
                File parent = output.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Could not create mod folder");
                if (entry.getSize() > MAX_EXTRACTED_BYTES) throw new IOException("ZIP entry is too large");
                try (InputStream input = new BufferedInputStream(zip.getInputStream(entry));
                     OutputStream stream = new BufferedOutputStream(new FileOutputStream(output))) {
                    long written = copyLimited(input, stream, MAX_EXTRACTED_BYTES, "ZIP entry");
                    total[0] += written;
                    if (total[0] > MAX_EXTRACTED_BYTES) throw new IOException("ZIP expands beyond the safe limit");
                }
            }
        }
    }

    static String safeZipName(String name) throws IOException {
        if (name == null || name.isEmpty() || name.startsWith("/") || name.indexOf('\\') >= 0
                || name.indexOf(':') >= 0) throw new IOException("Unsafe ZIP entry path");
        for (int i = 0; i < name.length(); ++i) {
            if (Character.isISOControl(name.charAt(i))) throw new IOException("Control character in ZIP path");
        }
        String[] parts = name.split("/", -1);
        for (String part : parts) {
            if (part.equals("..")) throw new IOException("ZIP path traversal is not allowed");
        }
        while (name.endsWith("/")) name = name.substring(0, name.length() - 1);
        return name;
    }

    private static void importSingleFile(File source, File staging, String displayName) throws IOException {
        String safeName = ModManifest.safeRelativePath(displayName, false);
        if (safeName == null || safeName.contains("/")) throw new IOException("Unsafe selected filename");
        rejectWindowsBinary(safeName);
        String lower = safeName.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".dds")) {
            throw new IOException("A DDS needs a texture pack with textures.ini; loose DDS import is not a replacement map");
        }
        String targetName = lower.endsWith(".so") ? "lib/arm64-v8a/" + safeName : safeName;
        File target = new File(staging, targetName);
        if (!target.getParentFile().mkdirs() && !target.getParentFile().isDirectory()) {
            throw new IOException("Could not stage selected file");
        }
        try (InputStream input = new FileInputStream(source); OutputStream output = new FileOutputStream(target)) {
            copyLimited(input, output, MAX_ARCHIVE_BYTES, "Selected file");
        }
    }

    private static void ensureGeneratedManifest(File staging, String displayName) throws IOException {
        if (new File(staging, ModManifest.FILE_NAME).isFile()) return;
        rejectWindowsBinaries(staging);
        String textures = new File(staging, "textures.ini").isFile() ? "textures.ini" : "";
        String plugin = findSinglePlugin(staging);
        String files = "";
        if (textures.isEmpty() && plugin.isEmpty()) files = ".";
        else if (new File(staging, "files").isDirectory()) files = "files";
        String id = legacyId(displayName);
        ModManifest.writeGenerated(staging, id, displayName, files, textures, plugin);
    }

    private static String findSinglePlugin(File staging) throws IOException {
        File nativeDir = new File(staging, "lib/arm64-v8a");
        File[] files = nativeDir.listFiles();
        String found = "";
        if (files != null) {
            for (File file : files) {
                if (!file.isFile() || !file.getName().endsWith(".so")) continue;
                if (!found.isEmpty()) throw new IOException("Multiple native plugins need an explicit mod manifest");
                found = "lib/arm64-v8a/" + file.getName();
            }
        }
        return found;
    }

    static void rejectWindowsBinaries(File directory) throws IOException {
        File[] children = directory.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (child.isDirectory()) rejectWindowsBinaries(child);
            else rejectWindowsBinary(child.getName());
        }
    }

    private static void rejectWindowsBinary(String name) throws IOException {
        String lower = name.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".asi") || lower.endsWith(".dll") || lower.endsWith(".exe")) {
            throw new IOException("Windows ASI/DLL/EXE files are not Android mods; translate the plugin to native ARM64");
        }
    }

    private static void validateNativeTree(File staging) throws IOException {
        File nativeDir = new File(staging, "lib/arm64-v8a");
        if (!nativeDir.exists()) return;
        int[] count = {0};
        validateNativeDirectory(nativeDir, count);
        if (count[0] == 0) throw new IOException("Native library directory contains no ARM64 shared object");
    }

    private static void validateNativeDirectory(File directory, int[] count) throws IOException {
        File[] children = directory.listFiles();
        if (children == null) throw new IOException("Could not inspect native plugin files");
        for (File child : children) {
            if (Files.isSymbolicLink(child.toPath())) throw new IOException("Native plugin symlinks are not supported");
            if (child.isDirectory()) validateNativeDirectory(child, count);
            else if (child.getName().endsWith(".so")) {
                ModManifest.validateElf64Arm64(child);
                if (++count[0] > 32) throw new IOException("Too many native plugin libraries");
            }
        }
    }

    static void prepareForLaunch(Context context) {
        File root = modsDir(context);
        File nativeRoot = nativeLaunchDir(context);
        try {
            // Point native code at the fresh path before best-effort cleanup of any older launch copies.
            Os.setenv("PROTODROID_MODS", root.getAbsolutePath(), true);
            Os.setenv("PROTODROID_MOD_NATIVE_DIR", nativeRoot.getAbsolutePath(), true);
            File[] stale = nativeRoot.getParentFile().listFiles((directory, name) ->
                    name.equals("mod-native") || name.startsWith("mod-native-"));
            if (stale != null) {
                for (File old : stale) if (!old.equals(nativeRoot)) deleteTreeQuietly(old);
            }
            if (!nativeRoot.mkdirs()) throw new IOException("Could not create private plugin launch directory");
            for (ModManifest mod : list(context)) {
                if (!mod.enabled || mod.plugin.isEmpty()) continue;
                File destination = new File(nativeRoot, mod.id);
                try {
                    if (!mod.error.isEmpty()) throw new IOException(mod.error);
                    File source = new File(mod.directory, "lib/arm64-v8a");
                    String entry = nativeDirRelative(source, mod.plugin);
                    int[] count = {0};
                    copyNativeDirectory(source, destination, count);
                    if (!new File(destination, entry).isFile()) {
                        throw new IOException("Native plugin entry was not staged");
                    }
                    new File(mod.directory, "launch-error.txt").delete();
                } catch (IOException | RuntimeException e) {
                    deleteTreeQuietly(destination);
                    writeLaunchError(mod.directory, safeMessage(e));
                    Log.e(TAG, "Plugin preparation failed for " + mod.id, e);
                }
            }
        } catch (IOException | ErrnoException | RuntimeException e) {
            Log.e(TAG, "Mod launch preparation failed", e);
        }
    }

    private static String nativeDirRelative(File nativeDir, String plugin) throws IOException {
        String prefix = "lib/arm64-v8a/";
        if (!plugin.startsWith(prefix)) throw new IOException("Invalid native plugin path");
        return plugin.substring(prefix.length());
    }

    private static void copyNativeDirectory(File source, File destination, int[] count)
            throws IOException {
        if (!source.isDirectory()) throw new IOException("Native plugin directory is missing");
        if (!destination.mkdirs() && !destination.isDirectory()) throw new IOException("Could not stage native plugin");
        File[] children = source.listFiles();
        if (children == null) throw new IOException("Could not enumerate native plugin files");
        for (File child : children) {
            if (Files.isSymbolicLink(child.toPath())) throw new IOException("Native plugin symlinks are not supported");
            File target = new File(destination, child.getName());
            if (child.isDirectory()) {
                copyNativeDirectory(child, target, count);
            } else if (child.getName().endsWith(".so")) {
                ModManifest.validateElf64Arm64(child);
                copyFile(child, target);
                target.setReadOnly();
                if (++count[0] > 32) throw new IOException("Too many native plugin libraries");
            }
        }
    }

    private static void copyFile(File source, File target) throws IOException {
        try (InputStream input = new FileInputStream(source); OutputStream output = new FileOutputStream(target)) {
            copyLimited(input, output, MAX_ARCHIVE_BYTES, "Native plugin");
        }
    }

    private static void writeLaunchError(File directory, String message) {
        File outputFile = new File(directory, "launch-error.txt");
        // Unlink first so a manually installed symlink/hard link cannot turn a diagnostic into an original-file write.
        if ((outputFile.exists() || Files.isSymbolicLink(outputFile.toPath())) && !outputFile.delete()) return;
        try (FileOutputStream output = new FileOutputStream(outputFile)) {
            output.write((message + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
            // The native log remains authoritative when shared storage is no longer writable.
        }
    }

    private static String displayName(Context context, Uri uri) {
        String name = null;
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0) name = cursor.getString(column);
            }
        } catch (RuntimeException ignored) {
            // Fall back to the URI below.
        }
        if (name == null || name.trim().isEmpty()) name = uri.getLastPathSegment();
        if (name == null || name.trim().isEmpty()) name = "local-mod";
        name = new File(name).getName();
        return name.length() > 120 ? name.substring(name.length() - 120) : name;
    }

    private static String legacyId(String displayName) {
        String base = displayName.toLowerCase(Locale.ROOT).replaceAll("\\.(zip|so|bin|dat)$", "");
        base = base.replaceAll("[^a-z0-9._-]+", "-").replaceAll("^[^a-z0-9]+", "");
        if (base.length() > 42) base = base.substring(0, 42);
        if (base.isEmpty()) base = "mod";
        return "local." + base + "-" + UUID.randomUUID().toString().substring(0, 8);
    }

    private static void deleteTree(File file) throws IOException {
        if (!file.exists()) return;
        if (Files.isSymbolicLink(file.toPath())) {
            if (!file.delete()) throw new IOException("Could not remove symbolic link");
            return;
        }
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Could not enumerate " + file.getName());
            for (File child : children) deleteTree(child);
        }
        if (!file.delete()) throw new IOException("Could not remove " + file.getName());
    }

    private static void deleteTreeQuietly(File file) {
        try {
            deleteTree(file);
        } catch (IOException e) {
            Log.w(TAG, "Could not clean mod staging", e);
        }
    }

    private static String safeMessage(Exception e) {
        return e.getMessage() == null || e.getMessage().trim().isEmpty()
                ? e.getClass().getSimpleName() : e.getMessage();
    }
}
