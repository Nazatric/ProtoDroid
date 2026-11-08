package com.protodroid;

import android.animation.ValueAnimator;
import android.app.Activity;

/** Shared motion policy for the custom launcher views. Android's reduced-motion setting is authoritative. */
final class LauncherMotion {
    private LauncherMotion() {}

    static boolean enabled() {
        return ValueAnimator.areAnimatorsEnabled();
    }

    static float clamp01(float value) {
        return Math.max(0f, Math.min(1f, value));
    }

    static float easeOut(float value) {
        value = clamp01(value);
        return 1f - (float) Math.pow(1f - value, 4);
    }

    static void transition(Activity activity) {
        if (enabled()) activity.overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out);
    }

    static void finishTransition(Activity activity) {
        if (enabled()) activity.overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out);
    }
}
