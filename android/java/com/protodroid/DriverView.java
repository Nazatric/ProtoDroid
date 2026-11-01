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
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Build;
import android.os.SystemClock;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
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
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * A guided Prototype-style driver screen built with platform Canvas APIs. Common choices stay short and visible;
 * package management and technical details are progressive disclosure, not a wall of advanced options.
 */
final class DriverView extends View implements DriverManager.Observer {
    interface Listener {
        void onClose();
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
    private static final int ACTION_CANCEL = 2;
    private static final int ACTION_IMPORT = 3;
    private static final int ACTION_CLEAR_STATUS = 4;
    private static final int ACTION_PACKAGES = 5;
    private static final int ACTION_SHOW_ALL = 6;
    private static final int ACTION_BROWSE_DOWNLOADS = 7;
    private static final int ACTION_MODE = 10;       // + mode 0..3
    private static final int ACTION_TAB = 20;        // + tab 0..1
    private static final int ACTION_SELECT = 1000;   // + installed index
    private static final int ACTION_REMOVE = 2000;   // + installed index
    private static final int ACTION_DOWNLOAD = 3000; // + visible catalog index
    private static final int ACTION_DETAILS = 3500;  // + installed index
    private static final int MAX_ACTION = 4096;

    private static final int BUTTON_NORMAL = 0;
    private static final int BUTTON_PRIMARY = 1;
    private static final int BUTTON_GHOST = 2;

    private final Listener listener;
    private final GameSettings settings;
    private final DriverManager manager;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bitmapPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Path path = new Path();
    private final Typeface medium;
    private final Typeface semibold;
    private final Typeface bold;
    private final Bitmap backdrop;
    private final Bitmap icon;
    private final boolean motion;
    private final long startedAt = SystemClock.uptimeMillis();
    private final float density;
    private final float fontScale;
    private final int touchSlop;
    private final OverScroller scroller;
    private final RectF[] actionBounds = new RectF[MAX_ACTION];
    private final ArrayList<Integer> usedActions = new ArrayList<>();
    private final Map<String, StaticLayout> paragraphCache = new HashMap<>();
    private final Set<String> catalogInstalledIds = new HashSet<>();
    private final ArrayList<DriverRepository.Entry> installed = new ArrayList<>();
    private final ArrayList<DriverCatalog.Entry> visibleCatalog = new ArrayList<>();
    private final boolean maliDevice;
    private final boolean adrenoDevice;

    private int insetLeft, insetTop, insetRight, insetBottom;
    private int pressedAction = ACTION_NONE;
    private int focusedAction = ACTION_NONE;
    private float lastTouchY;
    private float scrollY;
    private float contentTop;
    private float contentBottom;
    private float maxScrollY;
    private float lastUnit = 1f;
    private boolean scrolling;
    private int packageTab;
    private boolean showPackages;
    private boolean showAllPackages;
    private String selectedId = "";
    private String detailsId = "";
    private String observedStatus = "";
    private boolean observedBusy;
    private String removeArmedId = "";
    private long removeArmedAt;
    private VelocityTracker velocityTracker;
    private Shader backgroundShade;
    private int backgroundWidth = -1;
    private int backgroundHeight = -1;

    DriverView(Context context, GameSettings settings, DriverManager manager, Listener listener) {
        super(context);
        this.settings = settings;
        this.manager = manager;
        this.listener = listener;
        density = getResources().getDisplayMetrics().density;
        fontScale = getResources().getConfiguration().fontScale;
        touchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        scroller = new OverScroller(context);
        AssetManager assets = context.getAssets();
        medium = loadFont(assets, "setup/fonts/barlow_condensed_medium.ttf");
        semibold = loadFont(assets, "setup/fonts/barlow_condensed_semibold.ttf");
        bold = loadFont(assets, "setup/fonts/barlow_condensed_bold.ttf");
        backdrop = loadBitmap(assets, "setup/images/bg_2.jpg");
        icon = loadBitmap(assets, "setup/images/protodroid_icon.png");
        motion = LauncherMotion.enabled();
        maliDevice = new File("/dev/mali0").exists() || new File("/dev/mali").exists()
                || new File("/dev/mali_kbase").exists();
        adrenoDevice = !maliDevice && new File("/dev/kgsl-3d0").exists();
        showAllPackages = DriverScreenPolicy.showAllByDefault(maliDevice, adrenoDevice);
        showPackages = settings.driverMode() == 3;
        setBackgroundColor(Color.BLACK);
        setClickable(true);
        setFocusable(true);
        manager.setObserver(this);
        refresh();
    }

