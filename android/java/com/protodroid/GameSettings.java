package com.protodroid;

import android.content.Context;
import android.content.SharedPreferences;

import java.io.File;
import java.io.FileWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Shared by the launcher and the in-game panel so both edit the same preferences and engine configuration. */
final class GameSettings {
    static final int PRESET_LOW = 0;
    static final int PRESET_MEDIUM = 1;
    static final int PRESET_HIGH = 2;
    static final int PRESET_CUSTOM = 3;

    static final int CATEGORY_GRAPHICS = 0;
    static final int CATEGORY_GAME_VIDEO = 1;
    static final int CATEGORY_CONTROLS = 2;
    static final int CATEGORY_INTERFACE = 3;
    static final int CATEGORY_COUNT = 4;

    private static final String PRESET_KEY = "graphics_preset";
    private static final String[] PRESET_KEYS = {
            "render_height", "texture_skip_mips", "max_anisotropy",
            "game_aa_level", "game_shadow_detail", "game_graphic_quality",
    };

    static final class Setting {
        final String label, key;
        final String[] names;
        final int[] values;
        final String[] engineValues;
        final boolean engine;
        int index;

        Setting(String label, String key, String[] names, int[] values, boolean engine) {
            this(label, key, names, values, engine, null);
        }

        Setting(String label, String key, String[] names, int[] values, boolean engine, String[] engineValues) {
            this.label = label;
            this.key = key;
            this.names = names;
            this.values = values;
            this.engine = engine;
            this.engineValues = engineValues;
        }

        int value() {
            return values[index];
        }

        String engineValue() {
            return engineValues == null ? Integer.toString(value()) : engineValues[index];
        }
    }

    static boolean isLiveEngineSetting(Setting setting) {
        return setting.key.equals("fps_limit");
    }

    static int categoryOf(Setting setting) {
        if (setting.key.startsWith("game_")) return CATEGORY_GAME_VIDEO;
        if (setting.key.equals("ui_opacity") || setting.key.equals("ui_fps")) return CATEGORY_INTERFACE;
        if (setting.key.startsWith("ui_")) return CATEGORY_CONTROLS;
        return CATEGORY_GRAPHICS;
    }

    final Setting[] settings = {
            // Port graphics and performance.
            new Setting("Resolution", "render_height", new String[] {"540p", "720p", "900p", "1080p"},
                    new int[] {540, 720, 900, 1080}, true),
            new Setting("Textures", "texture_skip_mips", new String[] {"Full", "Half", "Quarter"},
                    new int[] {0, 1, 2}, true),
            new Setting("Anisotropy", "max_anisotropy", new String[] {"1x", "2x", "4x", "8x"},
                    new int[] {1, 2, 4, 8}, true),
            new Setting("FPS limit", "fps_limit", new String[] {"Off", "30", "45", "60"},
                    new int[] {0, 30, 45, 60}, true),
            new Setting("Battery saver", "battery_saver", new String[] {"Off", "On"}, new int[] {0, 1}, true),
            new Setting("Unlock menu FPS", "unlock_menu_fps", new String[] {"Off", "On"}, new int[] {0, 1}, true),
            new Setting("GPU driver", "vulkan_driver", new String[] {"Auto", "System", "Turnip", "Custom"},
                    new int[] {0, 1, 2, 3}, true, new String[] {"auto", "system", "turnip", "custom"}),
            // The game's own GameDB video settings. -1 leaves its device-dependent default untouched.
            new Setting("Anti-Alias Level", "game_aa_level", new String[] {"Game default", "Off", "2x", "4x"},
                    new int[] {-1, 0, 1, 2}, true),
            new Setting("Shadow Detail", "game_shadow_detail", new String[] {"Game default", "Low", "Medium", "High"},
                    new int[] {-1, 0, 1, 2}, true),
            new Setting("Graphic Quality", "game_graphic_quality", new String[] {"Game default", "Low", "Medium", "High"},
                    new int[] {-1, 0, 1, 2}, true),
            // Touch and gamepad controls.
            new Setting("Button size", "ui_scale", new String[] {"80%", "100%", "120%", "140%"},
                    new int[] {80, 100, 120, 140}, false),
            new Setting("Vibration", "ui_vibrate", new String[] {"Off", "On"}, new int[] {0, 1}, false),
            new Setting("Hide with gamepad", "ui_pad_hide", new String[] {"Off", "On"}, new int[] {0, 1}, false),
            new Setting("Stick edge sprint", "ui_stick_run", new String[] {"Off", "On"}, new int[] {0, 1}, false),
            new Setting("Camera speed", "ui_cam", new String[] {"50%", "75%", "100%", "150%", "200%"},
                    new int[] {50, 75, 100, 150, 200}, false),
            new Setting("Invert Y", "ui_invert", new String[] {"Off", "On"}, new int[] {0, 1}, false),
            new Setting("Extra buttons", "ui_power_buttons", new String[] {"Off", "On"}, new int[] {0, 1}, false),
            // Interface visibility.
            new Setting("Opacity", "ui_opacity", new String[] {"40%", "60%", "80%", "100%"},
                    new int[] {40, 60, 80, 100}, false),
            new Setting("Show FPS", "ui_fps", new String[] {"Off", "On"}, new int[] {0, 1}, false),
    };

