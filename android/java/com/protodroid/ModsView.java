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
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Build;
import android.os.SystemClock;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.util.LruCache;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.VelocityTracker;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.WindowInsets;
import android.widget.OverScroller;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Prototype-style local mod library. Engine and graphics options stay in the launcher. */
final class ModsView extends View {
    interface Listener {
        void onClose();
        void onImport();
        void onSetEnabled(ModManifest mod, boolean enabled);
        void onConfigure(ModManifest mod);
        void onRemove(ModManifest mod);
    }

    private static final int RED = 0xffc9141a;
    private static final int RED_DARK = 0xff72121a;
    private static final int RED_HOT = 0xffff463f;
    private static final int BONE = 0xfff0ece5;
    private static final int DIM = 0xffbdb7b0;
    private static final int MUTED = 0xff98929b;
    private static final int OK = 0xff9ae788;
    private static final int AMBER = 0xffedc078;
    private static final int PANEL = 0xe613151b;
    private static final int PANEL_ALT = 0xf01a1b22;
    private static final float MIN_TOUCH_DP = 48f;

    private static final int ACTION_NONE = 0;
    private static final int ACTION_BACK = 1;
    private static final int ACTION_IMPORT = 2;
    private static final int ACTION_TOGGLE = 1000;
    private static final int ACTION_REMOVE = 2000;
    private static final int ACTION_DETAILS = 3000;
    private static final int ACTION_CONFIGURE = 4000;
    private static final int MAX_ACTION = 8192;

    private static final int BUTTON_NORMAL = 0;
    private static final int BUTTON_PRIMARY = 1;
    private static final int BUTTON_GHOST = 2;

