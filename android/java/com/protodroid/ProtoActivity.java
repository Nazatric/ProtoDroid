package com.protodroid;

import android.content.Context;
import android.content.pm.ActivityInfo;
import android.app.GameManager;
import android.app.GameState;
import android.os.Build;
import android.os.Bundle;
import android.view.Window;
import android.system.ErrnoException;
import android.system.Os;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.Arrays;
import java.util.List;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/** SDL's activity running the native port (libmain.so: the recompiled engine and its platform layer). */
public class ProtoActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "FAudio", "dxvk_d3d9", modernCpu() ? "main" : "main_v80"};
    }

    /**
     * libmain.so is built for ARMv8.2 with RCpc (LDAPR keeps the emulated x86 memory ordering cheap) and LSE atomics;
     * older cores (Cortex-A53/A73, e.g. Snapdragon 6xx) get libmain_v80.so, built for ARMv8.0. Every core must have
     * both features: big.LITTLE phones list each core's features separately.
     *
     * "ilrcpc" (RCpc2, ARMv8.3+) satisfies the RCpc requirement and uses the same load-acquire instructions, but the
     * first version of this test only accepted the literal "lrcpc". A core whose line says "ilrcpc" therefore sent the
     * whole phone to the baseline ARMv8.0 build, which has neither LDAPR nor LSE atomics - the exact path every guest
     * lock, reference count and emulated x86 acquire load takes. Also accept "lrrcpc"/"ilrcpc2", the feature names
     * used for the same capability on newer kernels.
     */
    private static boolean modernCpu() {
        int cores = 0;
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/cpuinfo"))) {
            for (String line; (line = r.readLine()) != null;) {
                if (!line.startsWith("Features")) continue;
                List<String> features = Arrays.asList(line.substring(line.indexOf(':') + 1).trim().split("\\s+"));
                boolean atomics = features.contains("atomics");
                boolean rcpc = features.contains("lrcpc") || features.contains("ilrcpc") || features.contains("lrrcpc")
                        || features.contains("ilrcpc2");
                if (!atomics || !rcpc) return false;
                ++cores;
            }
        } catch (IOException e) {
            return false;
        }
        return cores > 0;
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new ProtoSurface(context);
    }

    /**
     * The engine runs once per process: a new start of the game gets a fresh process. SDL's onDestroy waits on the UI
     * thread for the game thread to return, which the engine's shutdown can make long enough for an ANR, so a
     * finishing activity ends the process before that.
     */
    @Override
    protected void onPause() {
        // Not loading, and no longer in gameplay: the platform may interrupt us.
        updateGameState(false, GameState.MODE_NONE);
        super.onPause();
    }

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

    /**
     * Item 48: the app declares itself a game but never used the platform's game APIs. Telling the platform that the
     * game is loading lets it put the power HAL into its game-loading mode (a CPU/IO boost) and telling it that the game
     * is in uninterruptible gameplay keeps the per-game performance profile applied; sustained performance mode stops
     * the system from quietly dropping the clocks mid-session.
     *
     * `GameManager.setGameState` landed in API 33 and takes a `GameState` object whose mode is one of GameState's own
     * `MODE_*` constants - the first version of this method called it with an int on API 31, and the second used
     * `GAME_STATE_*` names that exist only in Google's codelab enum, not in the platform class. Both failed javac.
     * Everything here is best-effort: a device or OEM build without a usable game service must not change behaviour.
     */
    private void updateGameState(boolean loading, int state) {
        if (Build.VERSION.SDK_INT < 33) return;
        try {
            GameManager manager = getSystemService(GameManager.class);
            if (manager != null) manager.setGameState(new GameState(loading, state));
        } catch (RuntimeException ignored) {
            // OEM builds without a usable game service, or a GameState value it rejects
        }
    }

    private void applySustainedPerformance() {
        try {
            Window window = getWindow();
            if (window != null) window.setSustainedPerformanceMode(true);
        } catch (RuntimeException ignored) {
            // not supported on this device
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Past the first resume the engine is running its intro/gameplay: uninterrupted gameplay state, not loading.
        if (started) updateGameState(false, GameState.MODE_GAMEPLAY_UNINTERRUPTIBLE);
        started = true;
    }

    private boolean started;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        int orientation = getSharedPreferences("controls", MODE_PRIVATE).getInt("display_orientation", 0);
        if (orientation == 1) {
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        } else if (orientation == 2) {
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE);
        }
        applySustainedPerformance();
        prepareVulkanDriver();
        updateGameState(true, GameState.MODE_NONE);  // loading: the platform may boost CPU/IO
        ModManager.prepareForLaunch(this);
        // The native side loads the bundled Vulkan driver (Turnip, on Adreno) from the extracted libraries.
        try {
            Os.setenv("PROTODROID_NATIVE_LIB_DIR", getApplicationInfo().nativeLibraryDir, true);
        } catch (ErrnoException ignored) {
            // the system Vulkan driver is used
        }
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);  // no dimming while playing
        // On-screen controls above SDL's surface; they feed SDL keyboard and mouse input.
        addContentView(new TouchControls(this), new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }
}
