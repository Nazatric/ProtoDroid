package com.protodroid;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Host regression tests for the real staged package installer; no Android runtime is required. */
public final class DriverPackageTest {
    private static int checks;

    private interface ThrowingRunnable {
        void run() throws Exception;
    }

    private static void check(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }

    private static void expectFailure(ThrowingRunnable action, String message) throws Exception {
        ++checks;
        try {
            action.run();
            throw new AssertionError(message + " (unexpectedly accepted)");
        } catch (IOException expected) {
            // Expected validation failure.
        }
    }

    /** Minimal ELF64/AArch64 ET_DYN with a PT_LOAD and a dynamic dependency string table. */
    private static byte[] elf(int machine, String needed) {
        byte[] bytes = new byte[512];
        ByteBuffer out = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        out.putInt(0, 0x464c457f);
        out.put(4, (byte) 2);  // ELFCLASS64
        out.put(5, (byte) 1);  // ELFDATA2LSB
        out.putShort(16, (short) 3);  // ET_DYN
        out.putShort(18, (short) machine);
        out.putLong(32, 64);  // program-header offset
        out.putShort(54, (short) 56);
        out.putShort(56, (short) 2);

        out.putInt(64, 1);  // PT_LOAD
        out.putLong(64 + 8, 0);
        out.putLong(64 + 16, 0);
        out.putLong(64 + 32, bytes.length);
        out.putInt(120, 2);  // PT_DYNAMIC
        out.putLong(120 + 8, 256);
        out.putLong(120 + 32, 64);

        out.putLong(256, 1);  // DT_NEEDED
        out.putLong(256 + 8, 0);
        out.putLong(256 + 16, 5);  // DT_STRTAB
        out.putLong(256 + 24, 384);
        out.putLong(256 + 32, 10);  // DT_STRSZ
        out.putLong(256 + 40, 32);
        out.putLong(256 + 48, 0);  // DT_NULL
        byte[] dependency = needed.getBytes(StandardCharsets.UTF_8);
        check(dependency.length < 32, "test dependency is too long");
        System.arraycopy(dependency, 0, bytes, 384, dependency.length);
        return bytes;
    }

