package com.protodroid;

import android.app.Dialog;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The one-time shader precompile offer, drawn in the launcher's visual language instead of a
 * system AlertDialog: black field, PROTOTYPE wordmark with the red accent bar, Barlow
 * Condensed type, and three stacked actions — solid red primary, red-outlined secondary, and
 * a plain tertiary line. Same three behaviors as before: run the tour now, play without
 * optimizing, or suppress the offer.
 */
final class PrecompileDialog {

    private static final int RED = 0xffc9141a;
    private static final int RED_DARK = 0xff7d0b10;
    private static final int RED_HOT = 0xffff2b2b;
    private static final int TEXT_DIM = 0xff8a8f98;

    interface Actions {
        void loadAllShadersNow();
        void playWithoutOptimizing();
        void dontAskAgain();
    }

    static void show(Context context, Actions actions) {
        final float dp = context.getResources().getDisplayMetrics().density;
        Typeface medium = typeface(context, "barlow_condensed_medium.ttf");
        Typeface semibold = typeface(context, "barlow_condensed_semibold.ttf");
        Typeface bold = typeface(context, "barlow_condensed_bold.ttf");

        final Dialog dialog = new Dialog(context, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
        dialog.setCancelable(false);

        FrameLayout root = new FrameLayout(context);
        root.setBackgroundColor(Color.BLACK);

        // Header: wordmark + red accent bar, mirroring the launcher and the precompile overlay.
        TextView wordmark = new TextView(context);
        wordmark.setTypeface(bold);
        wordmark.setText("PROTOTYPE");
        wordmark.setTextColor(Color.WHITE);
        wordmark.setTextSize(15f);
        wordmark.setLetterSpacing(0.18f);
        wordmark.setPadding(px(28, dp), px(28, dp), 0, 0);
        root.addView(wordmark, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        View accent = new View(context) {
            @Override
            protected void onDraw(android.graphics.Canvas canvas) {
                android.graphics.Paint fill = new android.graphics.Paint();
                fill.setColor(RED);
                canvas.drawRect(0f, 0f, px(34, dp), px(3, dp), fill);
            }
        };
        FrameLayout.LayoutParams accentParams = new FrameLayout.LayoutParams(
                px(34, dp), px(3, dp));
        accentParams.topMargin = px(52, dp);
        accentParams.leftMargin = px(28, dp);
        root.addView(accent, accentParams);

        LinearLayout body = new LinearLayout(context);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setGravity(Gravity.CENTER_VERTICAL);
        int pad = px(28, dp);
        body.setPadding(pad, 0, pad, 0);

        TextView title = new TextView(context);
        title.setTypeface(semibold);
        title.setText("OPTIMIZE GRAPHICS ONCE?");
        title.setTextColor(Color.WHITE);
        title.setTextSize(32f);
        title.setLetterSpacing(0.02f);
        body.addView(title, linearParams(true));

        TextView message = new TextView(context);
        message.setTypeface(medium);
        message.setText("A one-time calibration will build every shader ahead of time, so "
                + "gameplay does not stutter.\n\nIt takes about 4 minutes, runs only once, "
                + "and the game starts by itself when it is done.");
        message.setTextColor(TEXT_DIM);
        message.setTextSize(16f);
        message.setLineSpacing(px(3, dp), 1f);
        message.setPadding(0, px(14, dp), 0, px(22, dp));
        body.addView(message, linearParams(true));

        body.addView(button(context, bold, "LOAD ALL SHADERS NOW", Color.WHITE, RED, true,
                () -> { dialog.dismiss(); actions.loadAllShadersNow(); }), withTopMargin(px(6, dp), dp));
        body.addView(button(context, semibold, "PLAY WITHOUT OPTIMIZING", Color.WHITE, RED_DARK, false,
                () -> { dialog.dismiss(); actions.playWithoutOptimizing(); }), withTopMargin(px(12, dp), dp));
        TextView dontAsk = new TextView(context);
        dontAsk.setTypeface(medium);
        dontAsk.setText("Don't ask again");
        dontAsk.setTextColor(TEXT_DIM);
        dontAsk.setTextSize(15f);
        dontAsk.setPadding(0, px(18, dp), 0, px(8, dp));
        dontAsk.setOnClickListener(v -> { dialog.dismiss(); actions.dontAskAgain(); });
        body.addView(dontAsk, withTopMargin(px(8, dp), dp));

        root.addView(body, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        dialog.setContentView(root);
        dialog.show();
    }

    private interface Click { void run(); }

    private static TextView button(Context context, Typeface face, String label, int textColor,
                                   int chromeColor, boolean filled, Click click) {
        final float dp = context.getResources().getDisplayMetrics().density;
        TextView view = new TextView(context);
        view.setTypeface(filled ? face : face);
        view.setText(label);
        view.setTextColor(textColor);
        view.setTextSize(filled ? 19f : 17f);
        view.setLetterSpacing(0.06f);
        view.setGravity(Gravity.CENTER);
        view.setPadding(0, filled ? px(13, dp) : px(11, dp), 0, filled ? px(13, dp) : px(11, dp));
        GradientDrawable chrome = new GradientDrawable();
        chrome.setColor(filled ? chromeColor : Color.TRANSPARENT);
        chrome.setStroke(px(filled ? 0 : 1, dp), filled ? Color.TRANSPARENT : chromeColor);
        view.setBackground(chrome);
        view.setOnClickListener(v -> click.run());
        return view;
    }

    private static LinearLayout.LayoutParams linearParams(boolean widthMatch) {
        return new LinearLayout.LayoutParams(
                widthMatch ? ViewGroup.LayoutParams.MATCH_PARENT : ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private static LinearLayout.LayoutParams withTopMargin(int top, float dp) {
        LinearLayout.LayoutParams p = linearParams(true);
        p.setMargins(0, top, 0, 0);
        return p;
    }

    private static int px(int dp, float density) {
        return Math.round(dp * density);
    }

    private static Typeface typeface(Context context, String name) {
        try {
            return Typeface.createFromAsset(context.getAssets(), "setup/fonts/" + name);
        } catch (RuntimeException e) {
            return Typeface.DEFAULT;
        }
    }

    private PrecompileDialog() {}
}
