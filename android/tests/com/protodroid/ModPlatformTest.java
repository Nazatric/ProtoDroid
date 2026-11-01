package com.protodroid;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Host-side schema, archive and lifecycle policy tests. Android types are only linkage references in these paths. */
public final class ModPlatformTest {
    private static int checks;

    public static void main(String[] args) throws Exception {
        Path temporary = Files.createTempDirectory("protodroid-mod-test");
        try {
            testManifestAndLifecycle(temporary.resolve("lifecycle"));
            testElf(temporary.resolve("elf"));
            testTextures(temporary.resolve("textures"));
            testArchives(temporary.resolve("archives"));
            System.out.println("ProtoDroid mod platform tests passed: " + checks + " checks");
        } finally {
            Files.walk(temporary).sorted(Comparator.reverseOrder()).forEach(path -> {
                try { Files.deleteIfExists(path); } catch (IOException ignored) { }
            });
        }
    }

    private static void testManifestAndLifecycle(Path path) throws Exception {
        File root = path.toFile();
        check(root.mkdirs(), "lifecycle root");
        File outside = new File(root, "shared-save.sav");
        Files.writeString(outside.toPath(), "keep", StandardCharsets.UTF_8);
        File mod = createMod(root, "example.files", "files = files\n", true);
        ModManifest parsed = ModManifest.read(mod);
        check(parsed.error.isEmpty(), "valid manifest: " + parsed.error);
        check(!parsed.enabled && parsed.files.equals("files"), "imports/default manifest are disabled");
        Files.writeString(new File(mod, "launch-error.txt").toPath(), "dlopen failed\n");
        ModManifest retry = ModManifest.read(mod);
        check(retry.error.isEmpty() && retry.launchError.contains("dlopen"), "launch error is not incompatibility");

        ModManager.setEnabled(root, parsed.id, true);
        check(new File(mod, ModManifest.ENABLED_NAME).isFile(), "enable writes marker");
        check(!new File(mod, "launch-error.txt").exists(), "enabling clears stale launch failure for retry");
        check(ModManifest.read(mod).enabled, "enabled marker is visible");
        ModManager.setEnabled(root, parsed.id, false);
        check(!new File(mod, ModManifest.ENABLED_NAME).exists(), "disable removes marker");
        expectIOException(() -> ModManager.setEnabled(root, "../outside", true), "unsafe enable ID");
        if (System.getProperty("os.name").toLowerCase().contains("linux")) {
            Files.createSymbolicLink(new File(mod, "enabled.tmp").toPath(), outside.toPath());
            ModManager.setEnabled(root, parsed.id, true);
            check(Files.readString(outside.toPath()).equals("keep"), "enable marker never follows a symlink");
            ModManager.setEnabled(root, parsed.id, false);
        }
        ModManager.remove(root, parsed.id);
        check(!mod.exists() && outside.isFile(), "removal is contained and preserves external files");
        expectIOException(() -> ModManager.remove(root, "not installed"), "missing mod ID is rejected safely");

        File wrongEngine = createMod(root, "wrong.engine", "files = files\n", true);
        String metadata = Files.readString(new File(wrongEngine, ModManifest.FILE_NAME).toPath());
        Files.writeString(new File(wrongEngine, ModManifest.FILE_NAME).toPath(),
                metadata.replace("name = Test mod", "name = Test mod\nengine_sha256 = "
                        + "0000000000000000000000000000000000000000000000000000000000000000"));
        check(ModManifest.read(wrongEngine).error.contains("different engine"), "engine mismatch is visible");

        File traversal = createMod(root, "unsafe.files", "files = ../game\n", false);
        check(!ModManifest.read(traversal).error.isEmpty(), "loose-file traversal is rejected");
        File empty = createMod(root, "empty.mod", "", false);
        check(ModManifest.read(empty).error.contains("no files"), "empty package is rejected");

        if (System.getProperty("os.name").toLowerCase().contains("linux")) {
            Path externalDirectory = Files.createDirectory(path.getParent().resolve("external"));
            Files.writeString(externalDirectory.resolve("keep.bin"), "keep");
            Path link = path.resolve("linked.mod");
            Files.createSymbolicLink(link, externalDirectory);
            check(ModManifest.read(link.toFile()).error.contains("symbolic link"),
                    "symlinked mod root is not launchable");
            expectIOException(() -> ModManager.remove(root, "linked.mod"), "symlinked mod removal is rejected");
            check(Files.exists(externalDirectory.resolve("keep.bin")), "symlink target survives removal attempt");
        }
    }