    private static Typeface loadFont(AssetManager assets, String name) {
        try {
            return Typeface.createFromAsset(assets, name);
        } catch (RuntimeException e) {
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

    private void refresh() {
        installed.clear();
        installed.addAll(manager.installed());
        DriverRepository.Entry selected = manager.selected();
        selectedId = selected == null ? "" : selected.id;
        catalogInstalledIds.clear();
        visibleCatalog.clear();
        for (DriverRepository.Entry entry : installed) {
            if (!entry.catalogId().isEmpty()) catalogInstalledIds.add(entry.catalogId());
        }
        for (DriverCatalog.Entry entry : DriverCatalog.ENTRIES) {
            if (showAllPackages || DriverScreenPolicy.supportsBackend(maliDevice, adrenoDevice, entry.backend)) {
                visibleCatalog.add(entry);
            }
        }
        updateContentDescription();
        invalidate();
    }

    @Override public void onDriverStateChanged() {
        String status = manager.statusTitle();
        boolean busy = manager.isBusy();
        if (!status.equals(observedStatus) || busy != observedBusy) {
            observedStatus = status;
            observedBusy = busy;
            refresh();
        } else {
            updateContentDescription();
            invalidate();
        }
    }

    private void updateContentDescription() {
        String status = manager.statusTitle();
        String custom = settings.driverMode() == 3
                ? (selectedId.isEmpty() ? " No valid custom package is selected." : " A custom package is selected.")
                : "";
        setContentDescription("Prototype graphics drivers. Current choice: "
                + DriverScreenPolicy.modeName(settings.driverMode()) + "." + custom
                + " Changes apply the next time you play. "
                + (showPackages ? "Driver packages are open. " : "Driver packages are collapsed. ")
                + (status.isEmpty() ? "Automatic is recommended for most people."
                : status + ". " + manager.statusMessage()));
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
        boolean portrait = getHeight() > getWidth();
        if (portrait) return density;
        float usableDp = Math.max(1f, getHeight() - insetTop - insetBottom) / density;
        return density * Math.max(0.8f, Math.min(2.2f, usableDp / 360f));
    }

    private float touch(float u) {
        // Landscape scales the layout to fit short screens, but accessibility targets must never shrink below 48 dp.
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
        target.setLetterSpacing(size == 0f ? 0f : 0.015f);
        target.setStyle(Paint.Style.FILL);
        return target;
    }

    private float baseline(float top, float lineHeight, TextPaint paint) {
        Paint.FontMetrics metrics = paint.getFontMetrics();
        return top + (lineHeight - (metrics.descent - metrics.ascent)) / 2f - metrics.ascent;
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

    private void drawCentered(Canvas canvas, String value, RectF bounds, float size, int weight, int color, float u) {
        configure(text, size, weight, color, u);
        String shown = ellipsized(value, Math.max(0f, bounds.width() - 16f * u));
        canvas.drawText(shown, bounds.centerX() - text.measureText(shown) / 2f,
                baseline(bounds.top, bounds.height(), text), text);
    }

    private StaticLayout paragraph(String value, float width, float size, int weight, int color, float u) {
        int pixels = Math.max(1, Math.round(width));
        String key = value + "\n" + pixels + "\n" + size + "\n" + weight + "\n" + color + "\n" + u;
        StaticLayout cached = paragraphCache.get(key);
        if (cached != null) return cached;
        TextPaint layoutPaint = configure(new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG),
                size, weight, color, u);
        StaticLayout layout = StaticLayout.Builder.obtain(value, 0, value.length(), layoutPaint, pixels)
                .setAlignment(Layout.Alignment.ALIGN_NORMAL).setIncludePad(false).build();
        if (paragraphCache.size() > 180) paragraphCache.clear();
        paragraphCache.put(key, layout);
        return layout;
    }

    private float drawParagraph(Canvas canvas, String value, float x, float top, float width, float size,
                                int weight, int color, float u) {
        StaticLayout layout = paragraph(value, width, size, weight, color, u);
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
        canvas.drawPath(cutPath(bounds, Math.min(9f * u, bounds.height() * 0.2f)), paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1f, u * 0.7f));
        paint.setColor(edge);
        canvas.drawPath(cutPath(bounds, Math.min(9f * u, bounds.height() * 0.2f)), paint);
        paint.setStyle(Paint.Style.FILL);
    }

    private void bind(int action, RectF bounds) {
        if (action <= ACTION_NONE || action >= MAX_ACTION || bounds.isEmpty()) return;
        if (actionBounds[action] == null) actionBounds[action] = new RectF();
        actionBounds[action].set(bounds);
        usedActions.add(action);
    }

    private void bind(int action, float left, float top, float right, float bottom) {
        bind(action, new RectF(left, top, right, bottom));
    }

    private int actionAt(float x, float y) {
        for (int i = usedActions.size() - 1; i >= 0; --i) {
            int action = usedActions.get(i);
            RectF bounds = actionBounds[action];
            if (bounds != null && bounds.contains(x, y)) return action;
        }
        return ACTION_NONE;
    }

    private void button(Canvas canvas, int action, RectF bounds, String label, int variant, boolean enabled, float u) {
        boolean pressed = pressedAction == action && enabled;
        int edge = variant == BUTTON_PRIMARY || (focusedAction == action && enabled)
                ? RED_HOT : (pressed ? RED_HOT : 0xff51414a);
        int fill = variant == BUTTON_PRIMARY ? (pressed ? 0xffa91822 : RED_DARK)
                : variant == BUTTON_GHOST ? 0x5010141b : (pressed ? 0xff352027 : 0xff202027);
        if (!enabled) {
            edge = 0xff38353d;
            fill = 0xff17181d;
        }
        if (enabled) bind(action, bounds);
        canvas.save();
        if (motion && pressed) canvas.scale(0.97f, 0.97f, bounds.centerX(), bounds.centerY());
        panel(canvas, bounds, fill, edge, u);
        drawCentered(canvas, label, bounds, 12.5f, 700, enabled ? BONE : MUTED, u);
        canvas.restore();
    }

    private void radio(Canvas canvas, float cx, float cy, boolean selected, float u) {
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(1.8f * u);
        paint.setColor(selected ? RED_HOT : MUTED);
        canvas.drawCircle(cx, cy, 9f * u, paint);
        if (selected) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(RED_HOT);
            canvas.drawCircle(cx, cy, 4.5f * u, paint);
        }
        paint.setStyle(Paint.Style.FILL);
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
            float drift = motion ? 0.012f * (float) Math.sin((now - startedAt) / 10000.0) : 0f;
            float scale = Math.max(width / (float) backdrop.getWidth(), height / (float) backdrop.getHeight())
                    * (1.035f + drift);
            RectF destination = new RectF(0, 0, backdrop.getWidth() * scale, backdrop.getHeight() * scale);
            destination.offset((width - destination.width()) / 2f, (height - destination.height()) / 2f);
            bitmapPaint.setAlpha(175);
            canvas.drawBitmap(backdrop, null, destination, bitmapPaint);
            bitmapPaint.setAlpha(255);
        }
        if (backgroundShade == null || backgroundWidth != width || backgroundHeight != height) {
            backgroundWidth = width;
            backgroundHeight = height;
            backgroundShade = new LinearGradient(0, 0, width, height,
                    new int[]{0xf208090d, 0xea111018, 0xf2230e16}, null, Shader.TileMode.CLAMP);
        }
        paint.setShader(backgroundShade);
        canvas.drawRect(0, 0, width, height, paint);
        paint.setShader(null);
        paint.setColor(0x16c9141a);
        paint.setStrokeWidth(1f);
        for (int x = -height; x < width + height; x += Math.max(32, width / 12)) {
            canvas.drawLine(x, 0, x - height, height, paint);
        }
        paint.setColor(0xffc9141a);
        canvas.drawRect(0, 0, width, Math.max(2f, density * 0.6f), paint);
    }

