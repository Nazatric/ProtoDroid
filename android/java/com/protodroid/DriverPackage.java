package com.protodroid;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.PushbackInputStream;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Validated, per-app Android/Bionic Vulkan driver packages. All installs are staged before replacement. */
final class DriverPackage {
    static final long MAX_LIBRARY_SIZE = 96L * 1024 * 1024;
    static final long MAX_TOTAL_SIZE = 256L * 1024 * 1024;
    private static final int MAX_ENTRIES = 128;
    private static final int MAX_TEXT_SIZE = 1024 * 1024;

    static final class Installed {
        final File root;
        final String name, version, library, source, sha256;

        Installed(File root, String name, String version, String library, String source, String sha256) {
            this.root = root;
            this.name = name;
            this.version = version;
            this.library = library;
            this.source = source;
            this.sha256 = sha256;
        }

        File libraryFile() {
            return new File(root, library);
        }

        String title() {
            return version.isEmpty() ? name : name + "\n" + version;
        }
    }

    /** Returns null for an absent or invalid installation; native launch will use its normal fallback. */
    static Installed read(File root) {
        try {
            File parent = root.getParentFile();
            File backup = parent == null ? null : new File(parent, ".driver-previous");
            if (!root.exists() && backup != null && backup.exists()) backup.renameTo(root);
            JSONObject about = new JSONObject(readText(new File(root, "driver.json")));
            if (about.optInt("format") != 1) return null;
            String library = about.getString("library");
            if (!library.equals(baseName(library)) || !library.matches("[A-Za-z0-9._+-]+\\.so(?:\\.[0-9]+)*")) {
                return null;
            }
            File binary = new File(root, library);
            if (!binary.isFile() || binary.length() < 64 || binary.length() > MAX_LIBRARY_SIZE) return null;
            return new Installed(root, about.optString("name", "Custom driver"),
                    about.optString("version"), library, about.optString("source"), about.optString("sha256"));
        } catch (Exception e) {
            return null;
        }
    }

    static Installed install(InputStream input, String sourceName, File root, int currentApi) throws IOException {
        File parent = root.getParentFile();
        if (parent == null || (!parent.isDirectory() && !parent.mkdirs())) {
            throw new IOException("Cannot create the private driver folder");
        }
        File staging = new File(parent, ".driver-install");
        File backup = new File(parent, ".driver-previous");
        // Recover an interrupted directory swap before cleaning anything. Never discard the only good old package.
        if (!root.exists() && backup.exists() && !backup.renameTo(root)) {
            throw new IOException("The previous driver is preserved but could not be restored");
        }
        deleteTree(staging);
        if (root.exists()) deleteTree(backup);
        if (!staging.mkdirs()) throw new IOException("Cannot create the driver staging folder");

        boolean movedOld = false;
        try {
            PushbackInputStream prefix = new PushbackInputStream(input, 4);
            byte[] magic = new byte[4];
            int count = prefix.read(magic);
            if (count > 0) prefix.unread(magic, 0, count);
            boolean zip = count == 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4;
            Metadata metadata = zip ? installZip(prefix, sourceName, staging, currentApi)
                    : installLibrary(prefix, sourceName, staging);
            JSONObject about = new JSONObject();
            about.put("format", 1);
            about.put("name", metadata.name);
            about.put("version", metadata.version);
            about.put("library", metadata.library);
            about.put("source", metadata.source);
            about.put("sha256", metadata.sha256);
            writeText(new File(staging, "driver.json"), about.toString(2));

            if (root.exists()) {
                if (!root.renameTo(backup)) throw new IOException("Cannot prepare the previous driver for replacement");
                movedOld = true;
            }
            if (!staging.renameTo(root)) {
                if (movedOld) backup.renameTo(root);
                throw new IOException("Cannot activate the imported driver");
            }
            if (movedOld) deleteTree(backup);
            return new Installed(root, metadata.name, metadata.version, metadata.library,
                    metadata.source, metadata.sha256);
        } catch (JSONException e) {
            throw new IOException("Invalid driver metadata", e);
        } finally {
            if (backup.exists() && !root.exists() && !backup.renameTo(root)) {
                // Leave .driver-previous in place for the next launch/install recovery attempt.
                throw new IOException("The new driver could not be activated; the previous driver is preserved for recovery");
            }
            deleteTree(staging);
            if (backup.exists()) deleteTree(backup);
        }
    }