    private static void testElf(Path path) throws Exception {
        Files.createDirectories(path);
        byte[] elf = new byte[64];
        elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F'; elf[4] = 2; elf[5] = 1;
        elf[16] = 3;
        elf[18] = (byte) 183;
        File arm64 = path.resolve("libexample.so").toFile();
        Files.write(arm64.toPath(), elf);
        ModManifest.validateElf64Arm64(arm64);
        elf[18] = 62;
        File x86 = path.resolve("windows.so").toFile();
        Files.write(x86.toPath(), elf);
        expectIOException(() -> ModManifest.validateElf64Arm64(x86), "x86 ELF is not ARM64");
        Files.write(path.resolve("short.so"), new byte[12]);
        expectIOException(() -> ModManifest.validateElf64Arm64(path.resolve("short.so").toFile()),
                "truncated ELF is rejected");
    }

    private static void testTextures(Path path) throws Exception {
        File root = path.toFile();
        check(root.mkdirs(), "texture root");
        File mod = createMod(root, "example.textures", "textures = textures.ini\n", false);
        String hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        Files.writeString(new File(mod, "textures.ini").toPath(), "[hashes]\n" + hash + " = replacement.dds\n");
        Files.write(new File(mod, "replacement.dds").toPath(), dds(true));
        check(ModManifest.parseValidated(mod, true).textures.equals("textures.ini"), "valid DDS mapping");

        Files.writeString(new File(mod, "textures.ini").toPath(), hash + "=" + "replacement.dds\n"
                + hash + " = replacement.dds\n");
        expectIOException(() -> ModManifest.parseValidated(mod, true), "duplicate texture key");
        Files.writeString(new File(mod, "textures.ini").toPath(), "not-a-ppsspp-hash = replacement.dds\n");
        expectIOException(() -> ModManifest.parseValidated(mod, true), "non-SHA texture key");
        Files.writeString(new File(mod, "textures.ini").toPath(), hash + " = replacement.dds\n");
        Files.write(new File(mod, "replacement.dds").toPath(), new byte[128]);
        expectIOException(() -> ModManifest.parseValidated(mod, true), "truncated DDS data");
    }