    private static final int[] DEFAULTS = {1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 2, 0, 0, 2, 0};

    // Presets cover both port and game video options. FPS, battery and every control preference stay untouched.
    private static final int[][] PRESET_VALUES = {
            {0, 2, 0, 1, 1, 1},  // Low: 540p, quarter, 1x; AA off, low shadows and game quality
            {1, 1, 0, 2, 2, 2},  // Medium: 720p, half, 1x; 2x AA, medium shadows and game quality
            {3, 0, 3, 3, 3, 3},  // High: 1080p, full, 8x; 4x AA, high shadows and game quality
    };

    private final Context context;
    private final SharedPreferences prefs;
    private int graphicsPreset;

    GameSettings(Context context) {
        this.context = context.getApplicationContext();
        prefs = this.context.getSharedPreferences("controls", Context.MODE_PRIVATE);
        for (int i = 0; i < settings.length; i++) {
            Setting setting = settings[i];
            setting.index = Math.max(0, Math.min(setting.values.length - 1, prefs.getInt(setting.key, DEFAULTS[i])));
        }
        graphicsPreset = prefs.getInt(PRESET_KEY, detectPreset());
        if (graphicsPreset < PRESET_LOW || graphicsPreset > PRESET_CUSTOM) graphicsPreset = PRESET_CUSTOM;
    }

    int graphicsPreset() {
        return graphicsPreset;
    }

    private int settingIndex(String key) {
        for (int i = 0; i < settings.length; i++) if (settings[i].key.equals(key)) return i;
        throw new IllegalArgumentException("Unknown setting " + key);
    }

    int valueOf(String key) {
        return settings[settingIndex(key)].value();
    }

    private static boolean isPresetSetting(Setting setting) {
        for (String key : PRESET_KEYS) if (setting.key.equals(key)) return true;
        return false;
    }

    private int detectPreset() {
        for (int preset = PRESET_LOW; preset <= PRESET_HIGH; preset++) {
            boolean matches = true;
            for (int i = 0; i < PRESET_KEYS.length; i++) {
                matches &= settings[settingIndex(PRESET_KEYS[i])].index == PRESET_VALUES[preset][i];
            }
            if (matches) return preset;
        }
        return PRESET_CUSTOM;
    }

    void selectCustom() {
        graphicsPreset = PRESET_CUSTOM;
        prefs.edit().putInt(PRESET_KEY, PRESET_CUSTOM).apply();
    }

    void applyPreset(int preset) {
        if (preset < PRESET_LOW || preset > PRESET_HIGH) return;
        SharedPreferences.Editor editor = prefs.edit();
        for (int i = 0; i < PRESET_KEYS.length; i++) {
            Setting setting = settings[settingIndex(PRESET_KEYS[i])];
            setting.index = PRESET_VALUES[preset][i];
            editor.putInt(setting.key, setting.index);
        }
        graphicsPreset = preset;
        editor.putInt(PRESET_KEY, preset).apply();
        // A preset owns only its six graphics keys. In particular, do not rewrite an explicitly configured
        // fps_limit/battery_saver/unlock_menu_fps value from an unrelated preference default.
        writeEngineSettings(PRESET_KEYS);
    }

    void setIndex(int index, int next) {
        Setting setting = settings[index];
        setting.index = (next % setting.values.length + setting.values.length) % setting.values.length;
        SharedPreferences.Editor editor = prefs.edit().putInt(setting.key, setting.index);
        if (isPresetSetting(setting)) {
            graphicsPreset = PRESET_CUSTOM;
            editor.putInt(PRESET_KEY, PRESET_CUSTOM);
        }
        editor.apply();
        if (setting.engine) writeEngineSettings(new String[] {setting.key});
    }

    void setCustomDriverSelected(boolean custom) {
        setIndex(settingIndex("vulkan_driver"), custom ? 3 : 0);
    }

    /** Rewrites only our engine keys, preserving comments and every unrelated setting in protodroid.ini. */
    void writeEngineSettings() {
        writeEngineSettings(null);
    }

    private static boolean containsKey(String[] keys, String key) {
        if (keys == null) return true;
        for (String candidate : keys) if (candidate.equals(key)) return true;
        return false;
    }

    private void writeEngineSettings(String[] keys) {
        File dir = context.getExternalFilesDir(null);
        if (dir == null) return;
        File ini = new File(dir, "protodroid.ini");
        StringBuilder out = new StringBuilder();
        try {
            if (ini.exists()) {
                for (String line : new String(Files.readAllBytes(ini.toPath()), StandardCharsets.UTF_8).split("\n")) {
                    String key = line.split("=", 2)[0].trim();
                    boolean replace = false;
                    for (Setting setting : settings) {
                        replace |= setting.engine && containsKey(keys, setting.key) && setting.key.equals(key);
                    }
                    if (!replace && !line.trim().isEmpty()) out.append(line).append('\n');
                }
            }
            for (Setting setting : settings) {
                if (setting.engine && containsKey(keys, setting.key)) {
                    out.append(setting.key).append(" = ").append(setting.engineValue()).append('\n');
                }
            }
            try (FileWriter writer = new FileWriter(ini)) {
                writer.write(out.toString());
            }
        } catch (Exception ignored) {
            // Preferences remain saved; the engine keeps its previous file if storage is unavailable.
        }
    }
}
