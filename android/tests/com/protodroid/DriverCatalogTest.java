package com.protodroid;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;

/** Network/package validation for CI. It is opt-in locally so the offline installer suite stays hermetic. */
public final class DriverCatalogTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void checkDirectIcdExport(File library) throws Exception {
        Process process = new ProcessBuilder("readelf", "--dyn-syms", "--wide", library.getAbsolutePath())
                .redirectErrorStream(true).start();
        String output;
        try (InputStream input = process.getInputStream()) {
            output = new String(input.readAllBytes(), StandardCharsets.UTF_8);
        }
        check(process.waitFor() == 0, "readelf could not inspect " + library + "\n" + output);
        check(output.contains("vk_icdGetInstanceProcAddr"),
                "PanVK package does not export the direct ICD entry point\n" + output);
    }

    public static void main(String[] args) throws Exception {
        Path temporary = Files.createTempDirectory("protodroid-catalog-test");
        try {
            File files = temporary.resolve("files").toFile();
            for (DriverCatalog.Entry entry : DriverCatalog.ENTRIES) {
                File partial = temporary.resolve(entry.id + ".zip").toFile();
                new DriverCatalog.Download().run(entry, partial, (done, total) -> { });
                check(partial.length() == entry.size, entry.id + " size differs from the pinned release asset");
                DriverRepository.Entry installed;
                try (InputStream input = new FileInputStream(partial)) {
                    installed = DriverRepository.install(files, entry.id, input, entry.title + ".zip", entry.minApi);
                }
                check(entry.backend.equals(installed.installed.backend), entry.id + " backend changed during install");
                check(installed.installed.libraryFile().isFile(), entry.id + " has no installed library");
                if (DriverCatalog.PANVK.equals(entry.backend)) checkDirectIcdExport(installed.installed.libraryFile());
                System.out.println("validated " + entry.id + " -> " + installed.installed.libraryFile().getName());
            }
            System.out.println("online driver catalog tests passed (" + DriverCatalog.ENTRIES.length + " packages)");
        } finally {
            DriverPackage.deleteTree(temporary.toFile());
        }
    }
}
