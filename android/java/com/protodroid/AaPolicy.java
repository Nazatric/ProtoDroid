package com.protodroid;

/** Pure policy for the one-time 0.15 preset migration; kept independent of Android for host tests. */
final class AaPolicy {
    static final int FXAA_VALUE = 3;
    static final int FXAA_INDEX = 4;

    private static final int MEDIUM_PRESET = 1;
    private static final int HIGH_PRESET = 2;
    private static final int OLD_MEDIUM_AA_INDEX = 2; // 2x MSAA
    private static final int OLD_HIGH_AA_INDEX = 3;   // 4x MSAA

    private AaPolicy() {}

    static int migratePresetAaIndex(int preset, int currentIndex) {
        // Only exact values written by the old presets are migrated. Custom and explicit MSAA choices remain intact.
        if (preset == MEDIUM_PRESET && currentIndex == OLD_MEDIUM_AA_INDEX) return FXAA_INDEX;
        if (preset == HIGH_PRESET && currentIndex == OLD_HIGH_AA_INDEX) return FXAA_INDEX;
        return currentIndex;
    }
}
