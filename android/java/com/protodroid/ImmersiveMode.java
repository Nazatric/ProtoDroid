package com.protodroid;

import android.app.Activity;
import android.os.Build;
import android.util.Log;
import android.view.View;
import android.view.Window;

/** Launcher immersive mode. A system-edge swipe still reveals navigation temporarily, as Android intends. */
final class ImmersiveMode {
    private static final String TAG = "ProtoDroidImmersive";

    private ImmersiveMode() {}

    static void apply(Activity activity) {
        apply(activity.getWindow());
    }

    static void apply(Window window) {
        if (window == null) return;
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                // Keep API-30 types out of the class loaded during Android 8–10 launcher startup.
                Api30Impl.apply(window);
                return;
            } catch (RuntimeException | LinkageError e) {
                // A vendor insets implementation must not prevent the launcher from opening.
                Log.w(TAG, "System immersive controller failed; using legacy flags", e);
            }
        }
        applyLegacy(window);
    }

    private static void applyLegacy(Window window) {
        window.getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    /** Loaded only on API 30+, so older ART never verifies modern insets types during startup. */
    private static final class Api30Impl {
        private Api30Impl() {}

        static void apply(Window window) {
            android.view.WindowInsetsController controller = window.getInsetsController();
            if (controller == null) {
                applyLegacy(window);
                return;
            }
            controller.setSystemBarsBehavior(
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            controller.hide(android.view.WindowInsets.Type.statusBars()
                    | android.view.WindowInsets.Type.navigationBars());
        }
    }
}