    private static byte[] zip(Map<String, byte[]> entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream out = new ZipOutputStream(bytes)) {
            for (Map.Entry<String, byte[]> entry : entries.entrySet()) {
                out.putNextEntry(new ZipEntry(entry.getKey()));
                out.write(entry.getValue());
                out.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    private static byte[] validZip(byte[] library, String checksum, int minApi) throws IOException {
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("libvulkan_freedreno.so", library);
        entries.put("meta.json", ("{\"libraryName\":\"libvulkan_freedreno.so\","
                + "\"name\":\"Test Turnip\",\"driverVersion\":\"1\",\"minApi\":" + minApi + "}")
                .getBytes(StandardCharsets.UTF_8));
        entries.put("SHA256SUMS.txt", (checksum + "  libvulkan_freedreno.so\n")
                .getBytes(StandardCharsets.UTF_8));
        return zip(entries);
    }

    private static String sha256(byte[] bytes) throws Exception {
        StringBuilder out = new StringBuilder(64);
        for (byte value : MessageDigest.getInstance("SHA-256").digest(bytes)) {
            out.append(String.format("%02x", value & 0xff));
        }
        return out.toString();
    }

    public static void main(String[] args) throws Exception {
        Path temporary = Files.createTempDirectory("protodroid-driver-test");
        try {
            Path rootPath = temporary.resolve("driver");
            java.io.File root = rootPath.toFile();
            byte[] good = elf(183, "libc.so");
            byte[] valid = validZip(good, sha256(good), 29);
            DriverPackage.Installed installed = DriverPackage.install(
                    new ByteArrayInputStream(valid), "turnip.zip", root, 29);
            check(installed.libraryFile().isFile(), "valid driver was not activated");
            DriverPackage.Installed read = DriverPackage.read(root);
            check(read != null && read.name.equals("Test Turnip") && read.version.equals("1"),
                    "installed metadata was not preserved");
            check(read != null && DriverCatalog.TURNIP.equals(read.backend),
                    "Turnip backend was not inferred from the package");

            byte[] originalMetadata = Files.readAllBytes(rootPath.resolve("driver.json"));
            byte[] originalLibrary = Files.readAllBytes(rootPath.resolve("libvulkan_freedreno.so"));
            ThrowingRunnable unchanged = () -> {
                DriverPackage.Installed current = DriverPackage.read(root);
                check(current != null && current.version.equals("1"), "failed import changed active metadata");
                check(Arrays.equals(originalMetadata, Files.readAllBytes(rootPath.resolve("driver.json"))),
                        "failed import rewrote active metadata");
                check(Arrays.equals(originalLibrary, Files.readAllBytes(rootPath.resolve("libvulkan_freedreno.so"))),
                        "failed import rewrote active library");
            };

            byte[] wrongChecksum = validZip(good, new String(new char[64]).replace('\0', '0'), 29);
            expectFailure(() -> DriverPackage.install(new ByteArrayInputStream(wrongChecksum), "bad.zip", root, 29),
                    "checksum mismatch");
            unchanged.run();

            Map<String, byte[]> traversalEntry = new LinkedHashMap<>();
            traversalEntry.put("../libvulkan_freedreno.so", good);
            final byte[] traversal = zip(traversalEntry);
            expectFailure(() -> DriverPackage.install(new ByteArrayInputStream(traversal), "bad.zip", root, 29),
                    "ZIP traversal");
            unchanged.run();

            final byte[] tooNew = validZip(good, sha256(good), 30);
            expectFailure(() -> DriverPackage.install(new ByteArrayInputStream(tooNew), "bad.zip", root, 29),
                    "unmet Android API floor");
            unchanged.run();

            final byte[] x86 = validZip(elf(62, "libc.so"), sha256(elf(62, "libc.so")), 29);
            expectFailure(() -> DriverPackage.install(new ByteArrayInputStream(x86), "bad.zip", root, 29),
                    "wrong ELF architecture");
            unchanged.run();

            final byte[] glibc = elf(183, "libc.so.6");
            expectFailure(() -> DriverPackage.install(new ByteArrayInputStream(glibc), "glibc.so", root, 29),
                    "glibc dependency");
            unchanged.run();

            final byte[] overflowing = elf(183, "libc.so");
            ByteBuffer.wrap(overflowing).order(ByteOrder.LITTLE_ENDIAN).putLong(32, Long.MAX_VALUE);
            expectFailure(() -> DriverPackage.install(new ByteArrayInputStream(overflowing), "overflow.so", root, 29),
                    "overflowing ELF program-header offset");
            unchanged.run();

            Map<String, byte[]> ambiguousEntries = new LinkedHashMap<>();
            ambiguousEntries.put("libvulkan_one.so", good);
            ambiguousEntries.put("libvulkan_two.so", good);
            final byte[] ambiguous = zip(ambiguousEntries);
            expectFailure(() -> DriverPackage.install(new ByteArrayInputStream(ambiguous), "bad.zip", root, 29),
                    "ambiguous driver libraries");
            unchanged.run();

            DriverPackage.Installed raw = DriverPackage.install(new ByteArrayInputStream(good),
                    "libvulkan_custom.so", root, 29);
            check(raw.libraryFile().isFile() && DriverPackage.read(root) != null,
                    "raw Android ARM64 library import failed");
            check(!temporary.resolve(".driver-install").toFile().exists()
                            && !temporary.resolve(".driver-previous").toFile().exists(),
                    "staging or backup directory remained after installation");

            // Multi-slot storage: installing or removing one package must not disturb another.
            java.io.File files = temporary.resolve("files").toFile();
            DriverRepository.Entry first = DriverRepository.install(files, "first",
                    new ByteArrayInputStream(valid), "first.zip", 29);
            DriverRepository.Entry second = DriverRepository.install(files, "second",
                    new ByteArrayInputStream(valid), "second.zip", 29);
            check(!first.id.equals(second.id) && DriverRepository.list(files).size() == 2,
                    "catalog packages did not get independent slots");
            expectFailure(() -> DriverRepository.install(files, "first",
                    new ByteArrayInputStream(wrongChecksum), "bad.zip", 29), "failed catalog replacement");
            check(DriverRepository.find(files, first.id) != null
                            && DriverRepository.find(files, second.id) != null,
                    "failed catalog replacement damaged another slot");
            check(DriverRepository.remove(files, first.id), "catalog package could not be removed");
            check(DriverRepository.find(files, first.id) == null && DriverRepository.find(files, second.id) != null,
                    "removing one package removed or changed another");

            DriverRepository.Entry imported = DriverRepository.install(files, "",
                    new ByteArrayInputStream(good), "libvulkan_custom.so", 29);
            int afterImport = DriverRepository.list(files).size();
            DriverRepository.Entry duplicate = DriverRepository.install(files, "",
                    new ByteArrayInputStream(good), "libvulkan_custom.so", 29);
            check(imported.id.equals(duplicate.id) && DriverRepository.list(files).size() == afterImport,
                    "identical raw imports were not deduplicated");

            Map<String, byte[]> panvkEntries = new LinkedHashMap<>();
            panvkEntries.put("libvulkan_panfrost.so", good);
            panvkEntries.put("meta.json", ("{\"libraryName\":\"libvulkan_panfrost.so\","
                    + "\"name\":\"Test PanVK\",\"driverVersion\":\"Mesa devel\","
                    + "\"packageVersion\":\"26.3.1\",\"backend\":\"panvk-kbase\",\"minApi\":29}")
                    .getBytes(StandardCharsets.UTF_8));
            panvkEntries.put("SHA256SUMS.txt", (sha256(good) + "  libvulkan_panfrost.so\n")
                    .getBytes(StandardCharsets.UTF_8));
            DriverRepository.Entry panvk = DriverRepository.install(files, "panvk",
                    new ByteArrayInputStream(zip(panvkEntries)), "panvk.zip", 29);
            check(DriverCatalog.PANVK.equals(panvk.installed.backend)
                            && panvk.installed.version.equals("26.3.1"),
                    "PanVK backend or package version was not preserved");

            DriverPackage.install(new ByteArrayInputStream(good), "libvulkan_custom.so",
                    DriverRepository.legacyRoot(files), 29);
            check(DriverRepository.find(files, DriverRepository.LEGACY_ID) != null,
                    "legacy single-slot package was not readable");
            check(DriverRepository.remove(files, DriverRepository.LEGACY_ID)
                            && DriverRepository.find(files, DriverRepository.LEGACY_ID) == null,
                    "legacy package could not be removed independently");
            System.out.println("driver-package tests passed (" + checks + " checks)");
        } finally {
            DriverPackage.deleteTree(temporary.toFile());
        }
    }
}
