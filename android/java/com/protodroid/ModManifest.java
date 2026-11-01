package com.protodroid;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Schema-1 mod metadata. Kept free of Android imports so its safety policy has host-side tests. */
final class ModManifest {
    static final int API_VERSION = 1;
    static final String FILE_NAME = "protodroid-mod.ini";
    static final String ENABLED_NAME = "enabled";
    static final String ENGINE_SHA256 = "d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071";
    private static final int MAX_METADATA_BYTES = 128 * 1024;

    final File directory;
    String id = "";
    String name = "";
    String version = "";
    String description = "";
    String author = "";
    String cover = "";
    String files = "";
    String textures = "";
    String plugin = "";
    String engineSha256 = "";
    String error = "";         // metadata/compatibility validation failure
    String launchError = "";  // previous native load failure; a valid package may retry on the next Play
    int priority = 100;
    int api = API_VERSION;
    boolean enabled;

    private ModManifest(File directory) {
        this.directory = directory;
    }

    static ModManifest read(File directory) {
        try {
            if (Files.isSymbolicLink(directory.toPath())) {
                throw new IOException("Mod folder must not be a symbolic link");
            }
            ModManifest manifest = parseValidated(directory, true);
            if (!manifest.id.equals(directory.getName())) {
                throw new IOException("Mod ID does not match its folder name");
            }
            manifest.enabled = new File(directory, ENABLED_NAME).isFile();
            File launchError = new File(directory, "launch-error.txt");
            if (launchError.isFile() && launchError.length() <= 4096) {
                List<String> lines = Files.readAllLines(launchError.toPath(), StandardCharsets.UTF_8);
                if (!lines.isEmpty() && !lines.get(0).trim().isEmpty()) {
                    manifest.launchError = "Last launch: " + lines.get(0).trim();
                }
            }
            return manifest;
        } catch (IOException | RuntimeException e) {
            ModManifest invalid = new ModManifest(directory);
            invalid.id = directory.getName();
            invalid.name = directory.getName();
            invalid.error = message(e);
            invalid.enabled = new File(directory, ENABLED_NAME).isFile();
            return invalid;
        }
    }

