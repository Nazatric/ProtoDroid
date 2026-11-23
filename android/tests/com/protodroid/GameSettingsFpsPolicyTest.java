package com.protodroid;

public final class GameSettingsFpsPolicyTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        GameSettings.Setting fps = new GameSettings.Setting("FPS limit", "fps_limit",
                new String[] {"Off", "30", "45", "60"},
                new int[] {0, GameSettings.FPS_LIMIT_DEFAULT, 45, 60}, true);
        check(fps.values[fps.defaultIndex()] == 30, "a new settings profile must default to 30 FPS");

        check(GameSettings.effectiveFpsLimit(0, false) == 0,
                "an explicit Off selection must remain uncapped");
        check(GameSettings.effectiveFpsLimit(60, false) == 60,
                "an explicitly selected higher cap must be preserved");
        check(GameSettings.effectiveFpsLimit(1000, false) == 1000,
                "the highest selectable cap must be preserved");
        check(GameSettings.effectiveFpsLimit(0, true) == 30,
                "Battery saver must cap Off at 30 FPS");
        check(GameSettings.effectiveFpsLimit(60, true) == 30,
                "Battery saver must cap a higher selection at 30 FPS");
        check(GameSettings.effectiveFpsLimit(30, true) == 30,
                "Battery saver must retain an existing 30 FPS cap");

        System.out.println("FPS defaults, explicit overrides, and Battery saver policy tests passed");
    }
}
