package com.protodroid;

/** Android-independent presentation policy for the guided driver screen. */
final class DriverScreenPolicy {
    static boolean supportsBackend(boolean maliDevice, boolean adrenoDevice, String backend) {
        // Device nodes are only a family hint, not a promise that a package supports the exact GPU/kernel.
        if (maliDevice) return DriverCatalog.PANVK.equals(backend);
        if (adrenoDevice) return DriverCatalog.TURNIP.equals(backend);
        return true;
    }

    static boolean showAllByDefault(boolean maliDevice, boolean adrenoDevice) {
        return !maliDevice && !adrenoDevice;
    }

    static String deviceFamily(boolean maliDevice, boolean adrenoDevice) {
        if (maliDevice) return "Mali";
        if (adrenoDevice) return "Adreno";
        return "Unknown GPU family";
    }

    static String modeName(int mode) {
        switch (mode) {
            case 1: return "Phone driver";
            case 2: return "Bundled Turnip";
            case 3: return "Custom package";
            default: return "Automatic";
        }
    }

    private DriverScreenPolicy() {}
}
