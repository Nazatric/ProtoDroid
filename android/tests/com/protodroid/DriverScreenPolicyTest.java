package com.protodroid;

/** Host tests for family filtering and plain-language driver-mode labels. */
public final class DriverScreenPolicyTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        check(DriverScreenPolicy.supportsBackend(true, false, DriverCatalog.PANVK),
                "Mali's compatible view must retain PanVK-kbase");
        check(!DriverScreenPolicy.supportsBackend(true, false, DriverCatalog.TURNIP),
                "Mali's compatible view must not present Turnip as a Mali package");
        check(DriverScreenPolicy.supportsBackend(false, true, DriverCatalog.TURNIP),
                "Adreno's compatible view must retain Turnip");
        check(!DriverScreenPolicy.supportsBackend(false, true, DriverCatalog.PANVK),
                "Adreno's compatible view must not present PanVK-kbase as an Adreno package");
        check(DriverScreenPolicy.supportsBackend(false, false, DriverCatalog.TURNIP),
                "Unknown devices must not hide packages based on a guessed GPU family");
        check(DriverScreenPolicy.supportsBackend(false, false, DriverCatalog.PANVK),
                "Unknown devices must retain the explicit PanVK choice");
        check(!DriverScreenPolicy.showAllByDefault(true, false), "Mali should start with compatible packages");
        check(!DriverScreenPolicy.showAllByDefault(false, true), "Adreno should start with compatible packages");
        check(DriverScreenPolicy.showAllByDefault(false, false), "Unknown devices should start with all packages");
        check(DriverScreenPolicy.modeName(0).equals("Automatic"), "Auto label changed");
        check(DriverScreenPolicy.modeName(1).equals("Phone driver"), "System label changed");
        check(DriverScreenPolicy.modeName(2).equals("Bundled Turnip"), "Turnip label changed");
        check(DriverScreenPolicy.modeName(3).equals("Custom package"), "Custom label changed");
        check(DriverScreenPolicy.deviceFamily(true, false).equals("Mali"), "Mali family label changed");
        check(DriverScreenPolicy.deviceFamily(false, true).equals("Adreno"), "Adreno family label changed");
        System.out.println("Driver screen policy tests passed");
    }
}