    static void deleteTree(File file) throws IOException {
        if (!file.exists() && !Files.isSymbolicLink(file.toPath())) return;
        if (file.isDirectory() && !Files.isSymbolicLink(file.toPath())) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteTree(child);
        }
        if (!file.delete() && file.exists()) throw new IOException("Cannot delete " + file.getName());
    }

    private static final class Metadata {
        String name, version, library, source, sha256;
    }

    private static Metadata installZip(InputStream input, String sourceName, File staging, int currentApi)
            throws IOException {
        List<String> libraries = new ArrayList<>();
        JSONObject meta = null;
        String checksums = null;
        long total = 0;
        int entries = 0;
        byte[] buffer = new byte[64 * 1024];
        try (ZipInputStream zip = new ZipInputStream(input)) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null;) {
                if (++entries > MAX_ENTRIES) throw new IOException("The driver ZIP has too many entries");
                if (entry.isDirectory()) continue;
                String path = entry.getName().replace('\\', '/');
                if (path.startsWith("/") || hasParentSegment(path)) {
                    throw new IOException("The driver ZIP contains an unsafe path");
                }
                String name = baseName(path);
                if (name.isEmpty()) continue;
                boolean library = name.matches("[A-Za-z0-9._+-]+\\.so(?:\\.[0-9]+)*");
                boolean json = name.equals("meta.json");
                boolean sums = name.equals("SHA256SUMS.txt") || name.equals("SHA256SUMS");
                boolean documentation = name.matches("(?i)(LICENSE|NOTICE|COPYING|README)[A-Za-z0-9._-]*");
                if (!library && !json && !sums && !documentation) continue;
                if (libraries.contains(name) || (json && meta != null) || (sums && checksums != null)) {
                    throw new IOException("The driver ZIP has duplicate files: " + name);
                }
                if (library) {
                    File output = new File(staging, name);
                    long written = copy(zip, output, MAX_LIBRARY_SIZE, buffer);
                    total += written;
                    if (total > MAX_TOTAL_SIZE) throw new IOException("The driver ZIP is too large");
                    libraries.add(name);
                } else {
                    String value = readLimited(zip, MAX_TEXT_SIZE, buffer);
                    if (json) {
                        try {
                            meta = new JSONObject(value);
                        } catch (JSONException e) {
                            throw new IOException("meta.json is invalid", e);
                        }
                        writeText(new File(staging, name), value);
                    } else {
                        if (sums) checksums = value;
                        writeText(new File(staging, name), value);
                    }
                }
            }
        }
        if (libraries.isEmpty()) throw new IOException("The ZIP has no Vulkan driver .so");
        if (meta != null && meta.optInt("minApi", 0) > currentApi) {
            throw new IOException("This driver needs Android API " + meta.getInt("minApi") + " or newer");
        }
        String library = pickLibrary(libraries, meta);
        if (library == null) {
            throw new IOException("The ZIP does not identify one Vulkan driver; provide meta.json with libraryName");
        }
        validateAndroidElf(new File(staging, library));
        String actual = sha256(new File(staging, library));
        String expected = meta == null ? "" : meta.optString("sha256").trim();
        if (!expected.isEmpty() && !expected.equalsIgnoreCase(actual)) {
            throw new IOException("The driver does not match the checksum in meta.json");
        }
        verifyChecksumFile(checksums, library, actual);

        Metadata result = new Metadata();
        result.library = library;
        result.sha256 = actual;
        result.source = clean(sourceName, "Imported ZIP");
        result.name = clean(meta == null ? "" : meta.optString("name"), removeExtension(result.source));
        result.version = clean(meta == null ? "" : meta.optString("driverVersion",
                meta.optString("packageVersion")), "");
        return result;
    }

    private static Metadata installLibrary(InputStream input, String sourceName, File staging) throws IOException {
        String requested = baseName(sourceName == null ? "" : sourceName);
        String library = requested.matches("[A-Za-z0-9._+-]+\\.so(?:\\.[0-9]+)*")
                ? requested : "libvulkan_custom.so";
        copy(input, new File(staging, library), MAX_LIBRARY_SIZE, new byte[64 * 1024]);
        validateAndroidElf(new File(staging, library));
        Metadata result = new Metadata();
        result.library = library;
        result.sha256 = sha256(new File(staging, library));
        result.source = clean(sourceName, "Imported driver");
        result.name = removeExtension(result.source);
        result.version = "";
        return result;
    }

    private static String pickLibrary(List<String> libraries, JSONObject meta) {
        if (meta != null) {
            String named = baseName(meta.optString("libraryName"));
            if (!named.isEmpty() && libraries.contains(named)) return named;
        }
        for (String preferred : new String[] {"libvulkan_freedreno.so", "libvulkan_panfrost.so"}) {
            if (libraries.contains(preferred)) return preferred;
        }
        String found = null;
        for (String library : libraries) {
            if (!library.startsWith("libvulkan") && !library.startsWith("vulkan.")) continue;
            if (found != null) return null;
            found = library;
        }
        if (found == null && libraries.size() == 1) found = libraries.get(0);
        return found;
    }

    /** ELF64, little-endian, ET_DYN, AArch64, with no glibc dependency. */
    static void validateAndroidElf(File file) throws IOException {
        long length = file.length();
        if (length < 64 || length > MAX_LIBRARY_SIZE) throw new IOException("The driver library has an invalid size");
        try (RandomAccessFile random = new RandomAccessFile(file, "r"); FileChannel channel = random.getChannel()) {
            ByteBuffer elf = channel.map(FileChannel.MapMode.READ_ONLY, 0, length).order(ByteOrder.LITTLE_ENDIAN);
            if (elf.getInt(0) != 0x464c457f || elf.get(4) != 2 || elf.get(5) != 1) {
                throw new IOException("The driver is not a 64-bit little-endian ELF library");
            }
            if (elf.getShort(16) != 3 || elf.getShort(18) != 183) {
                throw new IOException("The driver is not an ARM64 (AArch64) shared library");
            }
            long phoff = elf.getLong(32);
            int entrySize = elf.getShort(54) & 0xffff;
            int entryCount = elf.getShort(56) & 0xffff;
            long programBytes = (long) entrySize * entryCount;
            if (entrySize < 56 || entryCount > 1024 || phoff < 0
                    || phoff > length || programBytes > length - phoff) {
                throw new IOException("The driver's ELF program headers are damaged");
            }
            List<long[]> loads = new ArrayList<>();
            long dynamic = -1, dynamicSize = 0;
            for (int i = 0; i < entryCount; ++i) {
                int at = Math.toIntExact(phoff + (long) i * entrySize);
                int type = elf.getInt(at);
                if (type == 1) {
                    long offset = elf.getLong(at + 8), address = elf.getLong(at + 16), size = elf.getLong(at + 32);
                    if (offset < 0 || address < 0 || size < 0 || offset > length || size > length - offset) {
                        throw new IOException("The driver's ELF load segments are damaged");
                    }
                    loads.add(new long[] {offset, address, size});
                }
                if (type == 2) {
                    dynamic = elf.getLong(at + 8);
                    dynamicSize = elf.getLong(at + 32);
                }
            }
            if (dynamic < 0 || dynamicSize < 16 || dynamic > length || dynamicSize > length - dynamic) {
                throw new IOException("The driver has no valid dynamic section");
            }
            long stringAddress = -1, stringSize = -1;
            List<Long> neededOffsets = new ArrayList<>();
            for (long at = dynamic; at + 16 <= dynamic + dynamicSize; at += 16) {
                long tag = elf.getLong(Math.toIntExact(at));
                long value = elf.getLong(Math.toIntExact(at + 8));
                if (tag == 0) break;
                if (tag == 1) neededOffsets.add(value);
                else if (tag == 5) stringAddress = value;
                else if (tag == 10) stringSize = value;
            }
            long strings = -1;
            if (stringAddress >= 0 && stringSize >= 0) {
                for (long[] load : loads) {
                    long offset = load[0], address = load[1], size = load[2];
                    if (stringAddress < address) continue;
                    long relative = stringAddress - address;
                    if (relative <= size && stringSize <= size - relative) {
                        strings = offset + relative;
                        break;
                    }
                }
            }
            if (strings < 0 || stringSize < 0 || strings > length || stringSize > length - strings) {
                throw new IOException("The driver's dynamic string table is damaged");
            }
            for (long offset : neededOffsets) {
                if (offset < 0 || offset >= stringSize) throw new IOException("The driver's dependencies are damaged");
                String needed = cString(elf, Math.toIntExact(strings + offset), Math.toIntExact(stringSize - offset));
                if (needed.equals("libc.so.6") || needed.startsWith("libstdc++.so")) {
                    throw new IOException("This is a Linux/glibc driver; install an Android/Bionic ARM64 build");
                }
            }
        } catch (IndexOutOfBoundsException | IllegalArgumentException e) {
            throw new IOException("The driver's ELF data is damaged", e);
        }
    }

    private static String cString(ByteBuffer data, int at, int maximum) {
        int end = at;
        int limit = Math.min(data.limit(), at + maximum);
        while (end < limit && data.get(end) != 0) ++end;
        byte[] bytes = new byte[end - at];
        for (int i = 0; i < bytes.length; ++i) bytes[i] = data.get(at + i);
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private static void verifyChecksumFile(String checksums, String library, String actual) throws IOException {
        if (checksums == null) return;
        for (String line : checksums.split("\\R")) {
            String[] parts = line.trim().split("\\s+");
            if (parts.length < 2 || !parts[0].matches("(?i)[0-9a-f]{64}")) continue;
            String named = parts[parts.length - 1].replaceFirst("^\\*", "");
            if (baseName(named).equals(library) && !parts[0].equalsIgnoreCase(actual)) {
                throw new IOException("The driver does not match SHA256SUMS.txt");
            }
        }
    }

    private static long copy(InputStream input, File output, long maximum, byte[] buffer) throws IOException {
        long total = 0;
        try (FileOutputStream out = new FileOutputStream(output)) {
            for (int count; (count = input.read(buffer)) >= 0;) {
                if (Thread.currentThread().isInterrupted()) throw new IOException("Driver import interrupted");
                if (count == 0) continue;
                total += count;
                if (total > maximum) throw new IOException(output.getName() + " is too large");
                out.write(buffer, 0, count);
            }
            out.getFD().sync();
        }
        if (total == 0) throw new IOException(output.getName() + " is empty");
        return total;
    }

    private static String readLimited(InputStream input, int maximum, byte[] buffer) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (int count; (count = input.read(buffer)) >= 0;) {
            if (Thread.currentThread().isInterrupted()) throw new IOException("Driver import interrupted");
            if (count == 0) continue;
            out.write(buffer, 0, count);
            if (out.size() > maximum) throw new IOException("A driver metadata file is too large");
        }
        return out.toString("UTF-8");
    }

    private static String sha256(File file) throws IOException {
        try (InputStream input = new FileInputStream(file)) {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[64 * 1024];
            for (int count; (count = input.read(buffer)) >= 0;) if (count > 0) digest.update(buffer, 0, count);
            StringBuilder out = new StringBuilder(64);
            for (byte value : digest.digest()) out.append(String.format(Locale.ROOT, "%02x", value & 0xff));
            return out.toString();
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException("SHA-256 is unavailable", e);
        }
    }

    private static boolean hasParentSegment(String path) {
        for (String part : path.split("/")) if (part.equals("..")) return true;
        return false;
    }

    private static String baseName(String path) {
        if (path == null) return "";
        String normalized = path.replace('\\', '/');
        int slash = normalized.lastIndexOf('/');
        return slash < 0 ? normalized : normalized.substring(slash + 1);
    }

    private static String removeExtension(String name) {
        return name.replaceFirst("(?i)\\.(zip|so)$", "");
    }

    private static String clean(String value, String fallback) {
        String text = value == null ? "" : value.replaceAll("[\\p{Cntrl}]", "").trim();
        if (text.isEmpty()) text = fallback == null ? "" : fallback;
        return text.length() > 160 ? text.substring(0, 160) : text;
    }

    private static String readText(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }

    private static void writeText(File file, String value) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(value.getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
    }

    private DriverPackage() {}
}
