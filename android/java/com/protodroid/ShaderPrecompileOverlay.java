package com.protodroid;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The tour's loading screen: while the isolated precompile run injects its own input and
 * replays the scenario, this replaces the on-screen controls. The status line is live: the
 * native side mirrors its current phase and DXVK state-cache size into phase.txt, and this
 * overlay reads it once a second so the wait is observable instead of a frozen label.
 */
public class ShaderPrecompileOverlay extends FrameLayout {

    private static final java.io.File PHASE_FILE = new java.io.File(
            "/storage/emulated/0/Prototype/diagnostic-tour/logs/phase.txt");

    private final TextView status;
    private final TextView detail;
    private final android.os.Handler handler;
    private final Runnable progress = new Runnable() {
        private int elapsedSec;

        @Override public void run() {
            elapsedSec++;
            String phase = readPhaseKey("phase");
            String cache = readPhaseKey("cache_bytes");
            String line;
            if ("startup".equals(phase) || "gameplay_wait".equals(phase)) {
                String presses = readPhaseKey("menu_presses");
                line = presses == null ? "STARTING THE AUTOMATED RUN"
                        : "CONFIRMING MAIN MENU (PRESS " + presses + ") — ENTERING GAME";
            } else if (phase != null) {
                line = "OPTIMIZING — " + phase.replace('_', ' ').toUpperCase();
            } else {
                line = "BUILDING PIPELINES";
            }
            status.setText(line);
            String detailText = "ELAPSED " + mmss(elapsedSec);
            if (cache != null) {
                long bytes = parseLong(cache, -1);
                if (bytes >= 0) detailText += "   ·   SHADER CACHE " + (bytes + 1023) / 1024 + " KB";
            }
            detail.setText(detailText);
            handler.postDelayed(this, 1000);
        }
    };

    /** Reads one key=value pair from the tour's live phase marker; null while it does not exist yet. */
    private static String readPhaseKey(String key) {
        try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader(PHASE_FILE))) {
            String prefix = key + "=";
            for (String line; (line = reader.readLine()) != null;) {
                line = line.trim();
                if (line.startsWith(prefix)) return line.substring(prefix.length()).trim();
            }
        } catch (java.io.IOException ignored) {
            // not written yet
        }
        return null;
    }

    private static long parseLong(String value, long fallback) {
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static String mmss(int totalSeconds) {
        return totalSeconds / 60 + ":" + String.format("%02d", totalSeconds % 60);
    }

    public ShaderPrecompileOverlay(Context context) {
        super(context);
        handler = new android.os.Handler(context.getMainLooper());
        setBackgroundColor(Color.parseColor("#FF0A0A0B"));
        setClickable(true);  // the tour injects input at the engine level; keep touches off the game

        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER);
        addView(column, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        TextView title = new TextView(context);
        title.setTypeface(font(context, "barlow_condensed_semibold.ttf"));
        title.setText("PROTOTYPE");
        title.setTextSize(42f);
        title.setTextColor(Color.WHITE);
        title.setLetterSpacing(0.14f);
        column.addView(title);

        View rule = new View(context);
        rule.setBackgroundColor(Color.parseColor("#FFC9141A"));
        LinearLayout.LayoutParams ruleParams =
                new LinearLayout.LayoutParams(dp(context, 120), dp(context, 3));
        ruleParams.setMargins(0, dp(context, 14), 0, dp(context, 18));
        column.addView(rule, ruleParams);

        status = new TextView(context);
        status.setTypeface(font(context, "barlow_condensed_medium.ttf"));
        status.setTextSize(17f);
        status.setTextColor(Color.parseColor("#FFC9CDD4"));
        status.setLetterSpacing(0.10f);
        column.addView(status);

        detail = new TextView(context);
        detail.setTypeface(font(context, "barlow_condensed_medium.ttf"));
        detail.setTextSize(14f);
        detail.setTextColor(Color.parseColor("#FF8A8F97"));
        detail.setLetterSpacing(0.08f);
        detail.setPadding(0, dp(context, 8), 0, 0);
        column.addView(detail);

        TextView note = new TextView(context);
        note.setTypeface(font(context, "barlow_condensed_medium.ttf"));
        note.setTextSize(13f);
        note.setTextColor(Color.parseColor("#FF70747C"));
        note.setText("One automated run. The game starts by itself when it finishes.");
        note.setPadding(0, dp(context, 10), 0, 0);
        column.addView(note);

        handler.post(progress);
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        handler.removeCallbacks(progress);
    }

    private Typeface font(Context context, String name) {
        try {
            return Typeface.createFromAsset(context.getAssets(), "setup/fonts/" + name);
        } catch (RuntimeException e) {
            return Typeface.DEFAULT;
        }
    }

    private static int dp(Context context, int value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                context.getResources().getDisplayMetrics()));
    }
}
