package com.protodroid;

public final class AaPolicyTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void migration(int preset, int before, int after, String message) {
        check(AaPolicy.migratePresetAaIndex(preset, before) == after, message);
    }

    public static void main(String[] args) {
        check(AaPolicy.FXAA_VALUE == 3, "FXAA must not reuse an engine MSAA value");
        check(AaPolicy.FXAA_INDEX == 4, "FXAA must follow all saved preference indices");

        migration(GamePreset.MEDIUM, 2, 4, "old Medium 2x MSAA was not migrated to FXAA");
        migration(GamePreset.HIGH, 3, 4, "old High 4x MSAA was not migrated to FXAA");
        migration(GamePreset.LOW, 1, 1, "Low AA Off changed");
        migration(GamePreset.CUSTOM, 2, 2, "Custom 2x MSAA was overwritten");
        migration(GamePreset.CUSTOM, 3, 3, "Custom 4x MSAA was overwritten");
        migration(GamePreset.MEDIUM, 3, 3, "non-preset Medium value was overwritten");
        migration(GamePreset.HIGH, 2, 2, "non-preset High value was overwritten");
        migration(GamePreset.MEDIUM, 4, 4, "new FXAA value was not idempotent");
        migration(GamePreset.HIGH, 4, 4, "new FXAA value was not idempotent");
        migration(GamePreset.CUSTOM, 0, 0, "Game default was overwritten");
        migration(GamePreset.CUSTOM, 1, 1, "explicit Off was overwritten");
        migration(99, 2, 2, "unknown preset was accepted");

        System.out.println("AA preset migration and custom-choice preservation tests passed");
    }

    /** Avoid an Android dependency just for the four numeric preset constants. */
    private static final class GamePreset {
        static final int LOW = 0;
        static final int MEDIUM = 1;
        static final int HIGH = 2;
        static final int CUSTOM = 3;
    }
}
