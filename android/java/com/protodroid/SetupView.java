package com.protodroid;

import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Build;
import android.os.SystemClock;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.util.Log;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.WindowInsets;

import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;

/**
 * Native Android rendering of the Prototype setup design. It deliberately uses only platform APIs so the existing
 * non-Gradle APK build does not need a Compose runtime. Game discovery and Android permission policy remain in
 * SetupActivity; this class only renders its state and reports the three actions.
 */
final class SetupView extends View {
    interface Listener {
        void onAllowFileAccess();
        void onCheckAgain();
        void onExit();
    }

    static final class State {
        final boolean fileAccess;
        final boolean gameFound;
        final boolean checking;
        final String status;
        final String primaryPath;
        final String secondaryPath;
        final String alternateSharedPath;

        State(boolean fileAccess, boolean gameFound, boolean checking, String status, String primaryPath,
              String secondaryPath, String alternateSharedPath) {
            this.fileAccess = fileAccess;
            this.gameFound = gameFound;
            this.checking = checking;
            this.status = status;
            this.primaryPath = primaryPath;
            this.secondaryPath = secondaryPath;
            this.alternateSharedPath = alternateSharedPath;
        }
    }

    private static final String TAG = "ProtoDroidSetup";
    private static final int RED = 0xffc9141a;
    private static final int RED_DARK = 0xff7d0b10;
    private static final int RED_HOT = 0xffff2b2b;
    private static final int BONE = 0xffece8e1;
    private static final int DIM = 0xffa9a49b;
    private static final int OK = 0xff8fe07a;

    private static final int ACTION_NONE = 0;
    private static final int ACTION_ALLOW = 1;
    private static final int ACTION_CHECK = 2;
    private static final int ACTION_EXIT = 3;

    private static final int BUTTON_NORMAL = 0;
    private static final int BUTTON_PRIMARY = 1;
    private static final int BUTTON_GHOST = 2;
    private static final int BUTTON_DONE = 3;

    private static final long SLIDE_HOLD_MS = 6500L;
    private static final String[] REQUIREMENTS = {
            "prototypeenginef.dll", ".rcf", ".p3d", "art", "audio", "scripts"
    };

    private final Listener listener;
    private final Bitmap[] slides = new Bitmap[4];
    private final Bitmap icon;
    private final Typeface medium;
    private final Typeface semibold;
    private final Typeface bold;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bitmapPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG | Paint.DITHER_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Path path = new Path();
    private final RectF allowBounds = new RectF();
    private final RectF checkBounds = new RectF();
    private final RectF exitBounds = new RectF();
    private final float[] buttonFill = new float[4];
    private final long startedAt = SystemClock.uptimeMillis();
    private final float density;
    private final float fontScale;
    private final int touchSlop;

    private State state = new State(false, false, false, "INITIALIZING...", "", "", "");
    private Shader horizontalShade;
    private Shader verticalShade;
    private int shadeWidth = -1;
    private int shadeHeight = -1;
    private int insetLeft;
    private int insetTop;
    private int insetRight;
    private int insetBottom;
    private long lastFrameAt;
    private int pressedAction = ACTION_NONE;
    private int focusedAction = ACTION_NONE;
    private int splatAction = ACTION_NONE;
    private float splatX;
    private float splatY;
    private long splatStartedAt;
    private boolean scrolling;
    private float touchStartY;
    private float lastTouchY;
    private float scrollY;
    private float maxScrollY;
    private boolean layoutDirty = true;
    private float panelLayoutWidth = -1f;
    private float panelLayoutUnit = -1f;
    private StaticLayout primaryPathLayout;
    private StaticLayout primaryNoteLayout;
    private StaticLayout secondaryPathLayout;
    private StaticLayout secondaryNoteLayout;

    SetupView(Context context, Listener listener) {
        super(context);
        this.listener = listener;
        density = getResources().getDisplayMetrics().density;
        fontScale = getResources().getConfiguration().fontScale;
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        setBackgroundColor(Color.BLACK);
        setClickable(true);
        setFocusable(true);
        setContentDescription("Prototype game file setup");

        AssetManager assets = context.getAssets();
        medium = loadFont(assets, "setup/fonts/barlow_condensed_medium.ttf");
        semibold = loadFont(assets, "setup/fonts/barlow_condensed_semibold.ttf");
        bold = loadFont(assets, "setup/fonts/barlow_condensed_bold.ttf");
        for (int i = 0; i < slides.length; i++) {
            slides[i] = loadBitmap(assets, "setup/images/bg_" + (i + 1) + ".jpg");
        }
        icon = loadBitmap(assets, "setup/images/protodroid_icon.png");
    }

    private static Typeface loadFont(AssetManager assets, String name) {
        try {
            return Typeface.createFromAsset(assets, name);
        } catch (RuntimeException e) {
            Log.w(TAG, "Unable to load " + name, e);
            return Typeface.DEFAULT;
        }
    }

