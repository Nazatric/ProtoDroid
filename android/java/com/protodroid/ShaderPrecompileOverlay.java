package com.protodroid;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.view.View;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Full-screen cover shown while the one-time shader precompile tour runs underneath. The tour drives the
 * engine itself; the player sees a loading screen in the launcher's visual language (black, Prototype red,
 * Barlow Condensed) with a determinate progress bar fed from the tour's phase.txt, instead of watching the
 * automated playthrough. Progress blends phase completion with the journal's presented-frame counter so the
 * bar moves smoothly inside long phases.
 */
public final class ShaderPrecompileOverlay extends View {
    private static final int RED = 0xffc9141a;
    private static final int RED_DARK = 0xff7d0b10;
    private static final int RED_HOT = 0xffff2b2b;
    private static final int TEXT_DIM = 0xff8a8f98;

    private static final String TOUR_DIR = "/storage/emulated/0/Prototype/diagnostic-tour/logs";
    private static final int PHASE_COUNT = 41;

    private static final Map<String, String> PHASE_LABELS = new HashMap<>();
    static {
        PHASE_LABELS.put("render_shaders_pipelines", "Building core rendering pipelines");
        PHASE_LABELS.put("camera_orbit", "Warming camera and lighting");
        PHASE_LABELS.put("shadow_ground_view", "Compiling shadow passes");
        PHASE_LABELS.put("shadow_motion", "Compiling dynamic shadows");
        PHASE_LABELS.put("movement_directions", "Loading movement and animation");
        PHASE_LABELS.put("sprint_sustained", "Loading sustained sprint");
        PHASE_LABELS.put("charged_jump", "Compiling jump and glide effects");
        PHASE_LABELS.put("glide_dash_chain", "Compiling air-dash effects");
        PHASE_LABELS.put("building_climb_forward", "Compiling climbing shaders");
        PHASE_LABELS.put("building_climb_strafe", "Compiling wall-run shaders");
        PHASE_LABELS.put("wall_grab_climb", "Compiling ledge grab shaders");
        PHASE_LABELS.put("rooftop_leap_descent", "Compiling leap and fall effects");
        PHASE_LABELS.put("parkour_obstacles", "Loading parkour animation");
        PHASE_LABELS.put("landing_recovery", "Compiling landing effects");
        PHASE_LABELS.put("jump_sprint", "Loading jump timing systems");
        PHASE_LABELS.put("target_lock", "Compiling target lock view");
        PHASE_LABELS.put("combat_primary", "Compiling combat effects");
        PHASE_LABELS.put("combat_charged", "Compiling charged attacks");
        PHASE_LABELS.put("grab_followup", "Compiling grab combos");
        PHASE_LABELS.put("grab_throw_movement", "Compiling throw effects");
        PHASE_LABELS.put("powers_and_abilities", "Compiling powers and abilities");
        PHASE_LABELS.put("power_wheel", "Loading power wheel");
        PHASE_LABELS.put("power_combat_combo", "Compiling power combos");
        PHASE_LABELS.put("disguise_wheel", "Loading disguise wheel");
        PHASE_LABELS.put("disguise_forms", "Compiling disguise forms");
        PHASE_LABELS.put("disguise_movement", "Compiling disguise movement");
        PHASE_LABELS.put("particles_effects", "Compiling particles and effects");
        PHASE_LABELS.put("lighting_exposure", "Compiling lighting and exposure");
        PHASE_LABELS.put("assets_streaming", "Streaming world assets");
        PHASE_LABELS.put("assets_streaming_glide", "Streaming skyline assets");
        PHASE_LABELS.put("audio_mix", "Warming up audio mixing");
        PHASE_LABELS.put("audio_combat_stress", "Stressing combat audio");
        PHASE_LABELS.put("bink_video", "Warming video playback");
        PHASE_LABELS.put("android_touch_overlay", "Loading touch overlay");
        PHASE_LABELS.put("save_isolation", "Verifying save isolation");
        PHASE_LABELS.put("memory_thread_stress", "Stressing memory and threads");
        PHASE_LABELS.put("input_release_stress", "Stressing input systems");
        PHASE_LABELS.put("sustained_combat", "Compiling sustained combat");
        PHASE_LABELS.put("sustained_parkour", "Compiling sustained parkour");
        PHASE_LABELS.put("sustained_effects", "Compiling heavy effects");
        PHASE_LABELS.put("recovery_idle", "Finishing up");
    }

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text;
    private final Typeface medium;
    private final Typeface semibold;
    private final Typeface bold;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            refreshProgress();
            invalidate();
            if (progress < 1f) handler.postDelayed(this, 500);
        }
    };

    private volatile float progress;
    private volatile String phaseLabel = "Starting the engine";
    private volatile int phaseIndex;
    private long startedAt;

    public ShaderPrecompileOverlay(Context context) {
        super(context);
        setBackgroundColor(Color.BLACK);
        medium = typeface("barlow_condensed_medium.ttf");
        semibold = typeface("barlow_condensed_semibold.ttf");
        bold = typeface("barlow_condensed_bold.ttf");
        text = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        startedAt = android.os.SystemClock.uptimeMillis();
        handler.post(tick);
    }

    private Typeface typeface(String name) {
        try {
            return Typeface.createFromAsset(getContext().getAssets(), "setup/fonts/" + name);
        } catch (RuntimeException e) {
            return Typeface.DEFAULT;
        }
    }

    /** phase.txt: "phase=<key>\nindex=<n>\nstatus=...". Result presence clamps progress to 100%. */
    private void refreshProgress() {
        int index = -1;
        String key = null;
        try (BufferedReader r = new BufferedReader(new FileReader(new File(TOUR_DIR, "phase.txt")))) {
            for (String line; (line = r.readLine()) != null;) {
                line = line.trim();
                if (line.startsWith("phase=")) key = line.substring(6).trim();
                else if (line.startsWith("index=")) index = Integer.parseInt(line.substring(6).trim());
            }
        } catch (IOException | NumberFormatException ignored) {
            // not written yet; keep showing the startup label
        }
        if (index < 0) return;
        phaseIndex = index;
        phaseLabel = PHASE_LABELS.containsKey(key) ? PHASE_LABELS.get(key)
                : "Optimizing shaders";
        // Blend phase completion with elapsed-time interpolation inside each phase so the bar
        // never freezes during long phases (the tour reports phase changes, not sub-progress).
        float phasePart = index / (float) PHASE_COUNT;
        float inner = Math.min(1f, (android.os.SystemClock.uptimeMillis() - startedAt) / 240000f);
        progress = Math.max(progress, Math.min(0.99f, phasePart + inner / PHASE_COUNT));
        if (new File(TOUR_DIR, "result.txt").exists()) progress = Math.max(progress, 0.995f);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        final float w = getWidth(), h = getHeight();
        final float density = getResources().getDisplayMetrics().density;

        // Title block, top-left: wordmark + red accent, mirroring the launcher header.
        text.setTypeface(bold);
        text.setColor(Color.WHITE);
        text.setLetterSpacing(0.18f);
        text.setTextSize(15f * density);
        canvas.drawText("PROTOTYPE", 28f * density, 44f * density, text);
        text.setLetterSpacing(0f);
        fill.setColor(RED);
        canvas.drawRect(28f * density, 52f * density, 28f * density + 34f * density, 55f * density, fill);

        // Center block.
        text.setTypeface(semibold);
        text.setColor(Color.WHITE);
        text.setTextSize(34f * density);
        canvas.drawText("OPTIMIZING SHADERS", 28f * density, h * 0.42f, text);
        text.setTypeface(medium);
        text.setColor(TEXT_DIM);
        text.setTextSize(15f * density);
        canvas.drawText("This happens once — the game starts by itself", 28f * density,
                h * 0.42f + 26f * density, text);

        // Progress track.
        final float trackY = h * 0.42f + 58f * density;
        final float trackW = w - 56f * density;
        fill.setColor(RED_DARK);
        canvas.drawRect(28f * density, trackY, 28f * density + trackW, trackY + 4f * density, fill);
        fill.setColor(RED);
        canvas.drawRect(28f * density, trackY, 28f * density + trackW * progress, trackY + 4f * density, fill);
        fill.setColor(RED_HOT);
        float head = 28f * density + trackW * progress;
        canvas.drawRect(head - 2f * density, trackY - 1f * density, head, trackY + 5f * density, fill);

        // Percent + current phase.
        text.setTypeface(bold);
        text.setColor(RED_HOT);
        text.setTextSize(15f * density);
        canvas.drawText(String.format(Locale.US, "%d%%", Math.round(progress * 100f)),
                28f * density, trackY + 34f * density, text);
        text.setTypeface(medium);
        text.setColor(Color.WHITE);
        text.setTextSize(16f * density);
        canvas.drawText(phaseLabel, 28f * density, trackY + 58f * density, text);

        // Footer: phase counter, dim.
        text.setTypeface(medium);
        text.setColor(TEXT_DIM);
        text.setTextSize(13f * density);
        String footer = phaseIndex >= 0
                ? String.format(Locale.US, "shader batch %d of %d · keep the app open", phaseIndex + 1, PHASE_COUNT)
                : "keep the app open";
        canvas.drawText(footer, 28f * density, h - 24f * density, text);

        // Launcher-style red scan band sweeping slowly across the whole screen.
        float sweep = ((android.os.SystemClock.uptimeMillis() - startedAt) % 4200f) / 4200f;
        float band = w * 0.45f;
        float x = (sweep * 1.6f - 0.3f) * w;
        fill.setShader(new LinearGradient(x - band / 2f, 0f, x + band / 2f, 0f,
                new int[]{Color.TRANSPARENT, 0x33ff463c, Color.TRANSPARENT},
                null, Shader.TileMode.CLAMP));
        canvas.drawRect(0f, 0f, w, h, fill);
        fill.setShader(null);
    }

    @Override
    protected void onDetachedFromWindow() {
        handler.removeCallbacks(tick);
        super.onDetachedFromWindow();
    }
}