    static ModManifest parseValidated(File directory, boolean deep) throws IOException {
        File metadata = new File(directory, FILE_NAME);
        if (!metadata.isFile()) throw new IOException("Missing " + FILE_NAME);
        if (metadata.length() > MAX_METADATA_BYTES) throw new IOException("Mod metadata is too large");
        ModManifest result = new ModManifest(directory);
        Map<String, String> values = new HashMap<>();
        String section = "";
        try (BufferedReader reader = Files.newBufferedReader(metadata.toPath(), StandardCharsets.UTF_8)) {
            String line;
            int number = 0;
            while ((line = reader.readLine()) != null) {
                ++number;
                if (number == 1 && line.startsWith("\ufeff")) line = line.substring(1);
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) continue;
                if (line.startsWith("[") && line.endsWith("]")) {
                    section = line.substring(1, line.length() - 1).trim().toLowerCase(Locale.ROOT);
                    continue;
                }
                int equals = line.indexOf('=');
                if (equals < 1) throw new IOException("Invalid metadata line " + number);
                String key = line.substring(0, equals).trim().toLowerCase(Locale.ROOT);
                String value = line.substring(equals + 1).trim();
                String compound = section + "." + key;
                if (values.put(compound, value) != null) {
                    throw new IOException("Duplicate metadata key " + compound);
                }
            }
        }
        result.id = value(values, "mod.id");
        result.name = value(values, "mod.name");
        result.version = value(values, "mod.version");
        result.description = value(values, "mod.description");
        result.author = value(values, "mod.author");
        result.cover = value(values, "mod.cover");
        result.engineSha256 = value(values, "mod.engine_sha256").toLowerCase(Locale.ROOT);
        result.files = value(values, "content.files");
        result.textures = value(values, "content.textures");
        result.plugin = value(values, "content.plugin");
        String schema = value(values, "mod.schema");
        String api = value(values, "mod.api");
        String priority = value(values, "mod.priority");
        try {
            if (!schema.isEmpty() && Integer.parseInt(schema) != 1) {
                throw new IOException("Unsupported mod schema " + schema);
            }
            if (!api.isEmpty()) result.api = Integer.parseInt(api);
            if (!priority.isEmpty()) result.priority = Integer.parseInt(priority);
        } catch (NumberFormatException e) {
            throw new IOException("Schema, API and priority must be integers");
        }
        result.validate(deep);
        return result;
    }

    private static String value(Map<String, String> values, String key) {
        String value = values.get(key);
        return value == null ? "" : value;
    }

    private void validate(boolean deep) throws IOException {
        if (!isSafeId(id)) throw new IOException("Invalid mod ID; use lowercase letters, digits, '.', '_' or '-'");
        if (name.trim().isEmpty() || name.length() > 80 || hasControl(name)) {
            throw new IOException("Mod name is empty, too long or contains control characters");
        }
        if (version.length() > 40 || author.length() > 80 || description.length() > 600
                || hasControl(version) || hasControl(author) || hasControl(description)) {
            throw new IOException("Invalid or oversized mod metadata");
        }
        if (api != API_VERSION) throw new IOException("Requires mod API " + api + "; installed API is " + API_VERSION);
        if (priority < -10000 || priority > 10000) throw new IOException("Priority is outside the supported range");
        if (!engineSha256.isEmpty() && !ENGINE_SHA256.equals(engineSha256)) {
            throw new IOException("Built for a different engine; the installed Steam build is required");
        }
        cover = checkedRelative(cover, "cover", false);
        files = checkedRelative(files, "files", true);
        textures = checkedRelative(textures, "textures", false);
        plugin = checkedRelative(plugin, "plugin", false);
        if (files.isEmpty() && textures.isEmpty() && plugin.isEmpty()) {
            throw new IOException("Mod has no files, textures or native plugin content");
        }
        if (!plugin.isEmpty() && (!plugin.startsWith("lib/arm64-v8a/") || !plugin.endsWith(".so"))) {
            throw new IOException("Native plugin must be under lib/arm64-v8a and end in .so");
        }
        if (!textures.isEmpty() && !textures.endsWith(".ini")) {
            throw new IOException("Texture mapping must be an .ini file");
        }
        if (!cover.isEmpty() && !isImage(cover)) throw new IOException("Cover must be PNG, JPEG or WebP");
        if (!deep) return;
        if (!files.isEmpty() && !resolved(files).isDirectory()) {
            throw new IOException("Loose-file directory is missing: " + files);
        }
        if (!cover.isEmpty() && !resolved(cover).isFile()) throw new IOException("Cover image is missing");
        if (!plugin.isEmpty()) validateElf64Arm64(resolved(plugin));
        if (!textures.isEmpty()) validateTextures(resolved(textures));
    }

    private String checkedRelative(String value, String label, boolean allowDot) throws IOException {
        if (value.isEmpty()) return "";
        String normalized = safeRelativePath(value, allowDot);
        if (normalized == null) throw new IOException("Unsafe " + label + " path");
        // Resolve now as well as after ZIP extraction; a manually installed symlink must not escape the mod root.
        File resolved = resolved(normalized);
        try {
            String root = directory.getCanonicalPath();
            String candidate = resolved.getCanonicalPath();
            if (!candidate.equals(root) && !candidate.startsWith(root + File.separator)) {
                throw new IOException(label + " path escapes the mod folder");
            }
        } catch (IOException e) {
            throw e;
        }
        return normalized;
    }

    File resolved(String relative) {
        return relative.isEmpty() || relative.equals(".") ? directory : new File(directory, relative);
    }

    File coverFile() {
        return cover.isEmpty() ? null : resolved(cover);
    }

    String contentLabel() {
        List<String> kinds = new ArrayList<>();
        if (!files.isEmpty()) kinds.add("Loose files");
        if (!textures.isEmpty()) kinds.add("Textures");
        if (!plugin.isEmpty()) kinds.add("ARM64 plugin");
        return String.join(" · ", kinds);
    }

    static String safeRelativePath(String value, boolean allowDot) {
        if (value == null) return null;
        value = value.trim();
        if (allowDot && value.equals(".")) return value;
        if (value.isEmpty() || value.length() > 240 || value.startsWith("/") || value.startsWith("\\")
                || value.indexOf('\\') >= 0 || value.indexOf(':') >= 0 || hasControl(value)) return null;
        String[] parts = value.split("/", -1);
        StringBuilder out = new StringBuilder();
        for (String part : parts) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) return null;
            if (out.length() > 0) out.append('/');
            out.append(part);
        }
        return out.toString();
    }

    static boolean isSafeId(String id) {
        return id != null && id.matches("[a-z0-9][a-z0-9._-]{0,63}")
                && !id.endsWith(".tmp") && !id.endsWith(".previous") && !id.startsWith("import-");
    }

    static void validateElf64Arm64(File file) throws IOException {
        byte[] header = new byte[64];
        try (java.io.InputStream input = Files.newInputStream(file.toPath())) {
            int offset = 0;
            while (offset < header.length) {
                int read = input.read(header, offset, header.length - offset);
                if (read < 0) break;
                offset += read;
            }
            if (offset != header.length) throw new IOException("Native plugin is truncated");
        }
        if ((header[0] & 0xff) != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F'
                || header[4] != 2 || header[5] != 1) {
            throw new IOException("Native plugin is not a 64-bit little-endian ELF shared object");
        }
        int type = (header[16] & 0xff) | ((header[17] & 0xff) << 8);
        int machine = (header[18] & 0xff) | ((header[19] & 0xff) << 8);
        if (type != 3 || machine != 183) {
            throw new IOException(machine == 3 || machine == 40 || machine == 62
                    ? "Windows/x86 native binaries are not loadable; translate this plugin to ARM64"
                    : "Native plugin is not an ARM64 shared object");
        }
    }

    static void validateTextures(File ini) throws IOException {
        if (!ini.isFile()) throw new IOException("Texture mapping is missing");
        if (ini.length() > 4 * 1024 * 1024) throw new IOException("Texture mapping is too large");
        int count = 0;
        HashSet<String> keys = new HashSet<>();
        String section = "";
        try (BufferedReader reader = Files.newBufferedReader(ini.toPath(), StandardCharsets.UTF_8)) {
            String line;
            while ((line = reader.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) continue;
                if (line.startsWith("[") && line.endsWith("]")) {
                    section = line.substring(1, line.length() - 1).trim().toLowerCase(Locale.ROOT);
                    continue;
                }
                if (!section.isEmpty() && !section.equals("hashes")) continue;
                int equals = line.indexOf('=');
                if (equals < 1) throw new IOException("Invalid texture mapping line");
                String hash = line.substring(0, equals).trim().toLowerCase(Locale.ROOT);
                String relative = safeRelativePath(line.substring(equals + 1).trim(), false);
                if (!hash.matches("[0-9a-f]{64}")) {
                    throw new IOException("Texture keys must be ProtoDroid SHA-256 hashes, not PPSSPP hashes");
                }
                if (!keys.add(hash)) throw new IOException("Duplicate texture key " + hash);
                if (relative == null || !relative.toLowerCase(Locale.ROOT).endsWith(".dds")) {
                    throw new IOException("Texture replacement must be a safe relative .dds path");
                }
                File dds = new File(ini.getParentFile(), relative);
                String root = ini.getParentFile().getCanonicalPath();
                if (!dds.getCanonicalPath().startsWith(root + File.separator) || !dds.isFile()) {
                    throw new IOException("Texture replacement is missing or unsafe: " + relative);
                }
                validateDds(dds);
                if (++count > 4096) throw new IOException("Texture mapping contains too many replacements");
            }
        }
        if (count == 0) throw new IOException("Texture mapping contains no replacements");
    }

    static void validateDds(File file) throws IOException {
        final long maximum = 128L * 1024L * 1024L;
        long length = file.length();
        if (length < 128 || length > maximum) throw new IOException("Invalid or oversized DDS: " + file.getName());
        byte[] h = new byte[128];
        try (java.io.InputStream input = Files.newInputStream(file.toPath())) {
            int offset = 0;
            while (offset < h.length) {
                int read = input.read(h, offset, h.length - offset);
                if (read < 0) break;
                offset += read;
            }
            if (offset != h.length) throw new IOException("Invalid DDS header: " + file.getName());
        }
        if (h[0] != 'D' || h[1] != 'D' || h[2] != 'S' || h[3] != ' '
                || u32(h, 4) != 124 || u32(h, 76) != 32) {
            throw new IOException("Invalid DDS header: " + file.getName());
        }
        long height = u32(h, 12), width = u32(h, 16), depth = u32(h, 24);
        long levels = Math.max(1, u32(h, 28));
        if (width == 0 || height == 0 || width > 32768 || height > 32768 || depth > 1
                || levels > 32 || u32(h, 112) != 0) {
            throw new IOException("Only bounded 2D DDS textures are supported: " + file.getName());
        }
        long flags = u32(h, 80), fourcc = u32(h, 84);
        long dxt1 = fourcc('D', 'X', 'T', '1'), dxt3 = fourcc('D', 'X', 'T', '3');
        long dxt5 = fourcc('D', 'X', 'T', '5');
        boolean compressed = (flags & 4) != 0 && (fourcc == dxt1 || fourcc == dxt3 || fourcc == dxt5);
        boolean rgba = (flags & 0x40) != 0 && u32(h, 88) == 32 && u32(h, 92) == 0x00ff0000L
                && u32(h, 96) == 0x0000ff00L && u32(h, 100) == 0x000000ffL
                && (u32(h, 104) == 0 || u32(h, 104) == 0xff000000L);
        if (!compressed && !rgba) throw new IOException("Unsupported DDS format: " + file.getName());
        long expected = 128;
        for (int level = 0; level < levels; ++level) {
            long w = Math.max(1, width >> level), hgt = Math.max(1, height >> level);
            long pitch, rows;
            if (compressed) {
                pitch = ((w + 3) / 4) * (fourcc == dxt1 ? 8 : 16);
                rows = (hgt + 3) / 4;
            } else {
                pitch = w * 4;
                rows = hgt;
            }
            long size = pitch * rows;
            if (size <= 0 || size > maximum || expected + size > length) {
                throw new IOException("Truncated DDS mip chain: " + file.getName());
            }
            expected += size;
        }
    }

    private static long u32(byte[] data, int offset) {
        return (data[offset] & 0xffL) | ((data[offset + 1] & 0xffL) << 8)
                | ((data[offset + 2] & 0xffL) << 16) | ((data[offset + 3] & 0xffL) << 24);
    }

    private static long fourcc(char a, char b, char c, char d) {
        return (a & 0xffL) | ((b & 0xffL) << 8) | ((c & 0xffL) << 16) | ((d & 0xffL) << 24);
    }

    static void writeGenerated(File directory, String id, String name, String files, String textures,
                               String plugin) throws IOException {
        File output = new File(directory, FILE_NAME);
        try (java.io.OutputStreamWriter writer = new java.io.OutputStreamWriter(
                new java.io.FileOutputStream(output), StandardCharsets.UTF_8)) {
            writer.write("[mod]\nschema = 1\napi = 1\nid = " + id + "\n");
            writer.write("name = " + sanitizeMetadata(name, 80) + "\n");
            writer.write("version = local\nauthor = Local import\n");
            writer.write("description = Imported local mod. Review its content before enabling.\npriority = 100\n\n");
            writer.write("[content]\n");
            if (!files.isEmpty()) writer.write("files = " + files + "\n");
            if (!textures.isEmpty()) writer.write("textures = " + textures + "\n");
            if (!plugin.isEmpty()) writer.write("plugin = " + plugin + "\n");
        }
    }

    private static String sanitizeMetadata(String value, int maximum) {
        StringBuilder result = new StringBuilder();
        for (int i = 0; i < value.length() && result.length() < maximum; ++i) {
            char c = value.charAt(i);
            result.append(c < 32 || c == 127 || c == '\n' || c == '\r' ? ' ' : c);
        }
        return result.toString().trim();
    }

    private static boolean isImage(String path) {
        String lower = path.toLowerCase(Locale.ROOT);
        return lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".webp");
    }

    private static boolean hasControl(String value) {
        for (int i = 0; i < value.length(); ++i) if (Character.isISOControl(value.charAt(i))) return true;
        return false;
    }

    private static String message(Exception e) {
        String message = e.getMessage();
        return message == null || message.trim().isEmpty() ? e.getClass().getSimpleName() : message;
    }
}