    private static void testArchives(Path path) throws Exception {
        Files.createDirectories(path);
        expectIOException(() -> ModManager.safeZipName("../escape.bin"), "ZIP parent traversal");
        expectIOException(() -> ModManager.safeZipName("C:\\escape.bin"), "ZIP drive/backslash path");
        expectIOException(() -> ModManager.safeZipName("/absolute.bin"), "ZIP absolute path");

        File valid = zip(path.resolve("valid.zip"), new String[] {
                "pack/protodroid-mod.ini", manifest("archive.files", "files = files\n"),
                "pack/files/data.bin", "payload", "ignored/other.bin", "ignored"});
        File output = Files.createDirectory(path.resolve("valid-out")).toFile();
        ModManager.extractArchive(valid, output);
        check(new File(output, "files/data.bin").isFile(), "single top-level ZIP prefix is stripped");
        check(!new File(output, "ignored/other.bin").exists(), "unrelated ZIP roots are not imported");

        File collision = zip(path.resolve("collision.zip"), new String[] {
                "protodroid-mod.ini", manifest("collision.mod", "files = files\n"),
                "files/A.bin", "one", "files/a.bin", "two"});
        expectIOException(() -> ModManager.extractArchive(collision,
                Files.createDirectory(path.resolve("collision-out")).toFile()), "case-insensitive ZIP collision");

        File canonical = zip(path.resolve("canonical.zip"), new String[] {
                "protodroid-mod.ini", manifest("canonical.mod", "files = files\n"),
                "files//data.bin", "one", "files/data.bin", "two"});
        expectIOException(() -> ModManager.extractArchive(canonical,
                Files.createDirectory(path.resolve("canonical-out")).toFile()), "canonical ZIP alias collision");

        File multiple = zip(path.resolve("multiple.zip"), new String[] {
                "protodroid-mod.ini", manifest("one.mod", "files = files\n"),
                "two/protodroid-mod.ini", manifest("two.mod", "files = files\n")});
        expectIOException(() -> ModManager.extractArchive(multiple,
                Files.createDirectory(path.resolve("multiple-out")).toFile()), "multiple manifests");

        File windows = Files.createDirectory(path.resolve("windows-pack")).toFile();
        Files.write(new File(windows, "PrototypeFix.asi").toPath(), new byte[] {1, 2, 3});
        expectIOException(() -> ModManager.rejectWindowsBinaries(windows), "explicit ASI rejection");
    }

    private static File createMod(File root, String id, String content, boolean files) throws IOException {
        File mod = new File(root, id);
        if (files && !new File(mod, "files").mkdirs()) throw new IOException("create mod files");
        if (!files && !mod.mkdirs()) throw new IOException("create mod");
        Files.writeString(new File(mod, ModManifest.FILE_NAME).toPath(), manifest(id, content));
        return mod;
    }

    private static String manifest(String id, String content) {
        return "[mod]\nschema = 1\napi = 1\nid = " + id + "\nname = Test mod\n"
                + "description = Host policy fixture\n\n[content]\n" + content;
    }

    private static byte[] dds(boolean compressed) {
        byte[] data = new byte[compressed ? 136 : 192];
        data[0] = 'D'; data[1] = 'D'; data[2] = 'S'; data[3] = ' ';
        put32(data, 4, 124); put32(data, 12, 4); put32(data, 16, 4); put32(data, 28, 1);
        put32(data, 76, 32); put32(data, 108, 0x1000);
        if (compressed) {
            put32(data, 80, 4); put32(data, 84, 0x31545844); // DXT1, one 4x4 block
        } else {
            put32(data, 80, 0x41); put32(data, 88, 32); put32(data, 92, 0x00ff0000);
            put32(data, 96, 0x0000ff00); put32(data, 100, 0x000000ff); put32(data, 104, 0xff000000);
        }
        return data;
    }

    private static void put32(byte[] data, int offset, int value) {
        data[offset] = (byte) value; data[offset + 1] = (byte) (value >>> 8);
        data[offset + 2] = (byte) (value >>> 16); data[offset + 3] = (byte) (value >>> 24);
    }

    private static File zip(Path path, String[] entries) throws IOException {
        try (ZipOutputStream output = new ZipOutputStream(Files.newOutputStream(path))) {
            for (int i = 0; i < entries.length; i += 2) {
                output.putNextEntry(new ZipEntry(entries[i]));
                output.write(entries[i + 1].getBytes(StandardCharsets.UTF_8));
                output.closeEntry();
            }
        }
        return path.toFile();
    }

    private static void check(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }

    private static void expectIOException(ThrowingRunnable action, String message) {
        ++checks;
        try {
            action.run();
            throw new AssertionError("Expected IOException: " + message);
        } catch (IOException expected) {
            // expected
        } catch (Exception other) {
            throw new AssertionError("Wrong exception for " + message, other);
        }
    }

    private interface ThrowingRunnable { void run() throws Exception; }
}
