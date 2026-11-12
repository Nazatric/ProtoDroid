package com.protodroid;

import android.app.Dialog;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The one-time shader precompile offer, in the launcher's visual language: black panel, red
 * accent, Barlow Condensed. "Load all shaders now" runs the automated tour once in its own
 * process; the launcher then relaunches normal Play with the tour's cache seeded in. Play
 * without optimizing skips it for this session; "don't ask again" writes the marker the
 * native side also writes after a successful seeding.
 */
public class PrecompileDialog {

    interface Actions {
        void loadAllShadersNow();

        void playWithoutOptimizing();

        void dontAskAgain();
    }

    static void show(Context context, Actions actions) {
        final Dialog dialog = new Dialog(context);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout panel = new LinearLayout(context);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(Color.parseColor("#F2000000"));
        int pad = dp(context, 22);
        panel.setPadding(pad, pad, pad, dp(context, 14));

        GradientDrawable chrome = new GradientDrawable();
        chrome.setColor(Color.parseColor("#F2000000"));
        chrome.setStroke(dp(context, 1), Color.parseColor("#FFC9141A"));
        panel.setBackground(chrome);

        panel.addView(title(context, "SHADER OPTIMIZATION", 22f, Color.WHITE));
        TextView body = new TextView(context);
        body.setTypeface(font(context, "barlow_condensed_medium.ttf"));
        body.setTextSize(16f);
        body.setTextColor(Color.parseColor("#FFB9BEC7"));
        body.setText("Prototype can build most of its graphics pipelines up front, in one automated "
                + "run, which removes the stutter you would otherwise feel during the first minutes "
                + "of play. The run drives the menu and the opening area by itself and usually takes "
                + "5-10 minutes; the game starts on its own when it finishes. You can also skip this "
                + "- the cache keeps growing as you play.");
        body.setPadding(0, dp(context, 10), 0, dp(context, 16));
        panel.addView(body, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        panel.addView(button(context, "LOAD ALL SHADERS NOW", true, v -> {
            dialog.dismiss();
            actions.loadAllShadersNow();
        }));
        panel.addView(button(context, "PLAY WITHOUT OPTIMIZING", false, v -> {
            dialog.dismiss();
            actions.playWithoutOptimizing();
        }));
        panel.addView(button(context, "DON'T ASK AGAIN", false, v -> {
            dialog.dismiss();
            actions.dontAskAgain();
        }));

        dialog.setContentView(panel);
        dialog.getWindow().setLayout(dp(context, 420), android.view.WindowManager.LayoutParams.WRAP_CONTENT);
        dialog.getWindow().setBackgroundDrawableResource(android.R.color.transparent);
        dialog.show();
    }

    private static TextView title(Context context, String text, float size, int color) {
        TextView view = new TextView(context);
        view.setTypeface(font(context, "barlow_condensed_semibold.ttf"));
        view.setText(text);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setLetterSpacing(0.06f);
        return view;
    }

    private static View button(Context context, String text, boolean primary, View.OnClickListener listener) {
        TextView view = new TextView(context);
        view.setTypeface(font(context, "barlow_condensed_semibold.ttf"));
        view.setText(text);
        view.setTextSize(17f);
        view.setLetterSpacing(0.05f);
        view.setGravity(Gravity.CENTER);
        view.setPadding(0, dp(context, 12), 0, dp(context, 12));
        GradientDrawable chrome = new GradientDrawable();
        chrome.setColor(primary ? Color.parseColor("#FFC9141A") : Color.TRANSPARENT);
        chrome.setStroke(dp(context, 1), primary ? Color.parseColor("#FFC9141A") : 0x33FFFFFF);
        view.setBackground(chrome);
        view.setTextColor(primary ? Color.WHITE : Color.parseColor("#FFC9CDD4"));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(context, 8);
        view.setLayoutParams(params);
        view.setOnClickListener(listener);
        return view;
    }

    private static Typeface font(Context context, String name) {
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
