package com.protodroid;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.Map;
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
            testLegacyWrappedLayouts(temporary.resolve("legacy-wrapped"));
            testScriptsAndDependencies(temporary.resolve("scripts"));
            testModConfig(temporary.resolve("mod-config"));
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
        File mismatch = createMod(root, "actual.id", "files = files\n", true);
        String mismatchMetadata = Files.readString(new File(mismatch, ModManifest.FILE_NAME).toPath());
        Files.writeString(new File(mismatch, ModManifest.FILE_NAME).toPath(),
                mismatchMetadata.replace("id = actual.id", "id = declared.id"));
        expectIOException(() -> ModManager.setEnabled(root, "actual.id", true),
                "deep enable retains folder/ID validation");
        check(!new File(mismatch, ModManifest.ENABLED_NAME).exists(), "mismatched package stays disabled");

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
        check(ModManifest.read(mod).error.isEmpty(), "library listing does not open every DDS payload");
        expectIOException(() -> ModManifest.parseValidated(mod, true), "deep validation still requires the DDS");
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

    private static void testLegacyWrappedLayouts(Path path) throws Exception {
        Files.createDirectories(path);
        File textureStaging = Files.createDirectory(path.resolve("textures")).toFile();
        File texturePack = Files.createDirectory(textureStaging.toPath().resolve("Pack")).toFile();
        String hash = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        Files.writeString(new File(texturePack, "textures.ini").toPath(), hash + " = replacement.dds\n");
        Files.write(new File(texturePack, "replacement.dds").toPath(), dds(true));
        ModManager.ensureGeneratedManifest(textureStaging, "wrapped-textures.zip");
        ModManifest textures = ModManifest.parseValidated(textureStaging, true);
        check(textures.textures.equals("textures.ini") && textures.files.isEmpty(),
                "wrapped legacy texture pack remains a texture mod");

        File filesStaging = Files.createDirectory(path.resolve("files")).toFile();
        File loose = Files.createDirectories(filesStaging.toPath().resolve("Pack/data")).toFile();
        Files.writeString(new File(loose, "data.bin").toPath(), "payload");
        ModManager.ensureGeneratedManifest(filesStaging, "wrapped-files.zip");
        ModManifest files = ModManifest.parseValidated(filesStaging, true);
        check(files.files.equals(".") && files.textures.isEmpty(),
                "wrapped loose package uses its normalized content root");

        File pluginStaging = Files.createDirectory(path.resolve("plugin")).toFile();
        File nativeDir = Files.createDirectories(pluginStaging.toPath().resolve("Pack/lib/arm64-v8a")).toFile();
        byte[] elf = new byte[64];
        elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F'; elf[4] = 2; elf[5] = 1;
        elf[16] = 3; elf[18] = (byte) 183;
        Files.write(new File(nativeDir, "libexample.so").toPath(), elf);
        ModManager.ensureGeneratedManifest(pluginStaging, "wrapped-plugin.zip");
        ModManifest plugin = ModManifest.parseValidated(pluginStaging, true);
        check(plugin.plugin.equals("lib/arm64-v8a/libexample.so") && plugin.files.isEmpty(),
                "wrapped ARM64 plugin retains its native layout");
    }

    private static void testScriptsAndDependencies(Path path) throws Exception {
        File root = Files.createDirectories(path).toFile();
        File library = createScriptMod(root, "example.library", "", "return { value = 42 }\n");
        ModManifest parsed = ModManifest.parseValidated(library, true);
        check(parsed.error.isEmpty() && parsed.api == 2 && parsed.scriptApi == 1 && parsed.hasExecutableContent(),
                "valid API-2 Lua package: " + parsed.error);
        check(parsed.contentLabel().contains("Lua script"), "library labels script content");

        File dependent = createScriptMod(root, "example.dependent", "example.library", "pd.log('dependent')\n");
        ModManager.setEnabled(root, "example.library", true);
        ModManager.setEnabled(root, "example.dependent", true);
        check(ModManager.activeCount(ModManager.list(root)) == 2, "enabled dependency graph is active");
        ModManager.setEnabled(root, "example.library", false);
        ModManifest blocked = findMod(ModManager.list(root), "example.dependent");
        check(blocked.error.contains("disabled"), "enabled dependent reports a disabled dependency");
        expectIOException(() -> ModManager.setEnabled(root, "example.dependent", true),
                "already-enabled marker is not rewritten across an invalid graph");
        ModManager.setEnabled(root, "example.library", true);
        check(ModManager.activeCount(ModManager.list(root)) == 2, "re-enabling dependency restores graph");

        File missing = createScriptMod(root, "example.missing", "not.installed", "pd.log('no')\n");
        check(findMod(ModManager.list(root), "example.missing").error.contains("Missing dependency"),
                "missing dependency is visible in library listing");
        expectIOException(() -> ModManager.setEnabled(root, "example.missing", true), "missing dependency cannot enable");

        createScriptMod(root, "example.cycle.a", "example.cycle.b", "pd.log('a')\n");
        createScriptMod(root, "example.cycle.b", "example.cycle.a", "pd.log('b')\n");
        check(findMod(ModManager.list(root), "example.cycle.a").error.contains("cycle")
                        || findMod(ModManager.list(root), "example.cycle.a").error.contains("unavailable"),
                "dependency cycle is rejected");

        File apiOne = createMod(root, "example.api1", "script = scripts/main.lua\n", false);
        writeScript(apiOne, "scripts/main.lua", "pd.log('bad api')\n");
        check(ModManifest.read(apiOne).error.contains("package API 2"), "API 1 cannot silently ignore scripts");

        File unicode = createScriptMod(root, "example.unicode", "", "pd.log('bad path')\n");
        File metadata = new File(unicode, ModManifest.FILE_NAME);
        Files.writeString(metadata.toPath(), Files.readString(metadata.toPath()).replace("scripts/main.lua", "scripts/máin.lua"));
        check(ModManifest.read(unicode).error.contains("Unsafe characters"), "Unicode script path is rejected consistently");

        File duplicate = createMod(root, "example.duplicate", "files = files\n", true);
        Files.writeString(new File(duplicate, ModManifest.FILE_NAME).toPath(),
                Files.readString(new File(duplicate, ModManifest.FILE_NAME).toPath())
                        .replace("api = 1", "api = 2\ndepends = example.library, example.library"));
        check(ModManifest.read(duplicate).error.contains("duplicate"), "duplicate dependencies are rejected");

        File declared = createMod(root, "example.declared", "files = files\nscript_api = 1\n", true);
        check(ModManifest.read(declared).error.contains("match a Lua entrypoint"),
                "script API cannot be declared without script content");

        File nul = createScriptMod(root, "example.nul", "", "pd.log('before')\n");
        byte[] nulSource = {'p', 'd', 0, 'x'};
        Files.write(new File(nul, "scripts/main.lua").toPath(), nulSource);
        check(ModManifest.read(nul).error.isEmpty(), "shallow listing does not open every script payload");
        expectIOException(() -> ModManifest.parseValidated(nul, true), "deep validation rejects NUL Lua source");

        File bytecode = createScriptMod(root, "example.bytecode", "", "pd.log('before')\n");
        Files.write(new File(bytecode, "scripts/main.lua").toPath(), new byte[] {0x1b, 'L', 'u', 'a'});
        expectIOException(() -> ModManifest.parseValidated(bytecode, true), "deep validation rejects Lua bytecode");

        File oversized = createScriptMod(root, "example.oversized", "", "pd.log('before')\n");
        Files.write(new File(oversized, "scripts/main.lua").toPath(), new byte[1024 * 1024 + 1]);
        expectIOException(() -> ModManifest.parseValidated(oversized, true), "deep validation enforces script size");

        ModManifest example = ModManifest.parseValidated(new File("modding/script-example/package"), true);
        check(example.api == 2 && example.scriptApi == 1 && example.options.size() == 3 && example.hasExecutableContent(),
                "shipped Lua example is a valid configurable API-2 package");
    }

    private static ModManifest findMod(java.util.List<ModManifest> mods, String id) {
        for (ModManifest mod : mods) if (mod.id.equals(id)) return mod;
        throw new AssertionError("Missing test mod " + id);
    }

    private static File createScriptMod(File root, String id, String dependencies, String source) throws IOException {
        File mod = createMod(root, id, "script = scripts/main.lua\nscript_api = 1\n", false);
        File metadata = new File(mod, ModManifest.FILE_NAME);
        String text = Files.readString(metadata.toPath()).replace("\napi = 1\n", "\napi = 2\n");
        if (!dependencies.isEmpty()) text = text.replace("name = Test mod", "name = Test mod\ndepends = " + dependencies);
        Files.writeString(metadata.toPath(), text);
        writeScript(mod, "scripts/main.lua", source);
        return mod;
    }

    private static void writeScript(File mod, String relative, String source) throws IOException {
        File file = new File(mod, relative);
        if (!file.getParentFile().mkdirs() && !file.getParentFile().isDirectory()) throw new IOException("script directory");
        Files.writeString(file.toPath(), source, StandardCharsets.UTF_8);
    }

    private static void testModConfig(Path path) throws Exception {
        File root = Files.createDirectories(path).toFile();
        String options = "files = files\n\n"
                + "[setting.enabled]\ntype = toggle\nlabel = Enabled\ndefault = 1\norder = 10\n"
                + "[setting.amount]\ntype = slider\nlabel = Amount\ndefault = 5\nmin = -20\nmax = 20\nstep = 0.5\n"
                + "[setting.mode]\ntype = choice\nlabel = Mode\noptions = Quiet|Every 300 frames\ndefault = Quiet\n"
                + "[setting.note]\ntype = text\nlabel = Note\ndefault = Hello\nmax_length = 80\n";
        File mod = createMod(root, "config.mod", options, true);
        ModManifest parsed = ModManifest.read(mod);
        check(parsed.error.isEmpty() && parsed.options.size() == 4 && parsed.pluginApi == 1,
                "four author options with legacy package/plugin defaults: " + parsed.error);
        check(parsed.options.get(0).id.equals("enabled"), "option order is respected");
        Files.writeString(new File(mod, ModConfig.FILE_NAME).toPath(), "future.value = keep\n");
        LinkedHashMap<String, String> changes = new LinkedHashMap<>();
        changes.put("enabled", "off");
        changes.put("amount", "7.5");
        changes.put("mode", "Every 300 frames");
        changes.put("note", "Hello = world");
        ModConfig.saveValues(mod, parsed.options, changes);
        Map<String, String> saved = ModConfig.loadValues(mod);
        check(saved.get("enabled").equals("0") && saved.get("amount").equals("7.5"),
                "toggle and stepped slider persist canonically");
        check(saved.get("mode").equals("Every 300 frames") && saved.get("note").equals("Hello = world"),
                "choice and text values persist");
        check(saved.get("future.value").equals("keep"), "unknown safe values survive edits");
        String before = Files.readString(new File(mod, ModConfig.FILE_NAME).toPath());
        changes.put("amount", "99");
        expectIOException(() -> ModConfig.saveValues(mod, parsed.options, changes), "out-of-range slider");
        check(Files.readString(new File(mod, ModConfig.FILE_NAME).toPath()).equals(before),
                "failed validation leaves previous settings untouched");
        changes.put("amount", "7.5");
        changes.put("note", "bad\nvalue");
        expectIOException(() -> ModConfig.saveValues(mod, parsed.options, changes), "multiline text rejection");

        File invalid = createMod(root, "invalid.choice", "files = files\n\n"
                + "[setting.mode]\ntype = choice\nlabel = Mode\noptions = Same|same\ndefault = Same\n", true);
        check(ModManifest.read(invalid).error.contains("duplicate choice"), "duplicate choices are visible");
        File invalidApi = createMod(root, "invalid.api", "files = files\nplugin_api = 2\n", true);
        check(ModManifest.read(invalidApi).error.contains("plugin API"), "plugin API requires native content");

        File example = new File("modding/example/package");
        ModManifest exampleManifest = ModManifest.parseValidated(example, false);
        check(exampleManifest.pluginApi == 3 && exampleManifest.api == 2 && exampleManifest.options.size() == 4,
                "shipped example opts into package API 2/native ABI v3 and demonstrates all option types");
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
