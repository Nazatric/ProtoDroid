package com.protodroid;

/** Pure policy for the one-time cleanup of AA values written by older presets. */
final class AaPolicy {
    static final int FXAA_VALUE = 3;
    static final int FXAA_INDEX = 4;
    static final int GAME_DEFAULT_INDEX = 0;

    private static final int MEDIUM_PRESET = 1;
    private static final int HIGH_PRESET = 2;
    private static final int OLD_MEDIUM_AA_INDEX = 2; // 2x MSAA
    private static final int OLD_HIGH_AA_INDEX = 3;   // 4x MSAA

    private AaPolicy() {}

    static int migratePresetOwnedAaIndex(boolean cleanupDone, int preset, int currentIndex) {
        // Low's old Off value is already safe. Medium/High return only their exact old preset-owned MSAA/FXAA
        // values to Game default. After the one-time cleanup, every value may be an explicit independent choice.
        if (cleanupDone) return currentIndex;
        if (preset == MEDIUM_PRESET
                && (currentIndex == OLD_MEDIUM_AA_INDEX || currentIndex == FXAA_INDEX)) {
            return GAME_DEFAULT_INDEX;
        }
        if (preset == HIGH_PRESET
                && (currentIndex == OLD_HIGH_AA_INDEX || currentIndex == FXAA_INDEX)) {
            return GAME_DEFAULT_INDEX;
        }
        return currentIndex;
    }
}
