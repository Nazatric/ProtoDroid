package com.protodroid;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

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
    static final int CATEGORY_FIXES = 4;
    static final int CATEGORY_COUNT = 5;

    private static final String PRESET_KEY = "graphics_preset";
    private static final Object INI_LOCK = new Object();
    private static final String[] PRESET_KEYS = {
            "render_height", "texture_skip_mips", "max_anisotropy",
            "game_aa_level", "game_shadow_detail", "game_graphic_quality", "decode_16bit",
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

        int defaultIndex() {
            // Defaults live beside their setting, so adding an option cannot misalign a parallel defaults array.
            if (key.equals("render_height") || key.equals("texture_skip_mips") || key.equals("decode_16bit")
                    || key.equals("unlock_menu_fps") || key.equals("ui_scale") || key.equals("ui_vibrate")
                    || key.equals("fix_button_tap_tick_rate")) {
                return 1;
            }
            if (key.equals("ui_cam") || key.equals("ui_opacity") || key.equals("fov_offset")) return 2;
            return 0;
        }
    }

    static boolean isLiveEngineSetting(Setting setting) {
        return setting.key.equals("fps_limit") || setting.key.equals("battery_saver")
                || setting.key.equals("unlock_menu_fps");
    }

    static boolean isGameplaySetting(Setting setting) {
        if (isLiveEngineSetting(setting)) return true;
        return setting.key.startsWith("ui_");
    }

    /** Short, on-demand explanations; the settings page shows these only for the active row. */
    static String helpFor(Setting setting) {
        switch (setting.key) {
            case "render_height": return "Internal render height. Applies the next time you play.";
            case "texture_skip_mips": return "Full keeps every texture level. Lower choices save memory on the next Play.";
            case "decode_16bit": return "Decoded DXT1 color on GPUs without BC. 32-bit preserves more color; 16-bit saves memory and bandwidth. Applies on the next Play.";
            case "max_anisotropy": return "Texture sharpness at glancing angles. Applies on the next Play.";
            case "fps_limit": return "Caps the engine loop. Your display and FIFO still pace presented frames.";
            case "battery_saver": return "Limits the engine to 30 FPS when the selected cap is higher or unlimited.";
            case "unlock_menu_fps": return "Keeps silent menu movies responsive. Audio-backed movies retain their normal pacing.";
            case "vulkan_driver": return "Auto is recommended. Change drivers from the Drivers screen when recovery is needed.";
            case "game_aa_level": return "FXAA filters the final image with built-in MSAA disabled. 2x/4x keep the game's original MSAA. Applies on the next Play.";
            case "game_shadow_detail": return "The game's shadow quality. Game default preserves dynamic shadows and their original detail.";
            case "game_graphic_quality": return "The game's general graphics quality. Applies on the next Play.";
            case "ui_scale": return "Changes touch-button size immediately. Use Layout in the in-game gear to move controls.";
            case "ui_vibrate": return "Vibrates for touch-control feedback when the device supports it.";
            case "ui_pad_hide": return "Hides touch controls while a gamepad is connected.";
            case "ui_stick_run": return "Pushing the movement stick to its edge also sprints.";
            case "ui_cam": return "Adjusts swipe camera speed without changing the game configuration.";
            case "ui_invert": return "Inverts vertical camera swipes.";
            case "ui_power_buttons": return "Shows additional power and disguise buttons. The in-game 123 button opens number keys separately.";
            case "display_orientation": return "Auto follows either landscape direction. Fixed directions can recover a device that chooses the wrong one; applies on the next Play.";
            case "ui_opacity": return "Changes touch-control visibility without changing game brightness.";
            case "ui_fps": return "Shows FPS, frame times, and app memory over the game.";
            case "fov_offset": return "Offsets the camera field of view. Original uses the game's value.";
            case "shadow_map_resolution": return "Optional shadow-map size override. Larger maps can cost performance and memory.";
            case "remove_tick_rate_limit": return "Removes the identified simulation wait for high-refresh play. Leave Off for ordinary behavior.";
            case "fix_button_tap_tick_rate": return "Adjusts button-tap timing only when Remove tick limit is enabled.";
            case "prefer_loose_files": return "Prefers eligible unpacked game data while keeping saves and write paths unchanged.";
            case "unlock_dev_menu": return "Experimental developer-menu patch. Ambiguous engine data is refused and logged.";
            case "vram_report_mb": return "Changes the memory amount reported to the game; it does not add physical memory.";
            case "dump_textures": return "Writes original DDS textures and replacement keys for mod authors. Uses extra storage.";
            default: return "Changes save automatically. Engine options apply on the next Play.";
        }
    }

    static int categoryOf(Setting setting) {
        if (setting.key.equals("fov_offset") || setting.key.equals("shadow_map_resolution")
                || setting.key.equals("remove_tick_rate_limit") || setting.key.equals("fix_button_tap_tick_rate")
                || setting.key.equals("prefer_loose_files") || setting.key.equals("unlock_dev_menu")
                || setting.key.equals("vram_report_mb") || setting.key.equals("dump_textures")) {
            return CATEGORY_FIXES;
        }
        if (setting.key.startsWith("game_")) return CATEGORY_GAME_VIDEO;
        if (setting.key.equals("display_orientation") || setting.key.equals("ui_opacity")
                || setting.key.equals("ui_fps")) return CATEGORY_INTERFACE;
        if (setting.key.startsWith("ui_")) return CATEGORY_CONTROLS;
        return CATEGORY_GRAPHICS;
    }

    final Setting[] settings = {
            // Port graphics and performance.
            new Setting("Resolution", "render_height", new String[] {"540p", "720p", "900p", "1080p"},
                    new int[] {540, 720, 900, 1080}, true),
            new Setting("Textures", "texture_skip_mips", new String[] {"Full", "Half", "Quarter"},
                    new int[] {0, 1, 2}, true),
            new Setting("DXT1 color", "decode_16bit", new String[] {"32-bit", "16-bit"},
                    new int[] {0, 1}, true),
            new Setting("Anisotropy", "max_anisotropy", new String[] {"1x", "2x", "4x", "8x"},
                    new int[] {1, 2, 4, 8}, true),
            new Setting("FPS limit", "fps_limit",
                    new String[] {"Off", "30", "45", "60", "90", "120", "144", "240", "500", "1000"},
                    new int[] {0, 30, 45, 60, 90, 120, 144, 240, 500, 1000}, true),
            new Setting("Battery saver", "battery_saver", new String[] {"Off", "On"}, new int[] {0, 1}, true),
            new Setting("Unlock menu FPS", "unlock_menu_fps", new String[] {"Off", "On"}, new int[] {0, 1}, true),
            new Setting("GPU driver", "vulkan_driver", new String[] {"Auto", "System", "Turnip", "Custom"},
                    new int[] {0, 1, 2, 3}, true, new String[] {"auto", "system", "turnip", "custom"}),
            // The game's own GameDB video settings. -1 leaves its device-dependent default untouched.
            // Append-only: preference indices 0..3 and INI values -1/0/1/2 keep their original meaning.
            new Setting("Anti-Alias Level", "game_aa_level",
                    new String[] {"Game default", "Off", "2x MSAA", "4x MSAA", "FXAA"},
                    new int[] {-1, 0, 1, 2, AaPolicy.FXAA_VALUE}, true),
            new Setting("Shadow Detail", "game_shadow_detail", new String[] {"Game default", "Low", "Medium", "High"},
                    new int[] {-1, 0, 1, 2}, true),
            new Setting("Graphic Quality", "game_graphic_quality", new String[] {"Game default", "Low", "Medium", "High"},
                    new int[] {-1, 0, 1, 2}, true),
            // Touch and gamepad controls.
            new Setting("Screen orientation", "display_orientation", new String[] {"Auto", "Landscape", "Reverse"},
                    new int[] {0, 1, 2}, false),
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
            // Independent Android equivalents of Prototype Fix options. Every addition is opt-in apart from the
            // existing QTE timing policy, which has no effect unless tick-limit removal is explicitly enabled.
            new Setting("Field of view", "fov_offset",
                    new String[] {"-20°", "-10°", "Original", "+10°", "+20°"},
                    new int[] {-20, -10, 0, 10, 20}, true),
            new Setting("Shadow map", "shadow_map_resolution",
                    new String[] {"Game default", "512", "1024", "2048", "4096", "8192"},
                    new int[] {0, 512, 1024, 2048, 4096, 8192}, true),
            new Setting("Remove tick limit", "remove_tick_rate_limit", new String[] {"Off", "On"},
                    new int[] {0, 1}, true),
            new Setting("Button-tap timing", "fix_button_tap_tick_rate", new String[] {"Off", "On"},
                    new int[] {0, 1}, true),
            new Setting("Prefer loose files", "prefer_loose_files", new String[] {"Off", "On"},
                    new int[] {0, 1}, true),
            new Setting("Developer menu", "unlock_dev_menu", new String[] {"Off", "On"},
                    new int[] {0, 1}, true),
            new Setting("Reported VRAM", "vram_report_mb",
                    new String[] {"DXVK default", "512 MiB", "1 GiB", "2 GiB", "3 GiB"},
                    new int[] {0, 512, 1024, 2048, 3072}, true),
            new Setting("Dump original textures", "dump_textures", new String[] {"Off", "On"},
                    new int[] {0, 1}, true),
    };

    // Presets cover both port and game video options. FPS, battery and every control preference stay untouched.
    private static final int[][] PRESET_VALUES = {
            {0, 2, 0, 1, 1, 1, 1},  // Low: 540p, quarter, 1x; reduced DXT1 color, AA off, low shadows/quality
            {1, 1, 0, 4, 2, 2, 1},  // Medium: 720p, half, 1x; reduced DXT1 color, FXAA, medium shadows/quality
            {3, 0, 3, 4, 3, 3, 0},  // High: 1080p, full, 8x; full DXT1 color, FXAA, high shadows/quality
    };

    private final Context context;
    private final SharedPreferences prefs;
    final Setting[] gameplaySettings;
    private int graphicsPreset;

    GameSettings(Context context) {
        this.context = context.getApplicationContext();
        prefs = this.context.getSharedPreferences("controls", Context.MODE_PRIVATE);
        Setting[] live = new Setting[settings.length];
        int liveCount = 0;
        for (Setting setting : settings) if (isGameplaySetting(setting)) live[liveCount++] = setting;
        gameplaySettings = new Setting[liveCount];
        System.arraycopy(live, 0, gameplaySettings, 0, liveCount);
        for (int i = 0; i < settings.length; i++) {
            Setting setting = settings[i];
            setting.index = Math.max(0, Math.min(setting.values.length - 1,
                    prefs.getInt(setting.key, setting.defaultIndex())));
        }
        graphicsPreset = prefs.getInt(PRESET_KEY, detectPreset());
        if (graphicsPreset < PRESET_LOW || graphicsPreset > PRESET_CUSTOM) graphicsPreset = PRESET_CUSTOM;

        // Updating the preset definitions alone would leave existing users on the exact MSAA values that caused the
        // corruption. Migrate only those old preset-owned AA indices. setIndex() marks manual changes Custom, so a
        // custom configuration or a deliberate return to MSAA is never rewritten here.
        Setting aa = settings[settingIndex("game_aa_level")];
        int migratedAa = AaPolicy.migratePresetAaIndex(graphicsPreset, aa.index);
        if (migratedAa != aa.index) {
            aa.index = migratedAa;
            prefs.edit().putInt(aa.key, migratedAa).apply();
            writeEngineSettings(new String[] {aa.key});
        }
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
        // A preset owns only its seven graphics keys. In particular, do not rewrite an explicitly configured
        // fps_limit/battery_saver/unlock_menu_fps value from an unrelated preference default.
        writeEngineSettings(PRESET_KEYS);
    }

    void setIndex(int index, int next) {
        setIndex(settings[index], next);
    }

    void setIndex(Setting setting, int next) {
        setting.index = (next % setting.values.length + setting.values.length) % setting.values.length;
        SharedPreferences.Editor editor = prefs.edit().putInt(setting.key, setting.index);
        if (isPresetSetting(setting)) {
            graphicsPreset = PRESET_CUSTOM;
            editor.putInt(PRESET_KEY, PRESET_CUSTOM);
        }
        editor.apply();
        if (setting.engine) writeEngineSettings(new String[] {setting.key});
    }

    void setDriverMode(int mode) {
        if (mode < 0 || mode > 3) return;
        setIndex(settingIndex("vulkan_driver"), mode);
    }

    void setCustomDriverSelected(boolean custom) {
        setDriverMode(custom ? 3 : 0);
    }

    int driverMode() {
        return settings[settingIndex("vulkan_driver")].index;
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
        synchronized (INI_LOCK) {
            writeEngineSettingsLocked(keys);
        }
    }

    private void writeEngineSettingsLocked(String[] keys) {
        File dir = context.getExternalFilesDir(null);
        if (dir == null) {
            reportWriteFailure("protodroid.ini", new IOException("App-specific storage is unavailable"));
            return;
        }
        File ini = new File(dir, "protodroid.ini");
        File temporary = new File(dir, "protodroid.ini.tmp");
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
            // Write and sync beside the destination. A failed write must not truncate the last working INI.
            try (FileOutputStream stream = new FileOutputStream(temporary);
                 OutputStreamWriter writer = new OutputStreamWriter(stream, StandardCharsets.UTF_8)) {
                writer.write(out.toString());
                writer.flush();
                stream.getFD().sync();
            }
            try {
                Files.move(temporary.toPath(), ini.toPath(), StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException unsupported) {
                Files.move(temporary.toPath(), ini.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (Exception error) {
            try {
                Files.deleteIfExists(temporary.toPath());
            } catch (IOException ignored) {
                // The original INI is still intact; the visible error below is the important signal.
            }
            reportWriteFailure(ini.getAbsolutePath(), error);
        }
    }

    private void reportWriteFailure(String path, Exception error) {
        String detail = error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage();
        String message = "Engine settings were not written to " + path + ": " + detail;
        Log.e("ProtoDroid", message, error);
        // Driver operations can change settings off the UI thread; always marshal the visible warning to main.
        new Handler(Looper.getMainLooper()).post(() ->
                Toast.makeText(context, message, Toast.LENGTH_LONG).show());
    }
}