    private final Listener listener;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bitmapPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Path path = new Path();
    private final Typeface medium;
    private final Typeface semibold;
    private final Typeface bold;
    private final Bitmap backdrop;
    private final Bitmap icon;
    private final float density;
    private final float fontScale;
    private final int touchSlop;
    private final OverScroller scroller;
    private final RectF[] actionBounds = new RectF[MAX_ACTION];
    private int[] usedActions = new int[64];
    private int usedActionCount;
    private final ArrayList<RectF> rectPool = new ArrayList<>();
    private int rectPoolUsed;
    private final Paint.FontMetrics fontMetrics = new Paint.FontMetrics();
    private final Rect coverSource = new Rect();
    private final ArrayList<ModManifest> mods = new ArrayList<>();
    private final Map<String, StaticLayout> paragraphCache = new HashMap<>();
    private final Set<String> loadingCovers = new HashSet<>();
    private final ExecutorService coverExecutor = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "mod-cover-decode");
        thread.setDaemon(true);
        return thread;
    });
    private final LruCache<String, Bitmap> covers = new LruCache<String, Bitmap>(12 * 1024) {
        @Override protected int sizeOf(String key, Bitmap bitmap) {
            return Math.max(1, bitmap.getByteCount() / 1024);
        }
    };
    private final boolean motion;
    private final long startedAt = SystemClock.uptimeMillis();

    private int insetLeft, insetTop, insetRight, insetBottom;
    private int pressedAction = ACTION_NONE;
    private int focusedAction = ACTION_NONE;
    private float lastTouchY;
    private float scrollY;
    private float maxScrollY;
    private float lastUnit = 1f;
    private float contentTop;
    private float contentBottom;
    private float contentShiftX;
    private boolean scrolling;
    private boolean busy;
    private boolean closed;
    private long tabChangedAt;
    private String status = "Imports start disabled. Review a mod, enable it, then press Play.";
    private String modsPath = "";
    private String detailsId = "";
    private String removeArmedId = "";
    private long removeArmedAt;
    private String pluginArmedId = "";
    private long pluginArmedAt;
    private VelocityTracker velocityTracker;
    private Shader backgroundShade;
    private int backgroundWidth = -1;
    private int backgroundHeight = -1;

    ModsView(Context context, Listener listener) {
        super(context);
        this.listener = listener;
        density = getResources().getDisplayMetrics().density;
        fontScale = getResources().getConfiguration().fontScale;
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        scroller = new OverScroller(context);
        motion = LauncherMotion.enabled();
        tabChangedAt = startedAt;
        AssetManager assets = context.getAssets();
        medium = loadFont(assets, "setup/fonts/barlow_condensed_medium.ttf");
        semibold = loadFont(assets, "setup/fonts/barlow_condensed_semibold.ttf");
        bold = loadFont(assets, "setup/fonts/barlow_condensed_bold.ttf");
        backdrop = loadBitmap(assets, "setup/images/bg_3.jpg");
        icon = loadBitmap(assets, "setup/images/protodroid_icon.png");
        setBackgroundColor(Color.BLACK);
        setClickable(true);
        setFocusable(true);
        reload();
    }

    private static Typeface loadFont(AssetManager assets, String name) {
        try {
            return Typeface.createFromAsset(assets, name);
        } catch (RuntimeException ignored) {
            return Typeface.DEFAULT;
        }
    }

    private static Bitmap loadBitmap(AssetManager assets, String name) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        options.inPreferredConfig = Bitmap.Config.RGB_565;
        try (InputStream input = assets.open(name)) {
            return BitmapFactory.decodeStream(input, null, options);
        } catch (IOException | RuntimeException ignored) {
            return null;
        }
    }

    void close() {
        closed = true;
        coverExecutor.shutdownNow();
        recycleVelocityTracker();
    }

    void reload() {
        if (closed) return;
        mods.clear();
        mods.addAll(ModManager.list(getContext()));
        modsPath = ModManager.modsDir(getContext()).getAbsolutePath();
        focusedAction = ACTION_NONE;
        removeArmedId = "";
        pluginArmedId = "";
        for (ModManifest mod : mods) requestCover(mod);
        updateContentDescription();
        invalidate();
    }

    void setBusy(boolean value, String message) {
        busy = value;
        if (message != null) status = message;
        updateContentDescription();
        invalidate();
    }

    void setStatus(String message, boolean announce) {
        status = message == null ? "" : message;
        updateContentDescription();
        if (announce && !status.isEmpty()) announceForAccessibility(status);
        invalidate();
    }

    boolean handleBack() {
        if (!removeArmedId.isEmpty() || !pluginArmedId.isEmpty()) {
            removeArmedId = "";
            pluginArmedId = "";
            setStatus("Confirmation cancelled.", false);
            return true;
        }
        return false;
    }

    private void updateContentDescription() {
        int active = ModManager.activeCount(mods);
        StringBuilder description = new StringBuilder("Prototype mod library. ")
                .append(mods.size()).append(" installed, ").append(active).append(" active next Play. ")
                .append("Mods view. ")
                .append(status);
        if (busy) description.append(" Mod operation in progress.");
        setContentDescription(description.toString());
    }

    private void requestCover(ModManifest mod) {
        final File file = mod.coverFile();
        if (file == null || covers.get(file.getAbsolutePath()) != null
                || !loadingCovers.add(file.getAbsolutePath())) return;
        coverExecutor.execute(() -> {
            Bitmap bitmap = decodeCover(file);
            post(() -> {
                loadingCovers.remove(file.getAbsolutePath());
                if (closed) return;
                if (bitmap != null) covers.put(file.getAbsolutePath(), bitmap);
                invalidate();
            });
        });
    }

    private static Bitmap decodeCover(File file) {
        if (!file.isFile() || file.length() > 24 * 1024 * 1024) return null;
        try {
            BitmapFactory.Options bounds = new BitmapFactory.Options();
            bounds.inJustDecodeBounds = true;
            BitmapFactory.decodeFile(file.getAbsolutePath(), bounds);
            if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null;
            int sample = 1;
            while (bounds.outWidth / sample > 512 || bounds.outHeight / sample > 512) sample *= 2;
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inSampleSize = sample;
            options.inPreferredConfig = Bitmap.Config.RGB_565;
            return BitmapFactory.decodeFile(file.getAbsolutePath(), options);
        } catch (RuntimeException | OutOfMemoryError ignored) {
            return null;
        }
    }

    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {
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

    @Override protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        super.onSizeChanged(width, height, oldWidth, oldHeight);
        paragraphCache.clear();
        backgroundShade = null;
    }

    private float unit() {
        if (getHeight() > getWidth()) return density;
        float usableDp = Math.max(1f, getHeight() - insetTop - insetBottom) / density;
        return density * Math.max(0.8f, Math.min(2.2f, usableDp / 360f));
    }

    private float touch(float u) {
        return Math.max(MIN_TOUCH_DP * density, MIN_TOUCH_DP * u);
    }

    private Typeface font(int weight) {
        if (weight >= 700) return bold;
        if (weight >= 600) return semibold;
        return medium;
    }

    private TextPaint configure(TextPaint target, float size, int weight, int color, float u) {
        target.setTypeface(font(weight));
        target.setTextSize(size * u * fontScale);
        target.setColor(color);
        target.setLetterSpacing(0.015f);
        target.setStyle(Paint.Style.FILL);
        return target;
    }

    private float baseline(float top, float lineHeight, TextPaint paint) {
        paint.getFontMetrics(fontMetrics);
        return top + (lineHeight - (fontMetrics.descent - fontMetrics.ascent)) / 2f - fontMetrics.ascent;
    }

    private String ellipsized(String value, float maxWidth) {
        if (value == null) return "";
        if (maxWidth <= 0 || text.measureText(value) <= maxWidth) return value;
        float ellipsis = text.measureText("…");
        int count = text.breakText(value, true, Math.max(0, maxWidth - ellipsis), null);
        return value.substring(0, Math.max(0, count)) + "…";
    }

    private void drawLine(Canvas canvas, String value, float x, float top, float lineHeight, float size,
                          int weight, int color, float u, float maxWidth) {
        if (value == null) return;
        configure(text, size, weight, color, u);
        canvas.drawText(ellipsized(value, maxWidth), x, baseline(top, lineHeight, text), text);
    }

    private void drawRight(Canvas canvas, String value, float right, float top, float lineHeight, float size,
                           int weight, int color, float u, float maxWidth) {
        configure(text, size, weight, color, u);
        String shown = ellipsized(value, maxWidth);
        canvas.drawText(shown, right - text.measureText(shown), baseline(top, lineHeight, text), text);
    }

    private void drawCentered(Canvas canvas, String value, RectF bounds, float size, int weight, int color, float u) {
        configure(text, size, weight, color, u);
        String shown = ellipsized(value, Math.max(0f, bounds.width() - 14f * u));
        canvas.drawText(shown, bounds.centerX() - text.measureText(shown) / 2f,
                baseline(bounds.top, bounds.height(), text), text);
    }

    private StaticLayout paragraph(String value, float width, float size, int weight, int color, float u, int maxLines) {
        int pixels = Math.max(1, Math.round(width));
        String key = value + "\n" + pixels + "\n" + size + "\n" + weight + "\n" + color + "\n" + u
                + "\n" + maxLines;
        StaticLayout cached = paragraphCache.get(key);
        if (cached != null) return cached;
        TextPaint layoutPaint = configure(new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG),
                size, weight, color, u);
        StaticLayout.Builder builder = StaticLayout.Builder.obtain(value, 0, value.length(), layoutPaint, pixels)
                .setAlignment(Layout.Alignment.ALIGN_NORMAL).setIncludePad(false);
        if (maxLines > 0) builder.setMaxLines(maxLines);
        StaticLayout layout = builder.build();
        if (paragraphCache.size() > 220) paragraphCache.clear();
        paragraphCache.put(key, layout);
        return layout;
    }

    private float drawParagraph(Canvas canvas, String value, float x, float top, float width, float size,
                                int weight, int color, float u, int maxLines) {
        StaticLayout layout = paragraph(value, width, size, weight, color, u, maxLines);
        canvas.save();
        canvas.translate(x, top);
        layout.draw(canvas);
        canvas.restore();
        return layout.getHeight();
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

    private void panel(Canvas canvas, RectF bounds, int fill, int edge, float u) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(fill);
        canvas.drawPath(cutPath(bounds, Math.min(10f * u, bounds.height() * 0.2f)), paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1f, u * 0.7f));
        paint.setColor(edge);
        canvas.drawPath(cutPath(bounds, Math.min(10f * u, bounds.height() * 0.2f)), paint);
        paint.setStyle(Paint.Style.FILL);
    }

    private RectF rect(float left, float top, float right, float bottom) {
        if (rectPoolUsed == rectPool.size()) rectPool.add(new RectF());
        RectF result = rectPool.get(rectPoolUsed++);
        result.set(left, top, right, bottom);
        return result;
    }

    private void bind(int action, RectF bounds) {
        if (action <= ACTION_NONE || action >= MAX_ACTION || bounds.isEmpty()) return;
        if (actionBounds[action] == null) actionBounds[action] = new RectF();
        actionBounds[action].set(bounds);
        if (contentShiftX != 0f) actionBounds[action].offset(contentShiftX, 0f);
        if (usedActionCount == usedActions.length) usedActions = Arrays.copyOf(usedActions, usedActions.length * 2);
        usedActions[usedActionCount++] = action;
    }

    private int actionAt(float x, float y) {
        for (int i = usedActionCount - 1; i >= 0; --i) {
            int action = usedActions[i];
            RectF bounds = actionBounds[action];
            if (bounds != null && bounds.contains(x, y)) return action;
        }
        return ACTION_NONE;
    }

    private void button(Canvas canvas, int action, RectF bounds, String label, int variant, boolean enabled, float u) {
        boolean pressed = pressedAction == action && enabled;
        boolean focused = focusedAction == action && enabled;
        int edge = focused ? BONE : variant == BUTTON_PRIMARY ? RED_HOT : pressed ? RED_HOT : 0xff51414a;
        int fill = variant == BUTTON_PRIMARY ? (pressed ? 0xffa91822 : RED_DARK)
                : variant == BUTTON_GHOST ? 0x6510141b : pressed ? 0xff45232c : 0xff202027;
        if (!enabled) {
            edge = 0xff38353d;
            fill = 0xff17181d;
        }
        canvas.save();
        if (pressed && motion) canvas.scale(0.98f, 0.98f, bounds.centerX(), bounds.centerY());
        panel(canvas, bounds, fill, edge, u);
        if (pressed && motion) {
            paint.setColor(0x35ffffff);
            canvas.drawRect(bounds.left, bounds.bottom - 3f * u, bounds.right, bounds.bottom, paint);
        }
        canvas.restore();
        if (enabled) bind(action, bounds);
        drawCentered(canvas, label, bounds, 12f, 700, enabled ? BONE : MUTED, u);
    }

    private void section(Canvas canvas, String title, float left, float right, float top, float u) {
        drawLine(canvas, title, left, top, 23f * u, 12.5f, 700, RED_HOT, u, right - left);
        paint.setColor(0xff543039);
        canvas.drawRect(left, top + 27f * u, right, top + 28f * u, paint);
        paint.setColor(RED);
        canvas.drawRect(left, top + 27f * u, left + 46f * u, top + 29f * u, paint);
    }

    private void drawBackground(Canvas canvas, long now) {
        int width = getWidth(), height = getHeight();
        canvas.drawColor(Color.BLACK);
        if (backdrop != null && !backdrop.isRecycled()) {
            float drift = motion ? 0.015f * (float) Math.sin((now - startedAt) / 9000.0) : 0f;
            float scale = Math.max(width / (float) backdrop.getWidth(), height / (float) backdrop.getHeight())
                    * (1.035f + drift);
            RectF destination = rect(0, 0, backdrop.getWidth() * scale, backdrop.getHeight() * scale);
            destination.offset((width - destination.width()) / 2f, (height - destination.height()) / 2f);
            bitmapPaint.setAlpha(190);
            canvas.drawBitmap(backdrop, null, destination, bitmapPaint);
            bitmapPaint.setAlpha(255);
        }
        if (backgroundShade == null || backgroundWidth != width || backgroundHeight != height) {
            backgroundWidth = width;
            backgroundHeight = height;
            backgroundShade = new LinearGradient(0, 0, width, height,
                    new int[]{0xf208090d, 0xe6111018, 0xf2230e16}, null, Shader.TileMode.CLAMP);
        }
        paint.setShader(backgroundShade);
        canvas.drawRect(0, 0, width, height, paint);
        paint.setShader(null);
        paint.setColor(0x14c9141a);
        paint.setStrokeWidth(1f);
        for (int x = -height; x < width + height; x += Math.max(32, width / 12)) {
            canvas.drawLine(x, 0, x - height, height, paint);
        }
        paint.setColor(RED);
        canvas.drawRect(0, 0, width, Math.max(2f, density * 0.6f), paint);
    }

    private void drawHeader(Canvas canvas, float left, float right, float top, float u) {
        float iconSize = 38f * u;
        if (icon != null) canvas.drawBitmap(icon, null, rect(left, top, left + iconSize, top + iconSize), bitmapPaint);
        float brandLeft = left + iconSize + 8f * u;
        drawLine(canvas, "[", brandLeft, top, 38f * u, 25f, 500, RED_HOT, u, -1);
        drawLine(canvas, "PROTOTYPE", brandLeft + 13f * u, top, 38f * u, 23f, 700, BONE, u, -1);
        RectF back = rect(right - 94f * u, top, right, top + touch(u));
        RectF importButton = rect(back.left - 156f * u, top, back.left - 8f * u, top + touch(u));
        button(canvas, ACTION_IMPORT, importButton, busy ? "IMPORTING…" : "+ IMPORT", BUTTON_PRIMARY, !busy, u);
        button(canvas, ACTION_BACK, back, "BACK", BUTTON_GHOST, true, u);
        drawLine(canvas, "MOD LIBRARY", left, top + 52f * u, 32f * u, 26f, 700, BONE, u, right - left);
        String subtitle = mods.size() + " installed  /  " + ModManager.activeCount(mods) + " active next Play";
        drawLine(canvas, subtitle, left, top + 86f * u, 20f * u, 12f, 500, DIM, u, right - left);
    }

    private float drawStatus(Canvas canvas, float left, float top, float width, float u) {
        if (status.isEmpty()) return 0f;
        StaticLayout message = paragraph(status, width - 28f * u, 12f, 500, BONE, u, 4);
        float height = 31f * u + message.getHeight() + 12f * u + (busy ? 8f * u : 0f);
        RectF card = rect(left, top, left + width, top + height);
        panel(canvas, card, PANEL_ALT, busy ? RED : 0xff51313a, u);
        drawLine(canvas, busy ? "MOD OPERATION IN PROGRESS" : "LIBRARY STATUS", left + 14f * u, top + 7f * u,
                19f * u, 11.5f, 700, busy ? RED_HOT : DIM, u, width - 28f * u);
        drawParagraph(canvas, status, left + 14f * u, top + 28f * u, width - 28f * u,
                12f, 500, BONE, u, 4);
        if (busy) {
            float phase = motion ? (now() % 1100L) / 1100f : 0.5f;
            float bar = (width - 28f * u) * 0.28f;
            float x = left + 14f * u + phase * (width - 28f * u - bar);
            paint.setColor(0xff3c3540);
            canvas.drawRect(left + 14f * u, top + height - 8f * u, left + width - 14f * u,
                    top + height - 5f * u, paint);
            paint.setColor(RED_HOT);
            canvas.drawRect(x, top + height - 8f * u, x + bar, top + height - 5f * u, paint);
        }
        return height;
    }

    private long now() {
        return SystemClock.uptimeMillis();
    }

    private float drawEmpty(Canvas canvas, float left, float top, float width, float u) {
        String body = "Import a ZIP, texture pack, loose game-data file, Lua package, or Android ARM64 plugin. "
                + "New mods start disabled; original game files and saves stay untouched.";
        StaticLayout bodyLayout = paragraph(body, width - 28f * u, 13f, 500, DIM, u, 5);
        float height = 43f * u + bodyLayout.getHeight() + 17f * u;
        RectF card = rect(left, top, left + width, top + height);
        panel(canvas, card, PANEL, 0xff45363f, u);
        drawLine(canvas, "NO MODS INSTALLED", left + 14f * u, top + 9f * u, 25f * u,
                15f, 700, BONE, u, width - 28f * u);
        drawParagraph(canvas, body, left + 14f * u, top + 38f * u, width - 28f * u, 13f, 500, DIM, u, 5);
        return height;
    }

    private void drawCover(Canvas canvas, ModManifest mod, RectF bounds, float u) {
        paint.setColor(0xff111218);
        canvas.drawRect(bounds, paint);
        Bitmap bitmap = mod.coverFile() == null ? null : covers.get(mod.coverFile().getAbsolutePath());
        if (bitmap != null && !bitmap.isRecycled()) {
            float scale = Math.max(bounds.width() / bitmap.getWidth(), bounds.height() / bitmap.getHeight());
            float width = bitmap.getWidth() * scale, height = bitmap.getHeight() * scale;
            coverSource.set(0, 0, bitmap.getWidth(), bitmap.getHeight());
            RectF destination = rect(bounds.centerX() - width / 2f, bounds.centerY() - height / 2f,
                    bounds.centerX() + width / 2f, bounds.centerY() + height / 2f);
            canvas.save();
            canvas.clipRect(bounds);
            canvas.drawBitmap(bitmap, coverSource, destination, bitmapPaint);
            canvas.restore();
        } else {
            drawCentered(canvas, !mod.plugin.isEmpty() ? "[ ARM64 ]" : !mod.script.isEmpty() ? "[ LUA ]" : "[ P3D ]",
                    bounds, 17f, 700, RED_HOT, u);
        }
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1f, u));
        paint.setColor(mod.enabled ? RED_HOT : 0xff45363f);
        canvas.drawRect(bounds, paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(RED);
        canvas.drawRect(bounds.left, bounds.bottom - 3f * u, bounds.right, bounds.bottom, paint);
    }

    private float drawModCard(Canvas canvas, ModManifest mod, int index, float left, float top, float width, float u) {
        boolean expanded = detailsId.equals(mod.id);
        float coverWidth = width / u < 560f ? 92f * u : 148f * u;
        float coverHeight = coverWidth * 0.68f;
        float textLeft = left + coverWidth + 26f * u;
        float inner = width - coverWidth - 40f * u;
        StringBuilder meta = new StringBuilder();
        if (!mod.version.isEmpty()) meta.append(mod.version);
        if (!mod.author.isEmpty()) {
            if (meta.length() > 0) meta.append("  •  ");
            meta.append(mod.author);
        }
        if (meta.length() > 0) meta.append("  •  ");
        meta.append(mod.contentLabel().isEmpty() ? "Incompatible package" : mod.contentLabel());
        meta.append("  •  Priority ").append(mod.priority);
        StaticLayout metaLayout = paragraph(meta.toString(), inner, 11.5f, 500, DIM, u, 3);
        String descriptionText = mod.description;
        if (expanded && !mod.dependencies.isEmpty()) {
            if (!descriptionText.isEmpty()) descriptionText += "\n";
            descriptionText += "Requires: " + String.join(", ", mod.dependencies);
        }
        StaticLayout description = descriptionText.isEmpty() ? null
                : paragraph(descriptionText, inner, 12.5f, 500, BONE, u, expanded ? 8 : 2);
        String diagnostic = !mod.error.isEmpty() ? mod.error : mod.launchError;
        String state = !mod.error.isEmpty() ? "INCOMPATIBLE PACKAGE"
                : !mod.launchError.isEmpty() ? (mod.enabled ? "LAST LOAD FAILED / RETRY ON PLAY" : "LAST LOAD FAILED")
                : mod.enabled ? "ACTIVE ON NEXT PLAY" : "INSTALLED / DISABLED";
        if (!diagnostic.isEmpty()) state += "\n" + diagnostic;
        if (pluginArmedId.equals(mod.id)) {
            state += mod.script.isEmpty()
                    ? "\nNative ARM64 code is trusted optional code and can crash the game. Confirm to enable it."
                    : "\nScripts can call the engine and change guest memory. Enable only code you trust; sandbox limits do not make engine calls safe.";
        }
        if (removeArmedId.equals(mod.id)) state += "\nConfirm removal of this mod folder only; saves stay untouched.";
        StaticLayout stateLayout = paragraph(state, inner, 11.5f, 600,
                !diagnostic.isEmpty() ? 0xffff8c83 : mod.enabled ? OK : DIM, u, 6);
        float contentHeight = 29f * u + metaLayout.getHeight()
                + (description == null ? 0f : description.getHeight() + 5f * u)
                + stateLayout.getHeight() + 7f * u;
        float contentBottom = top + 10f * u + Math.max(coverHeight, contentHeight);
        float buttonTop = contentBottom + 9f * u;
        float height = buttonTop - top + touch(u) + 10f * u;
        RectF card = rect(left, top, left + width, top + height);
        int edge = !mod.error.isEmpty() ? 0xff81505a : mod.enabled ? RED_HOT : 0xff45363f;
        panel(canvas, card, mod.enabled ? 0xf02b1b23 : PANEL, edge, u);
        drawCover(canvas, mod, rect(left + 12f * u, top + 12f * u,
                left + 12f * u + coverWidth, top + 12f * u + coverHeight), u);
        drawLine(canvas, mod.name, textLeft, top + 9f * u, 25f * u, 17f, 700, BONE, u, inner);
        float y = top + 36f * u;
        drawParagraph(canvas, meta.toString(), textLeft, y, inner, 11.5f, 500, DIM, u, 3);
        y += metaLayout.getHeight() + 5f * u;
        if (description != null) {
            drawParagraph(canvas, descriptionText, textLeft, y, inner, 12.5f, 500, BONE, u, expanded ? 8 : 2);
            y += description.getHeight() + 5f * u;
        }
        drawParagraph(canvas, state, textLeft, y, inner, 11.5f, 600,
                !diagnostic.isEmpty() ? 0xffff8c83 : mod.enabled ? OK : DIM, u, 6);
        float gap = 7f * u;
        boolean configurable = mod.error.isEmpty() && !mod.options.isEmpty();
        int buttonCount = configurable ? 4 : 3;
        float buttonWidth = (width - 24f * u - gap * (buttonCount - 1)) / buttonCount;
        RectF details = rect(left + 12f * u, buttonTop,
                left + 12f * u + buttonWidth, buttonTop + touch(u));
        button(canvas, ACTION_DETAILS + index, details, expanded ? "LESS" : "DETAILS", BUTTON_GHOST, !busy, u);
        RectF previous = details;
        if (configurable) {
            RectF configure = rect(previous.right + gap, buttonTop, previous.right + gap + buttonWidth,
                    buttonTop + touch(u));
            button(canvas, ACTION_CONFIGURE + index, configure, "CONFIGURE", BUTTON_GHOST, !busy, u);
            previous = configure;
        }
        RectF toggle = rect(previous.right + gap, buttonTop, previous.right + gap + buttonWidth,
                buttonTop + touch(u));
        RectF remove = rect(toggle.right + gap, buttonTop, left + width - 12f * u, buttonTop + touch(u));
        String toggleLabel = pluginArmedId.equals(mod.id)
                ? (mod.script.isEmpty() ? "CONFIRM ARM64" : "CONFIRM CODE") : mod.enabled ? "DISABLE" : "ENABLE";
        button(canvas, ACTION_TOGGLE + index, toggle, toggleLabel,
                !mod.enabled && mod.error.isEmpty() ? BUTTON_PRIMARY : BUTTON_GHOST,
                !busy && (mod.error.isEmpty() || mod.enabled), u);
        button(canvas, ACTION_REMOVE + index, remove, removeArmedId.equals(mod.id) ? "CONFIRM" : "REMOVE",
                removeArmedId.equals(mod.id) ? BUTTON_PRIMARY : BUTTON_GHOST, !busy, u);
        return height;
    }

    private float drawCompatibility(Canvas canvas, float left, float top, float width, float u) {
        String body = "Lua packages use sandboxed text source. Windows plugins need an Android ARM64 port. DDS replacements keep the original format and dimensions.";
        StaticLayout bodyLayout = paragraph(body, width - 28f * u, 11.5f, 500, DIM, u, 2);
        float height = 36f * u + bodyLayout.getHeight() + 13f * u;
        RectF card = rect(left, top, left + width, top + height);
        panel(canvas, card, 0xb011141b, 0xff3f3b43, u);
        drawLine(canvas, "ORIGINAL GAME FILES STAY UNTOUCHED", left + 14f * u, top + 8f * u,
                21f * u, 11.5f, 700, OK, u, width - 28f * u);
        drawParagraph(canvas, body, left + 14f * u, top + 31f * u, width - 28f * u,
                11.5f, 500, DIM, u, 2);
        return height;
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        rectPoolUsed = 0;
        if (scroller.computeScrollOffset()) {
            scrollY = scroller.getCurrY();
            invalidate();
        }
        long current = now();
        if (!removeArmedId.isEmpty() && current - removeArmedAt >= 6000) removeArmedId = "";
        if (!pluginArmedId.isEmpty() && current - pluginArmedAt >= 8000) pluginArmedId = "";
        drawBackground(canvas, current);
        contentShiftX = 0f;
        float u = unit();
        lastUnit = u;
        float availableLeft = insetLeft + 20f * u;
        float availableRight = getWidth() - insetRight - 20f * u;
        float fullWidth = Math.max(1f, availableRight - availableLeft);
        float width = Math.min(980f * u, fullWidth);
        float left = availableLeft + (fullWidth - width) / 2f;
        float right = left + width;
        for (int i = 0; i < usedActionCount; ++i) {
            int action = usedActions[i];
            if (actionBounds[action] != null) actionBounds[action].setEmpty();
        }
        usedActionCount = 0;
        drawHeader(canvas, left, right, insetTop + 12f * u, u);

        contentTop = insetTop + 121f * u;
        contentBottom = getHeight() - insetBottom - 12f * u;
        float available = Math.max(1f, contentBottom - contentTop);
        float y = contentTop - scrollY;
        canvas.save();
        canvas.clipRect(0, contentTop, getWidth(), contentBottom);
        if (motion) {
            float entrance = LauncherMotion.easeOut((current - tabChangedAt) / 420f);
            contentShiftX = (1f - entrance) * 18f * u;
            canvas.translate(contentShiftX, 0f);
        }
        float statusHeight = drawStatus(canvas, left, y, width, u);
        if (statusHeight > 0f) y += statusHeight + 12f * u;
        section(canvas, "01 / INSTALLED MODS", left, right, y, u);
        y += 39f * u;
        if (mods.isEmpty()) {
            y += drawEmpty(canvas, left, y, width, u) + 12f * u;
        } else {
            for (int i = 0; i < mods.size(); ++i) {
                y += drawModCard(canvas, mods.get(i), i, left, y, width, u) + 10f * u;
            }
        }
        y += drawCompatibility(canvas, left, y, width, u) + 18f * u;
        canvas.restore();
        contentShiftX = 0f;

        float contentHeight = y + scrollY - contentTop;
        float oldMax = maxScrollY;
        maxScrollY = Math.max(0f, contentHeight - available);
        if (scrollY > maxScrollY || scrollY < 0f) {
            scrollY = Math.max(0f, Math.min(scrollY, maxScrollY));
            invalidate();
        } else if (Math.abs(oldMax - maxScrollY) > 0.5f) {
            invalidate();
        }
        if (maxScrollY > 0f) {
            float trackTop = contentTop + 8f * u;
            float trackBottom = contentBottom - 8f * u;
            paint.setColor(0x50513c46);
            canvas.drawRect(right + 5f * u, trackTop, right + 7f * u, trackBottom, paint);
            float thumbHeight = Math.max(28f * u, (trackBottom - trackTop) * available / contentHeight);
            float thumbY = trackTop + (trackBottom - trackTop - thumbHeight) * scrollY / maxScrollY;
            paint.setColor(RED);
            canvas.drawRect(right + 5f * u, thumbY, right + 7f * u, thumbY + thumbHeight, paint);
        }
        if (motion && getWindowVisibility() == VISIBLE && isAttachedToWindow()) postInvalidateOnAnimation();
    }

    private void activate(int action) {
        if (action == ACTION_NONE) return;
        performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        if (action == ACTION_BACK) {
            listener.onClose();
            return;
        }
        if (action == ACTION_IMPORT) {
            if (!busy) listener.onImport();
            return;
        }
        if (busy) return;
        int index;
        if (action >= ACTION_CONFIGURE) {
            index = action - ACTION_CONFIGURE;
            if (index >= 0 && index < mods.size()) listener.onConfigure(mods.get(index));
            return;
        }
        if (action >= ACTION_DETAILS) {
            index = action - ACTION_DETAILS;
            if (index >= 0 && index < mods.size()) {
                String id = mods.get(index).id;
                detailsId = detailsId.equals(id) ? "" : id;
                removeArmedId = "";
                pluginArmedId = "";
                invalidate();
            }
            return;
        }
        if (action >= ACTION_REMOVE && action < ACTION_DETAILS) {
            index = action - ACTION_REMOVE;
            if (index < 0 || index >= mods.size()) return;
            ModManifest mod = mods.get(index);
            long current = now();
            if (!removeArmedId.equals(mod.id) || current - removeArmedAt >= 6000) {
                removeArmedId = mod.id;
                removeArmedAt = current;
                pluginArmedId = "";
                setStatus("Removing '" + mod.name + "' affects only its own mod folder. Tap CONFIRM within 6 seconds.", true);
                postInvalidateDelayed(6100);
                return;
            }
            listener.onRemove(mod);
            return;
        }
        if (action >= ACTION_TOGGLE && action < ACTION_REMOVE) {
            index = action - ACTION_TOGGLE;
            if (index < 0 || index >= mods.size()) return;
            ModManifest mod = mods.get(index);
            if (!mod.error.isEmpty() && !mod.enabled) return;
            boolean enable = !mod.enabled;
            long current = now();
            if (enable && mod.hasExecutableContent()
                    && (!pluginArmedId.equals(mod.id) || current - pluginArmedAt >= 8000)) {
                pluginArmedId = mod.id;
                pluginArmedAt = current;
                removeArmedId = "";
                setStatus(mod.script.isEmpty()
                        ? "Only enable native plugins you trust. Windows plugins need an Android ARM64 port. Tap CONFIRM ARM64 within 8 seconds."
                        : "Scripts can access engine memory and calls despite host sandboxing. Enable only trusted code; tap CONFIRM CODE within 8 seconds.", true);
                postInvalidateDelayed(8100);
                return;
            }
            pluginArmedId = "";
            removeArmedId = "";
            listener.onSetEnabled(mod, enable);
        }
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (velocityTracker == null) velocityTracker = VelocityTracker.obtain();
        velocityTracker.addMovement(event);
        float y = event.getY();
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                requestFocus();
                focusedAction = ACTION_NONE;
                getParent().requestDisallowInterceptTouchEvent(true);
                scroller.forceFinished(true);
                scrolling = false;
                lastTouchY = y;
                pressedAction = actionAt(event.getX(), y);
                invalidate();
                return true;
            case MotionEvent.ACTION_MOVE:
                if (!scrolling && Math.abs(y - lastTouchY) > touchSlop) {
                    scrolling = true;
                    pressedAction = ACTION_NONE;
                }
                if (scrolling) {
                    scrollY -= y - lastTouchY;
                    scrollY = Math.max(0f, Math.min(scrollY, maxScrollY));
                    lastTouchY = y;
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP:
                int action = ACTION_NONE;
                if (scrolling) {
                    velocityTracker.computeCurrentVelocity(1000,
                            ViewConfiguration.get(getContext()).getScaledMaximumFlingVelocity());
                    scroller.fling(0, Math.round(scrollY), 0, Math.round(-velocityTracker.getYVelocity()),
                            0, 0, 0, Math.round(maxScrollY));
                } else {
                    int releasedAction = actionAt(event.getX(), y);
                    if (releasedAction != ACTION_NONE && releasedAction == pressedAction) action = releasedAction;
                }
                pressedAction = ACTION_NONE;
                scrolling = false;
                recycleVelocityTracker();
                invalidate();
                // Complete tracker cleanup before BACK can synchronously detach this view.
                if (action != ACTION_NONE) {
                    performClick();
                    activate(action);
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
                pressedAction = ACTION_NONE;
                scrolling = false;
                recycleVelocityTracker();
                invalidate();
                return true;
            default:
                return true;
        }
    }

    @Override public boolean performClick() {
        super.performClick();
        return true;
    }

    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if (event.getAction() == MotionEvent.ACTION_SCROLL) {
            float amount = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
            scrollY = Math.max(0f, Math.min(maxScrollY, scrollY + amount * 48f * lastUnit));
            invalidate();
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    private void focusNext(int direction) {
        if (usedActionCount == 0) return;
        int position = -1;
        for (int i = 0; i < usedActionCount; ++i) {
            if (usedActions[i] == focusedAction) {
                position = i;
                break;
            }
        }
        position = position < 0 ? (direction > 0 ? 0 : usedActionCount - 1)
                : (position + direction + usedActionCount) % usedActionCount;
        focusedAction = usedActions[position];
        RectF bounds = actionBounds[focusedAction];
        if (bounds != null && !bounds.isEmpty()) {
            if (bounds.top < contentTop) scrollY = Math.max(0f, scrollY - (contentTop - bounds.top) - 12f * lastUnit);
            if (bounds.bottom > contentBottom) scrollY = Math.min(maxScrollY, scrollY + bounds.bottom - contentBottom + 12f * lastUnit);
        }
        invalidate();
    }

    @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_TAB || keyCode == KeyEvent.KEYCODE_DPAD_LEFT
                || keyCode == KeyEvent.KEYCODE_DPAD_RIGHT || keyCode == KeyEvent.KEYCODE_DPAD_UP
                || keyCode == KeyEvent.KEYCODE_DPAD_DOWN) {
            int direction = keyCode == KeyEvent.KEYCODE_DPAD_LEFT || keyCode == KeyEvent.KEYCODE_DPAD_UP ? -1 : 1;
            focusNext(direction);
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_PAGE_UP || keyCode == KeyEvent.KEYCODE_PAGE_DOWN) {
            float amount = (contentBottom - contentTop) * 0.8f;
            scrollY = Math.max(0f, Math.min(maxScrollY,
                    scrollY + (keyCode == KeyEvent.KEYCODE_PAGE_UP ? -amount : amount)));
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_ENTER || keyCode == KeyEvent.KEYCODE_DPAD_CENTER
                || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER || keyCode == KeyEvent.KEYCODE_BUTTON_A) {
            if (focusedAction != ACTION_NONE) {
                performClick();
                activate(focusedAction);
                return true;
            }
        }
        return super.onKeyDown(keyCode, event);
    }

    private void recycleVelocityTracker() {
        if (velocityTracker == null) return;
        velocityTracker.recycle();
        velocityTracker = null;
    }

    @Override protected void onDetachedFromWindow() {
        recycleVelocityTracker();
        scroller.forceFinished(true);
        super.onDetachedFromWindow();
    }
}