    private static Bitmap loadBitmap(AssetManager assets, String name) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        try (InputStream input = assets.open(name)) {
            return BitmapFactory.decodeStream(input, null, options);
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "Unable to load " + name, e);
            return null;
        }
    }

    void setState(State next) {
        state = next;
        layoutDirty = true;
        if (focusedAction == ACTION_ALLOW && next.fileAccess) focusedAction = ACTION_NONE;
        if (focusedAction == ACTION_CHECK && next.checking) focusedAction = ACTION_NONE;
        setContentDescription("Prototype setup. " + next.status + ". Storage access "
                + (next.fileAccess ? "granted" : "required"));
        invalidate();
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        insetLeft = insets.getSystemWindowInsetLeft();
        insetTop = insets.getSystemWindowInsetTop();
        insetRight = insets.getSystemWindowInsetRight();
        insetBottom = insets.getSystemWindowInsetBottom();
        if (Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
            insetLeft = Math.max(insetLeft, insets.getDisplayCutout().getSafeInsetLeft());
            insetTop = Math.max(insetTop, insets.getDisplayCutout().getSafeInsetTop());
            insetRight = Math.max(insetRight, insets.getDisplayCutout().getSafeInsetRight());
            insetBottom = Math.max(insetBottom, insets.getDisplayCutout().getSafeInsetBottom());
        }
        invalidate();
        return super.onApplyWindowInsets(insets);
    }

    private float unit(boolean portrait) {
        if (portrait) return density;
        float usableDp = Math.max(1f, getHeight() - insetTop - insetBottom) / density;
        float scale = Math.max(0.8f, Math.min(2.2f, usableDp / 360f));
        return density * scale;
    }

    private Typeface font(int weight) {
        if (weight >= 700) return bold;
        if (weight >= 600) return semibold;
        return medium;
    }

    private TextPaint configure(TextPaint target, float size, int weight, int color, float spacing, float u) {
        target.setTypeface(font(weight));
        target.setTextSize(size * u * fontScale);
        target.setColor(color);
        target.setLetterSpacing(size == 0f ? 0f : spacing / size);
        target.setStyle(Paint.Style.FILL);
        return target;
    }

    private TextPaint newText(float size, int weight, int color, float spacing, float u) {
        return configure(new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG),
                size, weight, color, spacing, u);
    }

    private static int withAlpha(int color, float alpha) {
        int a = Math.max(0, Math.min(255, Math.round(alpha * 255f)));
        return (color & 0x00ffffff) | (a << 24);
    }

    private static float clamp01(float value) {
        return Math.max(0f, Math.min(1f, value));
    }

    private static float easeOut(float value) {
        value = clamp01(value);
        return 1f - (float) Math.pow(1f - value, 4);
    }

    private void ensureShaders(int width, int height) {
        if (shadeWidth == width && shadeHeight == height) return;
        shadeWidth = width;
        shadeHeight = height;
        horizontalShade = new LinearGradient(0f, 0f, width, 0f,
                new int[]{withAlpha(Color.BLACK, 0.90f), withAlpha(Color.BLACK, 0.65f),
                        withAlpha(Color.BLACK, 0.18f), withAlpha(Color.BLACK, 0.08f)},
                new float[]{0f, 0.38f, 0.62f, 1f}, Shader.TileMode.CLAMP);
        verticalShade = new LinearGradient(0f, 0f, 0f, height,
                new int[]{withAlpha(Color.BLACK, 0.60f), Color.TRANSPARENT,
                        Color.TRANSPARENT, withAlpha(Color.BLACK, 0.70f)},
                new float[]{0f, 0.20f, 0.62f, 1f}, Shader.TileMode.CLAMP);
    }

    private void drawBackdrop(Canvas canvas, long now) {
        int width = getWidth();
        int height = getHeight();
        canvas.drawColor(Color.BLACK);
        long elapsed = Math.max(0L, now - startedAt);
        int slideNumber = (int) (elapsed / SLIDE_HOLD_MS);
        int index = slideNumber % slides.length;
        long phase = elapsed % SLIDE_HOLD_MS;
        float zoom = 1.04f + 0.12f * clamp01(phase / 11000f);
        if (slideNumber > 0 && phase < 1300L) {
            int previous = (index + slides.length - 1) % slides.length;
            drawSlide(canvas, slides[previous], 1f, 1.04f + 0.12f * clamp01(SLIDE_HOLD_MS / 11000f));
            drawSlide(canvas, slides[index], easeOut(phase / 1300f), zoom);
        } else {
            drawSlide(canvas, slides[index], 1f, zoom);
        }

        if (slideNumber > 0 && phase < 950L) {
            float progress = easeOut(phase / 950f);
            float band = width * 0.4f;
            float x = (progress * 1.5f - 0.25f) * width;
            paint.setShader(new LinearGradient(x - band / 2f, 0f, x + band / 2f, 0f,
                    new int[]{Color.TRANSPARENT, withAlpha(0xffff463c, 0.50f), Color.TRANSPARENT},
                    null, Shader.TileMode.CLAMP));
            canvas.drawRect(0f, 0f, width, height, paint);
            paint.setShader(null);
        }

        ensureShaders(width, height);
        paint.setShader(horizontalShade);
        canvas.drawRect(0f, 0f, width, height, paint);
        paint.setShader(verticalShade);
        canvas.drawRect(0f, 0f, width, height, paint);
        paint.setShader(null);
    }

    private void drawSlide(Canvas canvas, Bitmap bitmap, float alpha, float zoom) {
        if (bitmap == null || bitmap.isRecycled() || alpha <= 0f) return;
        float scale = Math.max(getWidth() / (float) bitmap.getWidth(), getHeight() / (float) bitmap.getHeight()) * zoom;
        float width = bitmap.getWidth() * scale;
        float height = bitmap.getHeight() * scale;
        float left = (getWidth() - width) / 2f;
        float top = (getHeight() - height) / 2f;
        bitmapPaint.setAlpha(Math.round(clamp01(alpha) * 255f));
        canvas.drawBitmap(bitmap, null, new RectF(left, top, left + width, top + height), bitmapPaint);
        bitmapPaint.setAlpha(255);
    }

    private float baseline(float top, float lineHeight, TextPaint paint) {
        Paint.FontMetrics fm = paint.getFontMetrics();
        return top + (lineHeight - (fm.descent - fm.ascent)) / 2f - fm.ascent;
    }

    private void drawText(Canvas canvas, String value, float x, float y, float size, int weight, int color,
                          float spacing, float u, float maxWidth) {
        if (value == null) return;
        configure(text, size, weight, color, spacing, u);
        String shown = value;
        if (maxWidth > 0f && text.measureText(value) > maxWidth) {
            float ellipsis = text.measureText("\u2026");
            int count = text.breakText(value, true, Math.max(0f, maxWidth - ellipsis), null);
            shown = value.substring(0, Math.max(0, count)) + "\u2026";
        }
        canvas.drawText(shown, x, y, text);
    }

    private void drawLine(Canvas canvas, String value, float x, float top, float lineHeight, float size, int weight,
                          int color, float spacing, float u, float maxWidth) {
        configure(text, size, weight, color, spacing, u);
        drawText(canvas, value, x, baseline(top, lineHeight, text), size, weight, color, spacing, u, maxWidth);
    }

    private void drawCentered(Canvas canvas, String value, RectF bounds, float size, int weight, int color,
                              float spacing, float u, float horizontalPadding) {
        configure(text, size, weight, color, spacing, u);
        float maxWidth = Math.max(0f, bounds.width() - horizontalPadding * 2f);
        String shown = value;
        if (text.measureText(value) > maxWidth) {
            float ellipsis = text.measureText("\u2026");
            int count = text.breakText(value, true, Math.max(0f, maxWidth - ellipsis), null);
            shown = value.substring(0, Math.max(0, count)) + "\u2026";
        }
        float measured = text.measureText(shown);
        float x = bounds.centerX() - measured / 2f;
        canvas.drawText(shown, x, baseline(bounds.top, bounds.height(), text), text);
    }

    private Path cutPath(RectF bounds, float cut) {
        path.rewind();
        path.moveTo(bounds.left, bounds.top);
        path.lineTo(bounds.right, bounds.top);
        path.lineTo(bounds.right, bounds.bottom - cut);
        path.lineTo(bounds.right - cut, bounds.bottom);
        path.lineTo(bounds.left, bounds.bottom);
        path.close();
        return path;
    }

    private void drawHud(Canvas canvas, float left, float right, float y, float u, long now) {
        float progress = easeOut((now - startedAt - 700L) / 1100f);
        float width = (right - left) * progress;
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(withAlpha(Color.WHITE, 0.16f));
        canvas.drawRect(left, y, left + width, y + u, paint);
        paint.setColor(RED_HOT);
        canvas.drawRect(left, y, left + Math.min(width, 44f * u), y + 2f * u, paint);
    }

    private void drawHeader(Canvas canvas, float left, float right, float top, float u, long now) {
        float iconSize = 46f * u;
        RectF iconBounds = new RectF(left, top, left + iconSize, top + iconSize);
        if (icon != null) {
            paint.setShader(new RadialGradient(iconBounds.centerX(), iconBounds.centerY(), iconSize * 0.58f,
                    new int[]{withAlpha(Color.BLACK, 0.80f), withAlpha(Color.BLACK, 0.45f), Color.TRANSPARENT},
                    new float[]{0f, 0.66f, 1f}, Shader.TileMode.CLAMP));
            canvas.drawCircle(iconBounds.centerX(), iconBounds.centerY(), iconSize * 0.58f, paint);
            paint.setShader(null);
            canvas.drawBitmap(icon, null, iconBounds, bitmapPaint);
        }
        float brandX = iconBounds.right + 12f * u;
        drawLine(canvas, "[", brandX, top, iconSize, 26f, 500, RED_HOT, 0f, u, -1f);
        configure(text, 26f, 500, RED_HOT, 0f, u);
        brandX += text.measureText("[");
        drawGlitch(canvas, "PROTOTYPE", brandX, top, iconSize, 25f, 700, BONE, 0.8f, u, now);
        configure(text, 25f, 700, BONE, 0.8f, u);
        brandX += text.measureText("PROTOTYPE");
        drawLine(canvas, "]", brandX, top, iconSize, 26f, 500, RED_HOT, 0f, u, -1f);

        exitBounds.set(right - 96f * u, top + 8f * u, right, top + 38f * u);
        drawButton(canvas, ACTION_EXIT, exitBounds, "EXIT", BUTTON_GHOST, true, false, u, now, 16f);
    }

    private void drawGlitch(Canvas canvas, String value, float x, float top, float lineHeight, float size, int weight,
                            int color, float spacing, float u, long now) {
        long glitchTime = now - startedAt - 2400L;
        boolean active = glitchTime >= 0L && glitchTime % 6500L < 220L;
        float shift = active ? (((glitchTime % 6500L) / 55L) % 2L == 0L ? -5f : 4f) * u : 0f;
        if (active) {
            configure(text, size, weight, RED_HOT, spacing, u);
            canvas.drawText(value, x + shift * 1.4f, baseline(top, lineHeight, text), text);
        }
        configure(text, size, weight, color, spacing, u);
        canvas.drawText(value, x - shift * 0.4f, baseline(top, lineHeight, text), text);
    }

    private void drawTile(Canvas canvas, RectF bounds, String label, String value, boolean ok, float u, long now) {
        int color = ok ? OK : RED_HOT;
        Path shape = cutPath(bounds, 8f * u);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(withAlpha(color, 0.14f));
        canvas.drawPath(shape, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1f, u));
        paint.setColor(withAlpha(color, 0.25f));
        canvas.drawPath(shape, paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(color);
        canvas.drawRect(bounds.left, bounds.top, bounds.left + 3f * u, bounds.bottom, paint);

        float cx = bounds.left + 24f * u;
        float cy = bounds.centerY();
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(1.5f * u);
        paint.setColor(withAlpha(color, 0.65f));
        canvas.drawCircle(cx, cy, 9.5f * u, paint);
        paint.setStyle(Paint.Style.FILL);
        if (ok) {
            paint.setColor(color);
            paint.setStrokeWidth(2.2f * u);
            paint.setStrokeCap(Paint.Cap.ROUND);
            paint.setStyle(Paint.Style.STROKE);
            path.rewind();
            path.moveTo(cx - 5f * u, cy);
            path.lineTo(cx - 1f * u, cy + 4f * u);
            path.lineTo(cx + 6f * u, cy - 5f * u);
            canvas.drawPath(path, paint);
            paint.setStrokeCap(Paint.Cap.BUTT);
        } else {
            float blink = 0.25f + 0.75f * (0.5f + 0.5f * (float) Math.sin(now / 700.0 * Math.PI));
            paint.setColor(withAlpha(color, blink));
            canvas.drawCircle(cx, cy, 3.5f * u, paint);
            paint.setColor(withAlpha(color, blink * 0.25f));
            canvas.drawCircle(cx, cy, 6.5f * u, paint);
        }
        paint.setStyle(Paint.Style.FILL);
        float tx = bounds.left + 45f * u;
        float max = bounds.right - tx - 12f * u;
        drawLine(canvas, label, tx, bounds.top + 8f * u, 12f * u, 9.5f, 500, DIM, 2f, u, max);
        drawLine(canvas, value, tx, bounds.top + 22f * u, 20f * u, 15.5f, 600, color, 1.6f, u, max);
    }

    private void drawStatusLine(Canvas canvas, float x, float top, float width, float u, long now) {
        drawLine(canvas, ">", x, top, 18f * u, 12f, 600, RED_HOT, 0f, u, -1f);
        configure(text, 12f, 600, RED_HOT, 0f, u);
        float messageX = x + text.measureText(">") + 6f * u;
        String message = state.status == null ? "" : state.status.toUpperCase(Locale.ROOT);
        configure(text, 12f, 500, BONE, 1.4f, u);
        float caretWidth = 6f * u;
        drawLine(canvas, message, messageX, top, 18f * u, 12f, 500, BONE, 1.4f, u,
                Math.max(0f, width - (messageX - x) - 16f * u));
        float messageWidth = Math.min(text.measureText(message), Math.max(0f, width - (messageX - x) - 16f * u));
        float phase = (now % 1040L) / 1040f;
        float alpha = phase < 0.5f ? phase * 2f : (1f - phase) * 2f;
        paint.setColor(withAlpha(RED_HOT, alpha));
        canvas.drawRect(messageX + messageWidth + 4f * u, top + 3.5f * u,
                messageX + messageWidth + (4f * u) + caretWidth, top + 14.5f * u, paint);
    }

    private void drawButtonLabel(Canvas canvas, RectF bounds, String label, int color, float u) {
        // Keep the full action name visible on narrower phones instead of truncating it with an ellipsis.
        float size = 13.5f;
        float spacing = 2.2f;
        float available = Math.max(1f, bounds.width() - 48f * u);
        configure(text, size, 600, color, spacing, u);
        while (text.measureText(label) > available && size > 10.5f) {
            size = Math.max(10.5f, size - 0.5f);
            spacing = 2.2f * size / 13.5f;
            configure(text, size, 600, color, spacing, u);
        }
        drawCentered(canvas, label, bounds, size, 600, color, spacing, u, 24f * u);
    }

    private void drawButton(Canvas canvas, int action, RectF bounds, String label, int variant, boolean enabled,
                            boolean busy, float u, long now, float dt) {
        boolean live = enabled && (variant == BUTTON_PRIMARY || pressedAction == action
                || focusedAction == action || busy);
        if (variant != BUTTON_DONE) {
            float target = live ? 1f : 0f;
            float amount = 1f - (float) Math.exp(-Math.max(0f, dt) / 110f);
            buttonFill[action] += (target - buttonFill[action]) * amount;
        } else {
            buttonFill[action] = 0f;
        }

        int outer = canvas.save();
        if (pressedAction == action) canvas.scale(0.97f, 0.97f, bounds.centerX(), bounds.centerY());
        Path shape = cutPath(bounds, Math.min(9f * u, bounds.height() * 0.25f));
        int clipped = canvas.save();
        canvas.clipPath(shape);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(variant == BUTTON_DONE ? withAlpha(OK, 0.12f) : 0xb2080808);
        canvas.drawRect(bounds, paint);

        float fill = buttonFill[action];
        if (fill > 0.002f) {
            float edge = bounds.left + bounds.width() * fill;
            float jag = bounds.height() * 0.30f * fill;
            path.rewind();
            path.moveTo(bounds.left, bounds.top);
            path.lineTo(edge, bounds.top);
            path.lineTo(edge + jag, bounds.top + bounds.height() / 3f);
            path.lineTo(edge, bounds.top + bounds.height() * 2f / 3f);
            path.lineTo(edge, bounds.bottom);
            path.lineTo(bounds.left, bounds.bottom);
            path.close();
            paint.setShader(new LinearGradient(bounds.left, 0f, bounds.right, 0f,
                    new int[]{RED_DARK, RED, RED_HOT}, null, Shader.TileMode.CLAMP));
            canvas.drawPath(path, paint);
            paint.setShader(null);
            if (variant == BUTTON_PRIMARY) {
                float glow = 0.5f + 0.5f * (float) Math.sin(now / 1300.0 * Math.PI);
                paint.setColor(withAlpha(Color.WHITE, 0.07f * glow));
                canvas.drawPath(path, paint);
            }
        }

        if (splatAction == action) {
            float progress = clamp01((now - splatStartedAt) / 600f);
            if (progress < 1f) {
                float radius = progress * bounds.width() * 0.8f + 1f;
                paint.setShader(new RadialGradient(splatX, splatY, radius,
                        new int[]{withAlpha(0xffff4a3d, 1f - progress),
                                withAlpha(RED_HOT, (1f - progress) * 0.65f), Color.TRANSPARENT},
                        new float[]{0f, 0.35f, 1f}, Shader.TileMode.CLAMP));
                canvas.drawCircle(splatX, splatY, radius, paint);
                paint.setShader(null);
            }
        }
        if (busy) {
            float sweep = (now % 900L) / 900f;
            float barWidth = bounds.width() * 0.35f;
            float x = bounds.left - barWidth + sweep * (bounds.width() + barWidth);
            paint.setColor(Color.WHITE);
            canvas.drawRect(x, bounds.bottom - 3f * u, x + barWidth, bounds.bottom, paint);
        }
        canvas.restoreToCount(clipped);

        int borderColor;
        if (variant == BUTTON_DONE) borderColor = withAlpha(OK, 0.55f);
        else if (!enabled) borderColor = withAlpha(Color.WHITE, 0.12f);
        else if (focusedAction == action) borderColor = withAlpha(BONE, 0.85f);
        else if (variant == BUTTON_PRIMARY) borderColor = withAlpha(RED_HOT, 0.75f);
        else borderColor = withAlpha(Color.WHITE, 0.24f);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1f, u));
        paint.setColor(borderColor);
        canvas.drawPath(cutPath(bounds, Math.min(9f * u, bounds.height() * 0.25f)), paint);
        paint.setStyle(Paint.Style.FILL);

        int textColor = variant == BUTTON_DONE ? OK : (!enabled ? DIM : BONE);
        int bracketColor = variant == BUTTON_DONE ? OK : (live ? BONE : withAlpha(RED_HOT, 0.5f));
        float bracketInset = (live ? 15f : 9f) * u;
        drawCentered(canvas, "[", new RectF(bounds.left + bracketInset - 8f * u, bounds.top,
                bounds.left + bracketInset + 8f * u, bounds.bottom), 22f, 400, bracketColor, 0f, u, 0f);
        drawButtonLabel(canvas, bounds, label, textColor, u);
        drawCentered(canvas, "]", new RectF(bounds.right - bracketInset - 8f * u, bounds.top,
                bounds.right - bracketInset + 8f * u, bounds.bottom), 22f, 400, bracketColor, 0f, u, 0f);
        canvas.restoreToCount(outer);
    }

    private StaticLayout wrapped(String value, float size, int weight, int color, float spacing, float u, int width) {
        TextPaint layoutPaint = newText(size, weight, color, spacing, u);
        return StaticLayout.Builder.obtain(value == null ? "" : value, 0, value == null ? 0 : value.length(),
                        layoutPaint, Math.max(1, width))
                .setAlignment(Layout.Alignment.ALIGN_NORMAL)
                .setIncludePad(false)
                .setLineSpacing(0f, 1f)
                .setMaxLines(4)
                .build();
    }

    private void ensurePanelLayouts(float width, float u) {
        if (!layoutDirty && Math.abs(panelLayoutWidth - width) < 0.5f
                && Math.abs(panelLayoutUnit - u) < 0.01f) return;
        panelLayoutWidth = width;
        panelLayoutUnit = u;
        int textWidth = Math.max(1, Math.round(width - 16f * u - 14f * u - 38f * u));
        primaryPathLayout = wrapped(state.primaryPath, 13f, 600, 0xfff1ece3, 0.2f, u, textWidth);
        String accessNote = state.fileAccess ? "Any file manager. File access granted"
                : "Any file manager. Needs file access";
        if (state.alternateSharedPath != null && !state.alternateSharedPath.isEmpty()) {
            accessNote += "\nAlso accepted: " + state.alternateSharedPath;
        }
        primaryNoteLayout = wrapped(accessNote, 10.5f, 500, DIM, 0.6f, u, textWidth);
        secondaryPathLayout = wrapped(state.secondaryPath, 13f, 600, 0xfff1ece3, 0.2f, u, textWidth);
        secondaryNoteLayout = wrapped("Android 11 or above: copy via PC/USB\n"
                + "Android 10 or below: use a file manager", 10.5f, 500, DIM, 0.6f, u, textWidth);
        layoutDirty = false;
    }

    private float entryHeight(StaticLayout pathLayout, StaticLayout noteLayout, float u) {
        return Math.max(31f * u, 11f * u + u + pathLayout.getHeight() + u + noteLayout.getHeight());
    }

    private float panelHeight(float u) {
        return 10f * u + 13f * u + 8f * u + entryHeight(primaryPathLayout, primaryNoteLayout, u)
                + 8f * u + entryHeight(secondaryPathLayout, secondaryNoteLayout, u) + 10f * u;
    }

    private void drawStatic(Canvas canvas, StaticLayout layout, float x, float y) {
        int save = canvas.save();
        canvas.translate(x, y);
        layout.draw(canvas);
        canvas.restoreToCount(save);
    }

    private void drawPathEntry(Canvas canvas, float left, float top, float width, String number, String tag,
                               StaticLayout pathLayout, StaticLayout noteLayout, float u) {
        float textX = left + 38f * u;
        drawLine(canvas, number, left, top, 31f * u, 26f, 700, withAlpha(Color.WHITE, 0.32f), 0f, u, 32f * u);
        drawLine(canvas, tag, textX, top, 11f * u, 9f, 500, RED_HOT, 2.4f, u, width - 38f * u);
        float pathY = top + 12f * u;
        drawStatic(canvas, pathLayout, textX, pathY);
        drawStatic(canvas, noteLayout, textX, pathY + pathLayout.getHeight() + u);
    }

    private float drawPathPanel(Canvas canvas, float left, float bottom, float width, float u, long now) {
        ensurePanelLayouts(width, u);
        float height = panelHeight(u);
        RectF bounds = new RectF(left, bottom - height, left + width, bottom);
        Path shape = cutPath(bounds, 12f * u);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xc0050505);
        canvas.drawPath(shape, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1f, u));
        paint.setColor(withAlpha(Color.WHITE, 0.16f));
        canvas.drawPath(shape, paint);
        paint.setStyle(Paint.Style.FILL);
        float edgeAlpha = 0.35f + 0.65f * (0.5f + 0.5f * (float) Math.sin(now / 1600.0 * Math.PI));
        paint.setColor(withAlpha(RED_HOT, edgeAlpha));
        canvas.drawRect(left, bounds.top, left + 3f * u, bottom, paint);

        float contentX = left + 16f * u;
        float contentWidth = width - 30f * u;
        float y = bounds.top + 10f * u;
        drawLine(canvas, "PLACE YOUR PC GAME FILES IN", contentX, y, 13f * u, 10.5f, 500,
                0xffd5cfc6, 2.4f, u, contentWidth);
        y += 21f * u;
        drawPathEntry(canvas, contentX, y, contentWidth, "01", "RECOMMENDED",
                primaryPathLayout, primaryNoteLayout, u);
        y += entryHeight(primaryPathLayout, primaryNoteLayout, u) + 8f * u;
        drawPathEntry(canvas, contentX, y, contentWidth, "02", "APP PRIVATE",
                secondaryPathLayout, secondaryNoteLayout, u);
        return height;
    }

    private float chipsHeight(float width, float u) {
        float x = 0f;
        float rows = 1f;
        for (String requirement : REQUIREMENTS) {
            configure(text, 10.5f, 500, 0xffcfc9c0, 0.8f, u);
            float item = text.measureText(requirement) + 16f * u;
            if (x > 0f && x + item > width) {
                rows++;
                x = 0f;
            }
            x += item + 5f * u;
        }
        return rows * 18f * u + (rows - 1f) * 5f * u;
    }

    private void drawChips(Canvas canvas, float left, float top, float width, float u) {
        float x = left;
        float y = top;
        for (String requirement : REQUIREMENTS) {
            configure(text, 10.5f, 500, 0xffcfc9c0, 0.8f, u);
            float itemWidth = text.measureText(requirement) + 16f * u;
            if (x > left && x + itemWidth > left + width) {
                x = left;
                y += 23f * u;
            }
            RectF bounds = new RectF(x, y, x + itemWidth, y + 18f * u);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0x66000000);
            canvas.drawRect(bounds, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(1f, u * 0.7f));
            paint.setColor(withAlpha(Color.WHITE, 0.20f));
            canvas.drawRect(bounds, paint);
            paint.setStyle(Paint.Style.FILL);
            drawCentered(canvas, requirement, bounds, 10.5f, 500, 0xffcfc9c0, 0.8f, u, 7f * u);
            x += itemWidth + 5f * u;
        }
    }

    private void drawLandscape(Canvas canvas, long now, float dt) {
        float u = unit(false);
        float left = insetLeft + 26f * u;
        float right = getWidth() - insetRight - 26f * u;
        float top = insetTop + 12f * u;
        float bottom = getHeight() - insetBottom - 12f * u;
        drawHeader(canvas, left, right, top, u, now);
        float hudY = top + 50f * u;
        drawHud(canvas, left, right, hudY, u, now);
        float contentTop = hudY + 10f * u;
        float contentBottom = bottom;
        float gap = 26f * u;
        float columns = Math.max(1f, right - left - gap);
        float leftWidth = columns * 1.08f / 2f;
        float rightWidth = columns * 0.92f / 2f;
        float rightLeft = left + leftWidth + gap;

        float headline = getHeight() / u < 300f ? 26f : (getHeight() / u < 340f ? 31f : 38f);
        float groupHeight = 46f * u + 10f * u + headline * 2f * u + 10f * u + 18f * u
                + 10f * u + 42f * u;
        float groupTop = contentTop + Math.max(0f, (contentBottom - contentTop - groupHeight) / 2f);
        float tileWidth = (leftWidth - 8f * u) / 2f;
        drawTile(canvas, new RectF(left, groupTop, left + tileWidth, groupTop + 46f * u),
                "STORAGE ACCESS", state.fileAccess ? "GRANTED" : "REQUIRED", state.fileAccess, u, now);
        drawTile(canvas, new RectF(left + tileWidth + 8f * u, groupTop, left + leftWidth, groupTop + 46f * u),
                "GAME DATA", state.gameFound ? "DETECTED" : "NOT FOUND", state.gameFound, u, now);

        float headlineTop = groupTop + 56f * u;
        drawGlitch(canvas, state.gameFound ? "GAME DATA" : "GAME FILES", left, headlineTop,
                headline * u, headline, 700, BONE, 0.2f, u, now);
        drawLine(canvas, state.gameFound ? "FOUND" : "NOT FOUND", left, headlineTop + headline * u,
                headline * u, headline, 700, state.gameFound ? OK : RED_HOT, 0.2f, u, leftWidth);
        float statusTop = headlineTop + headline * 2f * u + 10f * u;
        drawStatusLine(canvas, left, statusTop, leftWidth, u, now);
        float buttonsTop = statusTop + 28f * u;
        float buttonWidth = (leftWidth - 8f * u) / 2f;
        allowBounds.set(left, buttonsTop, left + buttonWidth, buttonsTop + 42f * u);
        checkBounds.set(left + buttonWidth + 8f * u, buttonsTop, left + leftWidth, buttonsTop + 42f * u);
        drawButton(canvas, ACTION_ALLOW, allowBounds, state.fileAccess ? "ACCESS ALLOWED" : "ALLOW FILE ACCESS",
                state.fileAccess ? BUTTON_DONE : BUTTON_PRIMARY, !state.fileAccess, false, u, now, dt);
        drawButton(canvas, ACTION_CHECK, checkBounds, state.checking ? "CHECKING" : "CHECK AGAIN",
                state.fileAccess && !state.gameFound ? BUTTON_PRIMARY : BUTTON_NORMAL,
                !state.checking, state.checking, u, now, dt);

        float chipsHeight = chipsHeight(rightWidth, u);
        float chipsTop = contentBottom - chipsHeight;
        drawPathPanel(canvas, rightLeft, chipsTop - 8f * u, rightWidth, u, now);
        drawChips(canvas, rightLeft, chipsTop, rightWidth, u);
        maxScrollY = 0f;
        scrollY = 0f;
    }

    private void drawPortrait(Canvas canvas, long now, float dt) {
        float u = unit(true);
        float left = insetLeft + 18f * u;
        float right = getWidth() - insetRight - 18f * u;
        float width = Math.max(1f, right - left);
        ensurePanelLayouts(width, u);
        float headline = width / u < 380f ? 30f : 36f;
        float contentHeight = 46f * u + 14f * u + 2f * u + 14f * u + 46f * u + 14f * u
                + headline * 2f * u + 14f * u + 18f * u + 14f * u + 42f * u + 14f * u
                + panelHeight(u) + 14f * u + chipsHeight(width, u);
        float available = getHeight() - insetTop - insetBottom - 32f * u;
        maxScrollY = Math.max(0f, contentHeight - available);
        scrollY = Math.max(0f, Math.min(scrollY, maxScrollY));
        float y = insetTop + 16f * u - scrollY;

        drawHeader(canvas, left, right, y, u, now);
        y += 60f * u;
        drawHud(canvas, left, right, y, u, now);
        y += 16f * u;
        float tileWidth = (width - 8f * u) / 2f;
        drawTile(canvas, new RectF(left, y, left + tileWidth, y + 46f * u),
                "STORAGE ACCESS", state.fileAccess ? "GRANTED" : "REQUIRED", state.fileAccess, u, now);
        drawTile(canvas, new RectF(left + tileWidth + 8f * u, y, right, y + 46f * u),
                "GAME DATA", state.gameFound ? "DETECTED" : "NOT FOUND", state.gameFound, u, now);
        y += 60f * u;
        drawGlitch(canvas, state.gameFound ? "GAME DATA" : "GAME FILES", left, y,
                headline * u, headline, 700, BONE, 0.2f, u, now);
        drawLine(canvas, state.gameFound ? "FOUND" : "NOT FOUND", left, y + headline * u,
                headline * u, headline, 700, state.gameFound ? OK : RED_HOT, 0.2f, u, width);
        y += headline * 2f * u + 14f * u;
        drawStatusLine(canvas, left, y, width, u, now);
        y += 32f * u;
        float buttonWidth = (width - 8f * u) / 2f;
        allowBounds.set(left, y, left + buttonWidth, y + 42f * u);
        checkBounds.set(left + buttonWidth + 8f * u, y, right, y + 42f * u);
        drawButton(canvas, ACTION_ALLOW, allowBounds, state.fileAccess ? "ACCESS ALLOWED" : "ALLOW FILE ACCESS",
                state.fileAccess ? BUTTON_DONE : BUTTON_PRIMARY, !state.fileAccess, false, u, now, dt);
        drawButton(canvas, ACTION_CHECK, checkBounds, state.checking ? "CHECKING" : "CHECK AGAIN",
                state.fileAccess && !state.gameFound ? BUTTON_PRIMARY : BUTTON_NORMAL,
                !state.checking, state.checking, u, now, dt);
        y += 56f * u;
        float panelHeight = panelHeight(u);
        drawPathPanel(canvas, left, y + panelHeight, width, u, now);
        y += panelHeight + 14f * u;
        drawChips(canvas, left, y, width, u);
    }

    private void drawBootCurtain(Canvas canvas, long now) {
        float progress = clamp01((now - startedAt - 150L) / 1100f);
        if (progress >= 1f) return;
        float eased = progress < 0.5f ? 4f * progress * progress * progress
                : 1f - (float) Math.pow(-2f * progress + 2f, 3) / 2f;
        float x = getWidth() * eased * 1.03f;
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(Color.BLACK);
        canvas.drawRect(x, 0f, getWidth() + 2f, getHeight(), paint);
        paint.setShader(new LinearGradient(x - 90f, 0f, x, 0f,
                new int[]{Color.TRANSPARENT, withAlpha(RED_HOT, 0.60f)}, null, Shader.TileMode.CLAMP));
        canvas.drawRect(x - 90f, 0f, x, getHeight(), paint);
        paint.setShader(null);
        paint.setColor(RED_HOT);
        canvas.drawRect(x - 3f, 0f, x, getHeight(), paint);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        long now = SystemClock.uptimeMillis();
        float dt = lastFrameAt == 0L ? 16f : Math.min(50f, now - lastFrameAt);
        lastFrameAt = now;
        drawBackdrop(canvas, now);
        if (getWidth() > 0 && getHeight() > 0) {
            if (getHeight() > getWidth()) drawPortrait(canvas, now, dt);
            else drawLandscape(canvas, now, dt);
            drawBootCurtain(canvas, now);
        }
        if (getWindowVisibility() == VISIBLE && isAttachedToWindow()) postInvalidateOnAnimation();
    }

    private int actionAt(float x, float y) {
        if (exitBounds.contains(x, y)) return ACTION_EXIT;
        if (allowBounds.contains(x, y) && !state.fileAccess) return ACTION_ALLOW;
        if (checkBounds.contains(x, y) && !state.checking) return ACTION_CHECK;
        return ACTION_NONE;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        float x = event.getX();
        float y = event.getY();
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                requestFocus();
                focusedAction = ACTION_NONE;
                pressedAction = actionAt(x, y);
                splatAction = pressedAction;
                splatX = x;
                splatY = y;
                splatStartedAt = SystemClock.uptimeMillis();
                scrolling = false;
                touchStartY = y;
                lastTouchY = y;
                if (pressedAction != ACTION_NONE) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
                getParent().requestDisallowInterceptTouchEvent(true);
                invalidate();
                return true;
            case MotionEvent.ACTION_MOVE:
                if (getHeight() > getWidth() && !scrolling
                        && Math.abs(y - touchStartY) > touchSlop) {
                    scrolling = true;
                    pressedAction = ACTION_NONE;
                }
                if (scrolling) {
                    scrollY = Math.max(0f, Math.min(maxScrollY, scrollY + lastTouchY - y));
                    lastTouchY = y;
                } else if (pressedAction != ACTION_NONE && actionAt(x, y) != pressedAction) {
                    pressedAction = ACTION_NONE;
                }
                invalidate();
                return true;
            case MotionEvent.ACTION_UP:
                int action = pressedAction;
                pressedAction = ACTION_NONE;
                if (!scrolling && action != ACTION_NONE && actionAt(x, y) == action) activate(action);
                scrolling = false;
                getParent().requestDisallowInterceptTouchEvent(false);
                invalidate();
                return true;
            case MotionEvent.ACTION_CANCEL:
                pressedAction = ACTION_NONE;
                scrolling = false;
                getParent().requestDisallowInterceptTouchEvent(false);
                invalidate();
                return true;
            default:
                return true;
        }
    }

    private void activate(int action) {
        performClick();
        if (action == ACTION_ALLOW && !state.fileAccess) listener.onAllowFileAccess();
        else if (action == ACTION_CHECK && !state.checking) listener.onCheckAgain();
        else if (action == ACTION_EXIT) listener.onExit();
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    private int nextFocusable(int current, int direction) {
        int[] order = {ACTION_ALLOW, ACTION_CHECK, ACTION_EXIT};
        int index = -1;
        for (int i = 0; i < order.length; i++) if (order[i] == current) index = i;
        for (int step = 1; step <= order.length; step++) {
            int candidate = order[(index + direction * step + order.length * 2) % order.length];
            if (candidate == ACTION_ALLOW && state.fileAccess) continue;
            if (candidate == ACTION_CHECK && state.checking) continue;
            return candidate;
        }
        return ACTION_NONE;
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT || keyCode == KeyEvent.KEYCODE_DPAD_UP
                || keyCode == KeyEvent.KEYCODE_DPAD_RIGHT || keyCode == KeyEvent.KEYCODE_DPAD_DOWN
                || keyCode == KeyEvent.KEYCODE_TAB) {
            int direction = keyCode == KeyEvent.KEYCODE_DPAD_LEFT || keyCode == KeyEvent.KEYCODE_DPAD_UP ? -1 : 1;
            focusedAction = nextFocusable(focusedAction, direction);
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_ENTER || keyCode == KeyEvent.KEYCODE_DPAD_CENTER
                || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER || keyCode == KeyEvent.KEYCODE_BUTTON_A) {
            if (focusedAction != ACTION_NONE) {
                activate(focusedAction);
                return true;
            }
        }
        return super.onKeyDown(keyCode, event);
    }
}