    private void drawHeader(Canvas canvas, float left, float right, float top, float u) {
        float iconSize = 38f * u;
        if (icon != null) canvas.drawBitmap(icon, null, new RectF(left, top, left + iconSize, top + iconSize), bitmapPaint);
        float brandLeft = left + iconSize + 8f * u;
        drawLine(canvas, "[", brandLeft, top, 38f * u, 25f, 500, RED_HOT, u, -1);
        drawLine(canvas, "PROTOTYPE", brandLeft + 14f * u, top, 38f * u, 23f, 700, BONE, u, -1);
        configure(text, 23f, 700, BONE, u);
        float brandEnd = brandLeft + 14f * u + text.measureText("PROTOTYPE") + 4f * u;
        drawLine(canvas, "]", brandEnd, top, 38f * u, 25f, 500, RED_HOT, u, -1);
        RectF back = new RectF(right - 104f * u, top, right, top + touch(u));
        button(canvas, ACTION_BACK, back, "BACK", BUTTON_GHOST, true, u);
        drawLine(canvas, "GRAPHICS DRIVERS", left, top + 52f * u, 32f * u, 26f, 700, BONE, u, right - left);
        drawLine(canvas, "Choose once. Your choice applies the next time you play.",
                left, top + 86f * u, 20f * u, 12.5f, 500, DIM, u, right - left);
    }

    private String modeBody(int mode) {
        switch (mode) {
            case 1:
                return "Uses the Vulkan driver already installed by your phone maker. No package is loaded.";
            case 2:
                return "Uses ProtoDroid's bundled Adreno driver. Nothing to download.";
            case 3:
                return "Uses one installed package you choose below. Best left to advanced testing.";
            default:
                return "Chooses a compatible path for this device. Best for most people.";
        }
    }

    private String modeNote(int mode) {
        DriverRepository.Entry selected = manager.selected();
        switch (mode) {
            case 1:
                return "Useful for comparison or recovering from a custom-driver problem.";
            case 2:
                if (maliDevice) return "Not for Mali — the phone driver is used instead.";
                if (!adrenoDevice) return "Adreno only — other devices safely use the phone driver.";
                return "For Adreno devices. Your phone's global driver is not replaced.";
            case 3:
                if (settings.driverMode() != 3) {
                    return "Opens Driver packages so you can make an explicit selection.";
                }
                if (selected != null) return "Selected: " + selected.installed.name + " • applies next Play.";
                return "No valid package selected. Open Driver packages to choose one.";
            default:
                return "Recommended • " + DriverScreenPolicy.deviceFamily(maliDevice, adrenoDevice)
                        + (maliDevice || adrenoDevice ? " detected" : "") + ".";
        }
    }

