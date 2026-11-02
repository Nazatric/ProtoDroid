package com.protodroid;

/** Android-free presentation policy for the optional Smart touch mode. */
final class SmartControlsModel {
    static final int CLASSIC = 0;
    static final int SMART = 1;

    static final int CONTEXT_UNKNOWN = 0;
    static final int CONTEXT_SPECIAL = 1;
    static final int CONTEXT_CONSUME = 2;

    private SmartControlsModel() {}

    static boolean isSmart(int mode) {
        return mode == SMART;
    }

    /** Unknown and every unrecognized native value deliberately retain the original Special action label. */
    static String specialLabel(int nativeContext) {
        return nativeContext == CONTEXT_CONSUME ? "CONSUME" : "SPECIAL";
    }
}
