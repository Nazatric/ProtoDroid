package com.protodroid;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.view.Display;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/** SDL's activity running the native port (libmain.so: the recompiled engine and its platform layer). */
public class ProtoActivity extends SDLActivity {
    private ProtoSurface gameSurface;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "FAudio", "dxvk_d3d9", modernCpu() ? "main" : "main_v80"};
    }

    /**
     * libmain.so is built for ARMv8.2 with RCpc (LDAPR keeps the emulated x86 memory ordering cheap) and LSE atomics;
     * older cores (Cortex-A53/A73, e.g. Snapdragon 6xx) get libmain_v80.so, built for ARMv8.0. Every core must have
     * both features: big.LITTLE phones list each core's features separately.
     */
    private static boolean modernCpu() {
        int cores = 0;
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/cpuinfo"))) {
            for (String line; (line = r.readLine()) != null;) {
                if (!line.startsWith("Features")) continue;
                List<String> features = Arrays.asList(line.substring(line.indexOf(':') + 1).trim().split("\\s+"));
                if (!features.contains("atomics") || !features.contains("lrcpc")) return false;
                ++cores;
            }
        } catch (IOException e) {
            return false;
        }
        return cores > 0;
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        gameSurface = new ProtoSurface(context);
        return gameSurface;
    }

    /**
     * Pin the display to its highest refresh rate at the current resolution. Without this, the power
     * manager down-switches the panel (120 -> 60 Hz and back) whenever the game's frame load changes,
     * and each switch tears down the BufferQueue: the Vulkan swapchain has to be recreated while the
     * old buffers drain, which stalled the whole game for ~20 seconds in the field. A pinned mode
     * means no switches, so no teardowns.
     */
    private void pinDisplayMode() {
        try {
            Display display = getWindowManager().getDefaultDisplay();
            Display.Mode current = display.getMode();
            Display.Mode best = current;
            for (Display.Mode m : display.getSupportedModes()) {
                if (m.getPhysicalWidth() == current.getPhysicalWidth()
                        && m.getPhysicalHeight() == current.getPhysicalHeight()
                        && m.getRefreshRate() > best.getRefreshRate()) {
                    best = m;
                }
            }
            if (best.getModeId() != current.getModeId()) {
                WindowManager.LayoutParams p = getWindow().getAttributes();
                p.preferredDisplayModeId = best.getModeId();
                getWindow().setAttributes(p);
            }
        } catch (Exception ignored) {
            // Without the pin the system picks rates as before; the game still runs.
        }
    }

    /**
     * Install the bundled minimap mod on first run so the in-game MODS panel has something to edit
     * from day one. Present = the user's copy wins (they may have edited or disabled it); the game
     * runs fine either way, so any failure here is silently non-fatal.
     */
    private void ensureBundledMods() {
        File mods = ModManager.modsDir(this);
        File target = new File(mods, "protodroid-minimap");
        if (new File(target, "protodroid-mod.ini").isFile()) return;
        try (InputStream in = getAssets().open("mods/protodroid-minimap.zip")) {
            ZipInputStream zip = new ZipInputStream(in);
            byte[] buffer = new byte[16384];
            for (ZipEntry e; (e = zip.getNextEntry()) != null; ) {
                File out = new File(mods, e.getName());
                if (!out.getCanonicalPath().startsWith(mods.getCanonicalPath() + File.separator)) continue;
                if (e.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                out.getParentFile().mkdirs();
                try (FileOutputStream f = new FileOutputStream(out)) {
                    for (int n; (n = zip.read(buffer)) > 0; ) f.write(buffer, 0, n);
                }
                zip.closeEntry();
            }
            new File(target, "enabled").createNewFile();
        } catch (IOException ignored) {
            // The mod stays optional; the game runs without it.
        }
    }

    /**
     * The engine runs once per process: a new start of the game gets a fresh process. SDL's onDestroy waits on the UI
     * thread for the game thread to return, which the engine's shutdown can make long enough for an ANR, so a
     * finishing activity ends the process before that.
     */
    @Override
    protected void onDestroy() {
        if (isFinishing()) android.os.Process.killProcess(android.os.Process.myPid());
        super.onDestroy();
    }

    private void prepareVulkanDriver() {
        DriverRepository.Entry selected = DriverManager.selected(this);
        DriverPackage.Installed installed = selected == null ? null : selected.installed;
        File driver = installed == null ? null : installed.libraryFile();
        if (driver == null && getSharedPreferences("controls", MODE_PRIVATE).getInt("vulkan_driver", 0) == 3) {
            // The in-game restart action comes straight back here, without SetupActivity's migration/recovery path.
            new GameSettings(this).setCustomDriverSelected(false);
            Toast.makeText(this, "No valid custom driver is selected; Auto was restored.", Toast.LENGTH_LONG).show();
        }
        try {
            if (driver == null) {
                Os.unsetenv("PROTODROID_CUSTOM_DRIVER");
                Os.unsetenv("PROTODROID_CUSTOM_DRIVER_BACKEND");
            } else {
                Os.setenv("PROTODROID_CUSTOM_DRIVER", driver.getAbsolutePath(), true);
                Os.setenv("PROTODROID_CUSTOM_DRIVER_BACKEND", installed.backend, true);
            }
        } catch (ErrnoException ignored) {
            // Native selection keeps its safe automatic/system fallback.
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        int orientation = getSharedPreferences("controls", MODE_PRIVATE).getInt("display_orientation", 0);
        if (orientation == 1) {
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        } else if (orientation == 2) {
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE);
        }
        prepareVulkanDriver();
        ModManager.prepareForLaunch(this);
        ensureBundledMods();
        // The native side loads the bundled Vulkan driver (Turnip, on Adreno) from the extracted libraries.
        try {
            Os.setenv("PROTODROID_NATIVE_LIB_DIR", getApplicationInfo().nativeLibraryDir, true);
        } catch (ErrnoException ignored) {
            // the system Vulkan driver is used
        }
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);  // no dimming while playing
        pinDisplayMode();
        if (isDiagnosticTour() && "precompile".equals(Os.getenv("PROTODROID_TOUR_PURPOSE"))) {
            // One-click shader precompile: the tour injects its own input, so the on-screen controls are
            // replaced by the styled loading screen. The relaunch on completion is owned by SetupActivity
            // in the launcher process; this tour process ends itself the moment the run finishes.
            addContentView(new ShaderPrecompileOverlay(this), new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        } else {
            // On-screen controls above SDL's surface; they feed SDL keyboard and mouse input.
            addContentView(new TouchControls(this), new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            // In-game mod settings: the left-edge handle opens the live panel (only when mods
            // with options are enabled). Passes touches through everywhere outside its controls.
            addContentView(new ModPanel(this), new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
    }

    private static boolean isDiagnosticTour() {
        return "1".equals(Os.getenv("PROTODROID_DIAGNOSTIC_TOUR"));
    }
}
