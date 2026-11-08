package com.protodroid;

public final class AaPolicyTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void migration(int preset, int before, int after, String message) {
        check(AaPolicy.migratePresetOwnedAaIndex(false, preset, before) == after, message);
    }

    public static void main(String[] args) {
        check(AaPolicy.FXAA_VALUE == 3, "FXAA must not reuse an engine MSAA value");
        check(AaPolicy.FXAA_INDEX == 4, "FXAA must follow all saved preference indices");
        check(AaPolicy.GAME_DEFAULT_INDEX == 0, "Game default preference index changed");

        migration(GamePreset.MEDIUM, 2, 0, "old Medium preset 2x MSAA did not return to Game default");
        migration(GamePreset.HIGH, 3, 0, "old High preset 4x MSAA did not return to Game default");
        migration(GamePreset.MEDIUM, 4, 0, "old Medium preset FXAA did not return to Game default");
        migration(GamePreset.HIGH, 4, 0, "old High preset FXAA did not return to Game default");
        migration(GamePreset.LOW, 1, 1, "Low's existing AA Off value changed");
        migration(GamePreset.CUSTOM, 0, 0, "Custom Game default was overwritten");
        migration(GamePreset.CUSTOM, 1, 1, "explicit Off was overwritten");
        migration(GamePreset.CUSTOM, 2, 2, "Custom 2x MSAA was overwritten");
        migration(GamePreset.CUSTOM, 3, 3, "Custom 4x MSAA was overwritten");
        migration(GamePreset.CUSTOM, 4, 4, "Custom FXAA was overwritten");
        migration(GamePreset.MEDIUM, 3, 3, "non-preset Medium value was overwritten");
        migration(GamePreset.HIGH, 2, 2, "non-preset High value was overwritten");
        migration(GamePreset.MEDIUM, 0, 0, "Medium Game default cleanup was not idempotent");
        migration(GamePreset.HIGH, 0, 0, "High Game default cleanup was not idempotent");
        migration(99, 2, 2, "unknown preset was accepted");

        // Once cleanup is complete, the same preset/value pair can be an explicit choice. Never migrate it again.
        for (int preset = 0; preset <= 3; preset++) {
            for (int aaIndex = 0; aaIndex <= 4; aaIndex++) {
                check(AaPolicy.migratePresetOwnedAaIndex(true, preset, aaIndex) == aaIndex,
                        "post-cleanup explicit AA choice was overwritten");
            }
        }

        System.out.println("AA preset cleanup and custom-choice preservation tests passed");
    }

    /** Avoid an Android dependency just for the four numeric preset constants. */
    private static final class GamePreset {
        static final int LOW = 0;
        static final int MEDIUM = 1;
        static final int HIGH = 2;
        static final int CUSTOM = 3;
    }
}
