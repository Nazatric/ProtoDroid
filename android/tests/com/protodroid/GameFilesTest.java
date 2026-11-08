package com.protodroid;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

public final class GameFilesTest {
    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("protodroid-game-files").toFile();
        try {
            File app = new File(root, "app");
            File shared = new File(root, "shared");
            check(!GameFiles.isComplete(null), "null directory was accepted");
            check(!GameFiles.isMissingUv(null), "null directory reported a missing UV file");
            check(GameFiles.firstComplete(app, shared) == null, "empty directories were accepted");
            check(!GameFiles.anyMissingUv(app, shared), "empty directories reported a missing UV file");

            check(app.mkdirs(), "could not create app directory");
            touch(app, GameFiles.DLL);
            check(!GameFiles.isComplete(app), "DLL-only directory was accepted");
            check(GameFiles.isMissingUv(app), "DLL-only directory did not identify the missing UV file");
            check(GameFiles.firstComplete(app, shared) == null, "DLL-only directory was selected");
            check(GameFiles.anyMissingUv(app, shared), "missing UV file was not reported");

            File uvDirectory = new File(app, GameFiles.UV);
            check(uvDirectory.mkdir(), "could not create directory with UV filename");
            check(GameFiles.isMissingUv(app), "directory with UV filename was accepted as a file");
            check(uvDirectory.delete(), "could not remove directory with UV filename");

            touch(shared, GameFiles.DLL);
            touch(shared, GameFiles.UV);
            check(GameFiles.firstComplete(app, shared).equals(shared),
                    "complete secondary directory did not win over incomplete primary directory");
            check(!GameFiles.anyMissingUv(shared), "complete installation reported a missing UV file");

            touch(app, GameFiles.UV);
            check(GameFiles.isComplete(app), "complete installation was rejected");
            check(GameFiles.firstComplete(app, shared).equals(app), "complete primary directory did not retain priority");
            check(!GameFiles.anyMissingUv(app, shared), "complete installations reported a missing UV file");

            check(new File(app, GameFiles.DLL).delete(), "could not remove DLL");
            check(!GameFiles.isComplete(app), "UV-only directory was accepted");
            check(!GameFiles.isMissingUv(app), "UV-only directory reported the wrong missing file");
        } finally {
            deleteTree(root);
        }
        System.out.println("Game-file requirement tests passed");
    }

    private static void touch(File dir, String name) throws IOException {
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("could not create " + dir);
        File file = new File(dir, name);
        if (!file.createNewFile()) throw new IOException("already exists: " + file);
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void deleteTree(File file) {
        if (file == null || !file.exists()) return;
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteTree(child);
        }
        if (!file.delete()) file.deleteOnExit();
    }
}