    private float drawModeCard(Canvas canvas, int mode, float left, float top, float width, float u) {
        boolean selected = settings.driverMode() == mode;
        String body = modeBody(mode);
        String note = modeNote(mode);
        float textLeft = left + 50f * u;
        float inner = width - 66f * u;
        StaticLayout bodyLayout = paragraph(body, inner, 12.5f, 500, BONE, u);
        int noteColor = (mode == 2 && maliDevice)
                || (mode == 3 && settings.driverMode() == 3 && selectedId.isEmpty()) ? AMBER : DIM;
        StaticLayout noteLayout = paragraph(note, inner, 11.5f, 600, noteColor, u);
        float height = 42f * u + bodyLayout.getHeight() + 6f * u + noteLayout.getHeight() + 13f * u;
        height = Math.max(height, 88f * u);
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, selected ? 0xf02c1b23 : PANEL, selected ? RED_HOT : 0xff45363f, u);
        bind(ACTION_MODE + mode, card);
        radio(canvas, left + 24f * u, top + 23f * u, selected, u);
        drawLine(canvas, DriverScreenPolicy.modeName(mode), textLeft, top + 10f * u, 25f * u,
                16f, 700, selected ? BONE : DIM, u, inner);
        drawParagraph(canvas, body, textLeft, top + 39f * u, inner, 12.5f, 500, BONE, u);
        drawParagraph(canvas, note, textLeft, top + 39f * u + bodyLayout.getHeight() + 6f * u,
                inner, 11.5f, 600, noteColor, u);
        return height;
    }

    private float drawCurrent(Canvas canvas, float left, float top, float width, float u) {
        int mode = settings.driverMode();
        String current = DriverScreenPolicy.modeName(mode);
        DriverRepository.Entry selected = manager.selected();
        if (mode == 3 && selected != null) current += " • " + selected.installed.name;
        String body = mode == 3 && selected == null
                ? "Custom mode needs a valid selection. Automatic is restored at launch if none is available."
                : "This affects ProtoDroid only. Your phone's global Vulkan driver and your saves are not changed.";
        StaticLayout bodyLayout = paragraph(body, width - 28f * u, 12f, 500, DIM, u);
        float height = 34f * u + bodyLayout.getHeight() + 13f * u;
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, PANEL_ALT, mode == 3 && selected == null ? AMBER : 0xff51313a, u);
        drawLine(canvas, "CURRENT CHOICE  /  " + current, left + 14f * u, top + 8f * u,
                20f * u, 12.5f, 700, mode == 3 && selected == null ? AMBER : OK, u, width - 28f * u);
        drawParagraph(canvas, body, left + 14f * u, top + 31f * u, width - 28f * u, 12f, 500, DIM, u);
        return height;
    }

    private float drawStatus(Canvas canvas, float left, float top, float width, float u) {
        String title = manager.statusTitle();
        if (title.isEmpty()) return 0f;
        String message = manager.statusMessage();
        boolean busy = manager.isBusy();
        boolean hasButton = !busy || manager.operationTitle().startsWith("Downloading");
        StaticLayout messageLayout = paragraph(message, width - 28f * u, 12f, 500, BONE, u);
        boolean progress = manager.progressTotal() > 0;
        float height = 35f * u + messageLayout.getHeight() + (progress ? 38f * u : 0f)
                + touch(u) + 22f * u;
        RectF card = new RectF(left, top, left + width, top + height);
        boolean good = title.contains("installed") || title.contains("selected") || title.contains("removed");
        panel(canvas, card, PANEL_ALT, good ? 0xff5d8154 : (busy ? RED : 0xff81505a), u);
        drawLine(canvas, title.toUpperCase(Locale.ROOT), left + 14f * u, top + 8f * u, 21f * u,
                12.5f, 700, good ? OK : RED_HOT, u, width - 28f * u);
        float y = top + 32f * u;
        drawParagraph(canvas, message, left + 14f * u, y, width - 28f * u, 12f, 500, BONE, u);
        y += messageLayout.getHeight() + 8f * u;
        if (progress) {
            float fraction = Math.max(0f, Math.min(1f, manager.progressDone() / (float) manager.progressTotal()));
            paint.setColor(0xff3c3540);
            canvas.drawRect(left + 14f * u, y + 8f * u, left + width - 14f * u, y + 13f * u, paint);
            paint.setColor(RED_HOT);
            canvas.drawRect(left + 14f * u, y + 8f * u,
                    left + 14f * u + (width - 28f * u) * fraction, y + 13f * u, paint);
            drawLine(canvas, String.format(Locale.ROOT, "%.1f / %.1f MiB  •  %d%%",
                    manager.progressDone() / 1048576.0, manager.progressTotal() / 1048576.0,
                    Math.round(fraction * 100)), left + 14f * u, y + 16f * u, 18f * u,
                    11f, 600, DIM, u, width - 28f * u);
            y += 38f * u;
        }
        if (hasButton) {
            RectF action = new RectF(left + width - 150f * u, y,
                    left + width - 14f * u, y + touch(u));
            button(canvas, busy ? ACTION_CANCEL : ACTION_CLEAR_STATUS, action,
                    busy ? "CANCEL DOWNLOAD" : "DISMISS", BUTTON_GHOST, true, u);
        } else {
            drawLine(canvas, "VALIDATING — KEEP PROTODROID OPEN", left + 14f * u, y,
                    touch(u), 11.5f, 600, DIM, u, width - 28f * u);
            postInvalidateDelayed(180);
        }
        return height;
    }

    private float drawPackageToggle(Canvas canvas, float left, float top, float width, float u) {
        String body = showPackages
                ? "Hide package downloads, imports, and technical details."
                : "Only needed for a specific custom build. Automatic does not need a download.";
        StaticLayout bodyLayout = paragraph(body, width - 102f * u, 12f, 500, DIM, u);
        float height = Math.max(76f * u, 38f * u + bodyLayout.getHeight() + 12f * u);
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, showPackages ? 0xf0251921 : PANEL, showPackages ? RED : 0xff45363f, u);
        bind(ACTION_PACKAGES, card);
        drawLine(canvas, "DRIVER PACKAGES", left + 14f * u, top + 9f * u, 23f * u,
                14f, 700, BONE, u, width - 102f * u);
        drawParagraph(canvas, body, left + 14f * u, top + 35f * u, width - 102f * u, 12f, 500, DIM, u);
        drawCentered(canvas, showPackages ? "−" : "+",
                new RectF(left + width - 72f * u, top, left + width - 14f * u, top + height),
                26f, 500, RED_HOT, u);
        return height;
    }

    private String backendLabel(String backend) {
        return DriverCatalog.PANVK.equals(backend) ? "PanVK-kbase • Mali • experimental"
                : "Turnip • Adreno";
    }

    private boolean isSelected(DriverRepository.Entry entry) {
        return settings.driverMode() == 3 && selectedId.equals(entry.id);
    }

    private float drawInstalledCard(Canvas canvas, DriverRepository.Entry entry, int index, float left, float top,
                                    float width, float u) {
        DriverPackage.Installed item = entry.installed;
        boolean selected = isSelected(entry);
        boolean expanded = detailsId.equals(entry.id);
        String detail = (item.version.isEmpty() ? "Custom package" : item.version) + "\n" + backendLabel(item.backend);
        StaticLayout detailLayout = paragraph(detail, width - 76f * u, 12f, 500, DIM, u);
        String selection = selected ? "SELECTED FOR NEXT PLAY" : "Tap the card to use this driver • Custom mode turns on";
        StaticLayout selectionLayout = paragraph(selection, width - 76f * u, 11.5f, 600, selected ? OK : DIM, u);
        String extra = expanded ? "Source: " + item.source + "\nPackage ID: " + entry.id
                + "\nThe package was validated before installation. A separate probe checks it again on the next Play." : "";
        StaticLayout extraLayout = expanded ? paragraph(extra, width - 28f * u, 11.5f, 500, DIM, u) : null;
        float mainHeight = 43f * u + detailLayout.getHeight() + 5f * u + selectionLayout.getHeight() + 12f * u;
        float height = mainHeight + (expanded ? extraLayout.getHeight() + 10f * u : 0f) + touch(u) + 14f * u;
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, selected ? 0xf02b1b23 : PANEL, selected ? RED_HOT : 0xff45363f, u);
        if (!selected && !manager.isBusy()) bind(ACTION_SELECT + index, left, top, left + width, top + mainHeight);
        radio(canvas, left + 25f * u, top + 23f * u, selected, u);
        drawLine(canvas, item.name, left + 50f * u, top + 10f * u, 25f * u,
                15.5f, 700, BONE, u, width - 66f * u);
        drawParagraph(canvas, detail, left + 50f * u, top + 39f * u, width - 66f * u, 12f, 500, DIM, u);
        float selectionTop = top + 39f * u + detailLayout.getHeight() + 5f * u;
        drawParagraph(canvas, selection, left + 50f * u, selectionTop, width - 66f * u,
                11.5f, 600, selected ? OK : DIM, u);
        if (expanded) {
            drawParagraph(canvas, extra, left + 14f * u, top + mainHeight + 3f * u,
                    width - 28f * u, 11.5f, 500, DIM, u);
        }
        float buttonTop = top + height - touch(u) - 8f * u;
        float gap = 8f * u;
        float buttonWidth = (width - 36f * u) / 2f;
        RectF details = new RectF(left + 14f * u, buttonTop,
                left + 14f * u + buttonWidth, buttonTop + touch(u));
        RectF remove = new RectF(details.right + gap, buttonTop, left + width - 14f * u, buttonTop + touch(u));
        button(canvas, ACTION_DETAILS + index, details, expanded ? "HIDE DETAILS" : "DETAILS",
                BUTTON_GHOST, !manager.isBusy(), u);
        boolean armed = removeArmedId.equals(entry.id) && SystemClock.uptimeMillis() - removeArmedAt < 6000;
        button(canvas, ACTION_REMOVE + index, remove, armed ? "CONFIRM REMOVE" : "REMOVE",
                armed ? BUTTON_PRIMARY : BUTTON_GHOST, !manager.isBusy(), u);
        return height;
    }

    private float drawCatalogCard(Canvas canvas, DriverCatalog.Entry entry, int index, float left, float top,
                                  float width, float u) {
        boolean narrow = width / u < 580f;
        float buttonColumn = narrow ? 0f : 174f * u;
        float inner = width - 28f * u - buttonColumn;
        String detail = entry.compatibility + "\n" + entry.source + " • "
                + String.format(Locale.ROOT, "%.2f MiB", entry.size / 1048576.0);
        StaticLayout description = paragraph(entry.description, inner, 12f, 500, BONE, u);
        StaticLayout detailLayout = paragraph(detail, inner, 11f, 600, DIM, u);
        boolean experimental = DriverCatalog.PANVK.equals(entry.backend);
        StaticLayout note = experimental ? paragraph("EXPERIMENTAL — exact GPU and kernel support still matters",
                inner, 11f, 700, AMBER, u) : null;
        float contentHeight = 41f * u + description.getHeight() + 6f * u + detailLayout.getHeight()
                + (experimental ? note.getHeight() + 5f * u : 0f) + 13f * u;
        float height = Math.max(104f * u, contentHeight) + (narrow ? touch(u) + 10f * u : 0f);
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, PANEL, experimental ? 0xff6b5037 : 0xff45363f, u);
        drawLine(canvas, entry.title, left + 14f * u, top + 10f * u, 25f * u,
                15.5f, 700, BONE, u, inner);
        drawParagraph(canvas, entry.description, left + 14f * u, top + 39f * u, inner, 12f, 500, BONE, u);
        float detailTop = top + 39f * u + description.getHeight() + 6f * u;
        drawParagraph(canvas, detail, left + 14f * u, detailTop, inner, 11f, 600, DIM, u);
        if (experimental) {
            drawParagraph(canvas, "EXPERIMENTAL — exact GPU and kernel support still matters",
                    left + 14f * u, detailTop + detailLayout.getHeight() + 5f * u, inner, 11f, 700, AMBER, u);
        }
        float buttonWidth = narrow ? width - 28f * u : 158f * u;
        float buttonLeft = narrow ? left + 14f * u : left + width - buttonWidth - 14f * u;
        float buttonTop = narrow ? top + height - touch(u) - 8f * u : top + 16f * u;
        RectF get = new RectF(buttonLeft, buttonTop, buttonLeft + buttonWidth, buttonTop + touch(u));
        button(canvas, ACTION_DOWNLOAD + index, get, catalogInstalled(entry) ? "REINSTALL" : "DOWNLOAD",
                BUTTON_PRIMARY, !manager.isBusy(), u);
        return height;
    }

    private boolean catalogInstalled(DriverCatalog.Entry entry) {
        return catalogInstalledIds.contains(entry.id);
    }

    private float drawImport(Canvas canvas, float left, float top, float width, float u) {
        String body = "Already have an AdrenoTools ZIP, .adpkg ZIP, or ARM64 Android .so? Import it here. "
                + "It is checked before installation and never selected automatically.";
        StaticLayout bodyLayout = paragraph(body, width - 28f * u, 12f, 500, DIM, u);
        float height = 40f * u + bodyLayout.getHeight() + touch(u) + 18f * u;
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, PANEL_ALT, 0xff45363f, u);
        drawLine(canvas, "IMPORT YOUR OWN", left + 14f * u, top + 9f * u, 22f * u,
                12.5f, 700, RED_HOT, u, width - 28f * u);
        drawParagraph(canvas, body, left + 14f * u, top + 34f * u, width - 28f * u, 12f, 500, DIM, u);
        RectF importButton = new RectF(left + 14f * u, top + height - touch(u) - 10f * u,
                left + width - 14f * u, top + height - 10f * u);
        button(canvas, ACTION_IMPORT, importButton, "IMPORT ZIP OR .SO", BUTTON_NORMAL, !manager.isBusy(), u);
        return height;
    }

    private float drawCompatibility(Canvas canvas, float left, float top, float width, float u) {
        String family = DriverScreenPolicy.deviceFamily(maliDevice, adrenoDevice);
        String body;
        if (!maliDevice && !adrenoDevice) {
            body = "GPU family was not detected from the device nodes. All pinned packages are shown; read each compatibility line and use Automatic if unsure.";
        } else if (showAllPackages) {
            body = family + " detected. Showing every package, including ones for other GPU families. Check compatibility before downloading.";
        } else {
            body = family + " detected. Showing only packages built for that family. This is a compatibility filter, not a guarantee for every GPU or kernel.";
        }
        StaticLayout bodyLayout = paragraph(body, width - 28f * u, 12f, 500, DIM, u);
        boolean canToggle = maliDevice || adrenoDevice;
        float height = 35f * u + bodyLayout.getHeight() + (canToggle ? touch(u) + 10f * u : 12f * u) + 12f * u;
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, PANEL_ALT, 0xff45363f, u);
        drawLine(canvas, "COMPATIBILITY", left + 14f * u, top + 8f * u, 21f * u,
                12f, 700, DIM, u, width - 28f * u);
        drawParagraph(canvas, body, left + 14f * u, top + 32f * u, width - 28f * u, 12f, 500, DIM, u);
        if (canToggle) {
            RectF toggle = new RectF(left + 14f * u, top + height - touch(u) - 10f * u,
                    left + width - 14f * u, top + height - 10f * u);
            button(canvas, ACTION_SHOW_ALL, toggle, showAllPackages ? "SHOW COMPATIBLE ONLY" : "SHOW ALL PACKAGES",
                    BUTTON_GHOST, !manager.isBusy(), u);
        }
        return height;
    }

    private float drawEmptyInstalled(Canvas canvas, float left, float top, float width, float u) {
        String body = "No custom packages are installed. You do not need one for Automatic or Phone driver.";
        StaticLayout bodyLayout = paragraph(body, width - 28f * u, 12.5f, 500, DIM, u);
        float height = 39f * u + bodyLayout.getHeight() + touch(u) + 20f * u;
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, PANEL, 0xff45363f, u);
        drawLine(canvas, "NOTHING TO MANAGE YET", left + 14f * u, top + 9f * u, 22f * u,
                12.5f, 700, DIM, u, width - 28f * u);
        drawParagraph(canvas, body, left + 14f * u, top + 34f * u, width - 28f * u, 12.5f, 500, DIM, u);
        RectF browse = new RectF(left + 14f * u, top + height - touch(u) - 10f * u,
                left + width - 14f * u, top + height - 10f * u);
        button(canvas, ACTION_BROWSE_DOWNLOADS, browse, "BROWSE DOWNLOADS", BUTTON_NORMAL, true, u);
        return height;
    }

    private float drawRecovery(Canvas canvas, float left, float top, float width, float u) {
        String recovery = "If a driver causes trouble, choose Automatic or Phone driver. A failed custom probe uses "
                + "System and records the reason. Packages are private to ProtoDroid and never replace the phone's global driver.";
        StaticLayout recoveryLayout = paragraph(recovery, width - 28f * u, 12f, 500, DIM, u);
        float height = recoveryLayout.getHeight() + 43f * u;
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, 0xb011141b, 0xff3f3b43, u);
        drawLine(canvas, "SAFE RECOVERY", left + 14f * u, top + 8f * u, 22f * u,
                12f, 700, OK, u, width - 28f * u);
        drawParagraph(canvas, recovery, left + 14f * u, top + 32f * u, width - 28f * u, 12f, 500, DIM, u);
        return height;
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (scroller.computeScrollOffset()) {
            scrollY = scroller.getCurrY();
            invalidate();
        }
        long now = SystemClock.uptimeMillis();
        if (!removeArmedId.isEmpty() && now - removeArmedAt >= 6000) removeArmedId = "";
        drawBackground(canvas, now);
        float u = unit();
        lastUnit = u;
        float availableLeft = insetLeft + 20f * u;
        float availableRight = getWidth() - insetRight - 20f * u;
        float fullWidth = Math.max(1f, availableRight - availableLeft);
        float width = Math.min(920f * u, fullWidth);
        float left = availableLeft + (fullWidth - width) / 2f;
        float right = left + width;
        for (Integer action : usedActions) {
            if (actionBounds[action] != null) actionBounds[action].setEmpty();
        }
        usedActions.clear();
        drawHeader(canvas, left, right, insetTop + 12f * u, u);

        contentTop = insetTop + 124f * u;
        contentBottom = getHeight() - insetBottom - 12f * u;
        float available = Math.max(1f, contentBottom - contentTop);
        float y = contentTop - scrollY;
        canvas.save();
        canvas.clipRect(0, contentTop, getWidth(), contentBottom);

        y += drawCurrent(canvas, left, y, width, u) + 14f * u;
        float statusHeight = drawStatus(canvas, left, y, width, u);
        if (statusHeight > 0f) y += statusHeight + 14f * u;

        section(canvas, "01 / DRIVER MODE", left, right, y, u);
        y += 39f * u;
        boolean twoColumns = width / u >= 600f;
        float gap = 10f * u;
        if (twoColumns) {
            float columnWidth = (width - gap) / 2f;
            for (int mode = 0; mode < 4; mode += 2) {
                float first = drawModeCard(canvas, mode, left, y, columnWidth, u);
                float second = drawModeCard(canvas, mode + 1, left + columnWidth + gap, y, columnWidth, u);
                y += Math.max(first, second) + gap;
            }
        } else {
            for (int mode = 0; mode < 4; ++mode) {
                y += drawModeCard(canvas, mode, left, y, width, u) + gap;
            }
        }
        y += 5f * u;

        section(canvas, "02 / OPTIONAL CUSTOM PACKAGES", left, right, y, u);
        y += 39f * u;
        y += drawPackageToggle(canvas, left, y, width, u) + 12f * u;
        if (showPackages) {
            float tabWidth = (width - gap) / 2f;
            RectF installedTab = new RectF(left, y, left + tabWidth, y + touch(u));
            RectF downloadTab = new RectF(left + tabWidth + gap, y, right, y + touch(u));
            button(canvas, ACTION_TAB, installedTab, "INSTALLED (" + installed.size() + ")",
                    packageTab == 0 ? BUTTON_PRIMARY : BUTTON_GHOST, !manager.isBusy(), u);
            button(canvas, ACTION_TAB + 1, downloadTab, "DOWNLOADS",
                    packageTab == 1 ? BUTTON_PRIMARY : BUTTON_GHOST, !manager.isBusy(), u);
            y += touch(u) + 12f * u;
            if (packageTab == 0) {
                y += drawImport(canvas, left, y, width, u) + 12f * u;
                if (installed.isEmpty()) {
                    y += drawEmptyInstalled(canvas, left, y, width, u) + 12f * u;
                } else {
                    for (int i = 0; i < installed.size(); ++i) {
                        y += drawInstalledCard(canvas, installed.get(i), i, left, y, width, u) + 10f * u;
                    }
                }
            } else {
                y += drawCompatibility(canvas, left, y, width, u) + 12f * u;
                for (int i = 0; i < visibleCatalog.size(); ++i) {
                    y += drawCatalogCard(canvas, visibleCatalog.get(i), i, left, y, width, u) + 10f * u;
                }
            }
            y += 4f * u;
        }
        y += drawRecovery(canvas, left, y, width, u) + 18f * u;
        canvas.restore();

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
        if (action == ACTION_BACK) {
            listener.onClose();
            return;
        }
        if (action == ACTION_CANCEL) {
            manager.cancel();
            return;
        }
        if (action == ACTION_CLEAR_STATUS) {
            manager.clearStatus();
            return;
        }
        if (action == ACTION_IMPORT) {
            removeArmedId = "";
            manager.openPicker();
            return;
        }
        if (action == ACTION_PACKAGES) {
            showPackages = !showPackages;
            removeArmedId = "";
            detailsId = "";
            updateContentDescription();
            invalidate();
            return;
        }
        if (action == ACTION_BROWSE_DOWNLOADS) {
            packageTab = 1;
            refresh();
            return;
        }
        if (action == ACTION_SHOW_ALL) {
            showAllPackages = !showAllPackages;
            refresh();
            return;
        }
        if (action >= ACTION_MODE && action < ACTION_MODE + 4) {
            int mode = action - ACTION_MODE;
            removeArmedId = "";
            detailsId = "";
            settings.setDriverMode(mode);
            if (mode == 3) {
                showPackages = true;
                packageTab = installed.isEmpty() ? 1 : 0;
            }
            refresh();
            performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
            return;
        }
        if (action >= ACTION_TAB && action < ACTION_TAB + 2) {
            packageTab = action - ACTION_TAB;
            removeArmedId = "";
            detailsId = "";
            refresh();
            return;
        }
        if (manager.isBusy()) return;
        if (action >= ACTION_SELECT && action < ACTION_SELECT + 500) {
            int index = action - ACTION_SELECT;
            if (index >= 0 && index < installed.size()) {
                DriverRepository.Entry entry = installed.get(index);
                settings.setDriverMode(3);
                showPackages = true;
                packageTab = 0;
                manager.select(entry.id);
                removeArmedId = "";
                detailsId = "";
                refresh();
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
            }
            return;
        }
        if (action >= ACTION_REMOVE && action < ACTION_REMOVE + 500) {
            int index = action - ACTION_REMOVE;
            if (index < 0 || index >= installed.size()) return;
            DriverRepository.Entry entry = installed.get(index);
            long now = SystemClock.uptimeMillis();
            if (!removeArmedId.equals(entry.id) || now - removeArmedAt >= 6000) {
                removeArmedId = entry.id;
                removeArmedAt = now;
                manager.clearStatus();
                invalidate();
                postInvalidateDelayed(6100);
                return;
            }
            DriverRepository.Entry selected = manager.selected();
            boolean wasSelected = selected != null && selected.id.equals(entry.id);
            if (manager.remove(entry.id) && wasSelected) settings.setDriverMode(0);
            removeArmedId = "";
            detailsId = "";
            refresh();
            return;
        }
        if (action >= ACTION_DOWNLOAD && action < ACTION_DOWNLOAD + 500) {
            int index = action - ACTION_DOWNLOAD;
            if (index >= 0 && index < visibleCatalog.size()) {
                removeArmedId = "";
                manager.download(visibleCatalog.get(index));
            }
            return;
        }
        if (action >= ACTION_DETAILS && action < ACTION_DETAILS + 500) {
            int index = action - ACTION_DETAILS;
            if (index >= 0 && index < installed.size()) {
                String id = installed.get(index).id;
                detailsId = detailsId.equals(id) ? "" : id;
                removeArmedId = "";
                invalidate();
            }
        }
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (velocityTracker == null) velocityTracker = VelocityTracker.obtain();
        velocityTracker.addMovement(event);
        float y = event.getY();
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                getParent().requestDisallowInterceptTouchEvent(true);
                scroller.forceFinished(true);
                scrolling = false;
                lastTouchY = y;
                requestFocus();
                focusedAction = ACTION_NONE;
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
                // Finish tracker cleanup before BACK can synchronously detach this view.
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
        if (usedActions.isEmpty()) return;
        int index = usedActions.indexOf(focusedAction);
        if (index < 0) index = direction >= 0 ? -1 : 0;
        focusedAction = usedActions.get(Math.floorMod(index + direction, usedActions.size()));
        ensureFocusedVisible();
        invalidate();
    }

    private void ensureFocusedVisible() {
        if (focusedAction < 0 || focusedAction >= actionBounds.length || actionBounds[focusedAction] == null) return;
        RectF bounds = actionBounds[focusedAction];
        float visibleTop = bounds.top + scrollY;
        float visibleBottom = bounds.bottom + scrollY;
        if (bounds.top < contentTop) scrollY = Math.max(0f, visibleTop - contentTop - 12f * lastUnit);
        else if (bounds.bottom > contentBottom) {
            scrollY = Math.min(maxScrollY, visibleBottom - contentBottom + 12f * lastUnit);
        }
    }

    @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_PAGE_UP) {
            scrollY = Math.max(0f, scrollY - 64f * lastUnit);
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_PAGE_DOWN) {
            scrollY = Math.min(maxScrollY, scrollY + 64f * lastUnit);
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_UP || keyCode == KeyEvent.KEYCODE_DPAD_LEFT) {
            focusNext(-1);
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_DOWN || keyCode == KeyEvent.KEYCODE_DPAD_RIGHT
                || keyCode == KeyEvent.KEYCODE_TAB) {
            focusNext(1);
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_CENTER || keyCode == KeyEvent.KEYCODE_ENTER
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
