package com.protodroid;

import java.io.File;

/** Required user-owned game files. This class deliberately has no Android dependencies so launcher policy is host-testable. */
final class GameFiles {
    static final String DLL = "prototypeenginef.dll";
    static final String UV = "proto_uv.dat";

    private GameFiles() {
    }

    static boolean isComplete(File dir) {
        return hasDll(dir) && isReadableFile(dir, UV);
    }

    static boolean isMissingUv(File dir) {
        return hasDll(dir) && !isReadableFile(dir, UV);
    }

    static File firstComplete(File... dirs) {
        if (dirs == null) return null;
        for (File dir : dirs) {
            if (isComplete(dir)) return dir;
        }
        return null;
    }

    static boolean anyMissingUv(File... dirs) {
        if (dirs == null) return false;
        for (File dir : dirs) {
            if (isMissingUv(dir)) return true;
        }
        return false;
    }

    private static boolean hasDll(File dir) {
        return isReadableFile(dir, DLL);
    }

    private static boolean isReadableFile(File dir, String name) {
        if (dir == null) return false;
        File file = new File(dir, name);
        return file.isFile() && file.canRead();
    }
}
