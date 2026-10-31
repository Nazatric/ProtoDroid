package com.protodroid;

import android.content.Context;
import android.content.res.AssetManager;
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
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * A dedicated, lightweight Prototype-style driver center. It intentionally uses platform Canvas APIs, like SetupView,
 * so driver management adds no Compose/AndroidX runtime or artwork to the APK.
 */
final class DriverView extends View implements DriverManager.Observer {
    interface Listener {
        void onClose();
    }

    private static final int RED = 0xffc9141a;
    private static final int RED_DARK = 0xff6f1017;
    private static final int RED_HOT = 0xffff4038;
    private static final int BONE = 0xffece8e1;
    private static final int DIM = 0xffaaa59e;
    private static final int MUTED = 0xff74747a;
    private static final int OK = 0xff8fe07a;
    private static final int AMBER = 0xffe1b56b;
    private static final int PANEL = 0xe613151b;
    private static final int PANEL_ALT = 0xf01a1b22;

    private static final int ACTION_NONE = 0;
    private static final int ACTION_BACK = 1;
    private static final int ACTION_CANCEL = 2;
    private static final int ACTION_IMPORT = 3;
    private static final int ACTION_CLEAR_STATUS = 4;
    private static final int ACTION_MODE = 10;       // + mode 0..3
    private static final int ACTION_FILTER = 20;     // + filter 0..3
    private static final int ACTION_SELECT = 1000;   // + visible installed index
    private static final int ACTION_REMOVE = 2000;   // + visible installed index
    private static final int ACTION_DOWNLOAD = 3000; // + visible catalog index
    private static final int MAX_ACTION = 4096;

    private static final int BUTTON_NORMAL = 0;
    private static final int BUTTON_PRIMARY = 1;
    private static final int BUTTON_GHOST = 2;

    private static final String[] MODE_NAMES = {"AUTO", "SYSTEM", "TURNIP", "CUSTOM"};
    private static final String[] MODE_SUBTITLES = {"RECOMMENDED", "PHONE DRIVER", "ADRENO", "LIBRARY"};
    private static final String[] FILTER_NAMES = {"ALL", "TURNIP", "PANVK", "INSTALLED"};

    private final Listener listener;
    private final GameSettings settings;
    private final DriverManager manager;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Path path = new Path();
    private final Typeface medium;
    private final Typeface semibold;
    private final Typeface bold;
    private final float density;
    private final float fontScale;
    private final int touchSlop;
    private final OverScroller scroller;
    private final RectF[] actionBounds = new RectF[MAX_ACTION];
    private final ArrayList<Integer> usedActions = new ArrayList<>();
    private final Set<String> catalogInstalledIds = new HashSet<>();
    private final ArrayList<DriverRepository.Entry> installed = new ArrayList<>();
    private final ArrayList<DriverRepository.Entry> visibleInstalled = new ArrayList<>();
    private final ArrayList<DriverCatalog.Entry> visibleCatalog = new ArrayList<>();
    private final boolean maliDevice;

    private int insetLeft, insetTop, insetRight, insetBottom;
    private int pressedAction = ACTION_NONE;
    private float lastTouchY;
    private float scrollY;
    private float maxScrollY;
    private float lastUnit = 1f;
    private boolean scrolling;
    private int filter;
    private String selectedId = "";
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
        maliDevice = new File("/dev/mali0").exists() || new File("/dev/mali").exists()
                || new File("/dev/mali_kbase").exists();
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

    private void refresh() {
        installed.clear();
        installed.addAll(manager.installed());
        DriverRepository.Entry selected = manager.selected();
        selectedId = selected == null ? "" : selected.id;
        catalogInstalledIds.clear();
        visibleInstalled.clear();
        visibleCatalog.clear();
        for (DriverRepository.Entry entry : installed) {
            if (!entry.catalogId().isEmpty()) catalogInstalledIds.add(entry.catalogId());
            if (filter == 3 || filter == 0 || backendFilter(entry.installed.backend)) visibleInstalled.add(entry);
        }
        if (filter != 3) {
            for (DriverCatalog.Entry entry : DriverCatalog.ENTRIES) {
                if (filter == 0 || backendFilter(entry.backend)) visibleCatalog.add(entry);
            }
        }
        updateContentDescription();
        invalidate();
    }

    private boolean backendFilter(String backend) {
        return (filter == 1 && DriverCatalog.TURNIP.equals(backend))
                || (filter == 2 && DriverCatalog.PANVK.equals(backend));
    }

    @Override public void onDriverStateChanged() {
        String status = manager.statusTitle();
        boolean busy = manager.isBusy();
        if (!status.equals(observedStatus) || busy != observedBusy) {
            observedStatus = status;
            observedBusy = busy;
            refresh();
        } else {
            // Progress updates need a redraw, not another pass over every package's metadata.
            updateContentDescription();
            invalidate();
        }
    }

    private void updateContentDescription() {
        String status = manager.statusTitle();
        setContentDescription("Prototype GPU drivers. " + MODE_NAMES[settings.driverMode()]
                + " mode. " + installed.size() + " installed custom packages. "
                + (status.isEmpty() ? "Scroll for downloads, import, compatibility, and recovery information."
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

    private Typeface font(int weight) {
        if (weight >= 700) return bold;
        if (weight >= 600) return semibold;
        return medium;
    }

    private TextPaint configure(TextPaint target, float size, int weight, int color, float u) {
        target.setTypeface(font(weight));
        target.setTextSize(size * u * fontScale);
        target.setColor(color);
        target.setLetterSpacing(size == 0f ? 0f : 0.02f);
        target.setStyle(Paint.Style.FILL);
        return target;
    }

    private float baseline(float top, float lineHeight, TextPaint paint) {
        Paint.FontMetrics metrics = paint.getFontMetrics();
        return top + (lineHeight - (metrics.descent - metrics.ascent)) / 2f - metrics.ascent;
    }

    private void drawLine(Canvas canvas, String value, float x, float top, float lineHeight, float size,
                          int weight, int color, float u, float maxWidth) {
        if (value == null) return;
        configure(text, size, weight, color, u);
        String shown = value;
        if (maxWidth > 0 && text.measureText(value) > maxWidth) {
            float ellipsis = text.measureText("…");
            int count = text.breakText(value, true, Math.max(0, maxWidth - ellipsis), null);
            shown = value.substring(0, Math.max(0, count)) + "…";
        }
        canvas.drawText(shown, x, baseline(top, lineHeight, text), text);
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
        // A malformed content provider can supply a very long name; keep the screen's cache bounded.
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
        canvas.drawPath(cutPath(bounds, 7f * u), paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1f, u * 0.65f));
        paint.setColor(edge);
        canvas.drawPath(cutPath(bounds, 7f * u), paint);
        paint.setStyle(Paint.Style.FILL);
    }

    private void bind(int action, RectF bounds) {
        if (action <= ACTION_NONE || action >= MAX_ACTION) return;
        if (actionBounds[action] == null) actionBounds[action] = new RectF();
        actionBounds[action].set(bounds);
        usedActions.add(action);
    }

    private void bind(int action, float left, float top, float right, float bottom) {
        RectF bounds = new RectF(left, top, right, bottom);
        bind(action, bounds);
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
        int edge = variant == BUTTON_PRIMARY ? RED_HOT : (pressed ? RED_HOT : 0xff4c3c42);
        int fill = variant == BUTTON_PRIMARY ? (pressed ? 0xffa91822 : RED_DARK)
                : variant == BUTTON_GHOST ? 0x5010141b : (pressed ? 0xff352027 : 0xff202027);
        if (!enabled) {
            edge = 0xff303039;
            fill = 0xff15161b;
        }
        panel(canvas, bounds, fill, edge, u);
        if (enabled) bind(action, bounds);
        drawLine(canvas, label, bounds.left + 8f * u, bounds.top, bounds.height(), 11f, 700,
                enabled ? BONE : MUTED, u, bounds.width() - 16f * u);
    }

    private void section(Canvas canvas, String title, float left, float right, float top, float u) {
        drawLine(canvas, title, left, top, 22f * u, 12f, 700, RED_HOT, u, right - left);
        paint.setColor(0xff4c282e);
        canvas.drawRect(left, top + 25f * u, right, top + 26f * u, paint);
        paint.setColor(RED);
        canvas.drawRect(left, top + 25f * u, left + 38f * u, top + 27f * u, paint);
    }

    private void drawBackground(Canvas canvas) {
        int width = getWidth(), height = getHeight();
        if (backgroundShade == null || backgroundWidth != width || backgroundHeight != height) {
            backgroundWidth = width;
            backgroundHeight = height;
            backgroundShade = new LinearGradient(0, 0, width, height,
                    new int[]{0xff08090d, 0xff111018, 0xff230e16}, null, Shader.TileMode.CLAMP);
        }
        paint.setShader(backgroundShade);
        canvas.drawRect(0, 0, width, height, paint);
        paint.setShader(null);
        paint.setColor(0x18c9141a);
        paint.setStrokeWidth(1f);
        for (int x = -height; x < width + height; x += Math.max(28, width / 12)) {
            canvas.drawLine(x, 0, x - height, height, paint);
        }
        paint.setColor(0xffc9141a);
        canvas.drawRect(0, 0, width, Math.max(2f, density * 0.6f), paint);
    }

    private void drawHeader(Canvas canvas, float left, float right, float top, float u) {
        drawLine(canvas, "[", left, top, 34f * u, 24f, 500, RED_HOT, u, -1);
        drawLine(canvas, "PROTOTYPE", left + 13f * u, top, 34f * u, 22f, 700, BONE, u, -1);
        drawLine(canvas, "]", left + 142f * u, top, 34f * u, 24f, 500, RED_HOT, u, -1);
        RectF back = new RectF(right - 82f * u, top + 2f * u, right, top + 34f * u);
        button(canvas, ACTION_BACK, back, "BACK", BUTTON_GHOST, true, u);
        drawLine(canvas, "GPU DRIVERS", left, top + 43f * u, 32f * u, 26f, 700, BONE, u, right - left);
        drawLine(canvas, "A private Vulkan library for Prototype  •  changes apply on the next Play",
                left, top + 76f * u, 18f * u, 11f, 500, DIM, u, right - left);
    }

    private String modeBody(int mode) {
        switch (mode) {
            case 1:
                return "Uses Android's installed Vulkan driver exactly as the phone provides it. This is the normal "
                        + "Mali baseline and a useful comparison on Adreno. No custom package is loaded.";
            case 2:
                return "Uses ProtoDroid's bundled open-source Adreno driver policy. Turnip is for Adreno, not Mali: "
                        + "on Mali this mode refuses Turnip and safely uses System. No download is needed.";
            case 3:
                return "Uses only the installed package you select below. A separate probe checks it before gameplay; "
                        + "a failed probe falls back to System and records the reason. Downloading never selects Custom.";
            default:
                return "Recommended. ProtoDroid chooses its bundled Adreno policy on supported Qualcomm GPUs and the "
                        + "phone's system driver on Mali or unsupported devices. No download is needed.";
        }
    }

    private String modeNote(int mode) {
        if (maliDevice && mode == 2) return "Mali detected: Turnip will not be forced. System is the safe fallback.";
        if (maliDevice && mode == 3) return "Mali detected: choose a PanVK-kbase package below, or keep Auto/System for the stable baseline.";
        if (maliDevice) return "Mali detected: Auto and System keep the phone's working driver path.";
        return "Auto is the default. Custom is always an explicit choice; your global Android driver is never replaced.";
    }

    private void drawModeExplanation(Canvas canvas, float left, float top, float width, float u) {
        int mode = settings.driverMode();
        String body = modeBody(mode);
        String note = modeNote(mode);
        float inner = width - 28f * u;
        StaticLayout bodyLayout = paragraph(body, inner, 12f, 500, BONE, u);
        StaticLayout noteLayout = paragraph(note, inner, 11f, 600, maliDevice && mode == 2 ? AMBER : DIM, u);
        float height = 28f * u + bodyLayout.getHeight() + 7f * u + noteLayout.getHeight() + 14f * u;
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, PANEL, mode == 3 ? RED : 0xff3c3038, u);
        drawLine(canvas, MODE_NAMES[mode] + "  /  WHAT HAPPENS", left + 14f * u, top + 9f * u,
                18f * u, 11f, 700, mode == 3 ? RED_HOT : DIM, u, inner);
        drawParagraph(canvas, body, left + 14f * u, top + 31f * u, inner, 12f, 500, BONE, u);
        drawParagraph(canvas, note, left + 14f * u, top + 31f * u + bodyLayout.getHeight() + 7f * u,
                inner, 11f, 600, maliDevice && mode == 2 ? AMBER : DIM, u);
    }

    private void drawStatus(Canvas canvas, float left, float top, float width, float u) {
        String title = manager.statusTitle();
        if (title.isEmpty()) return;
        String message = manager.statusMessage();
        boolean busy = manager.isBusy();
        boolean cancel = busy && manager.operationTitle().startsWith("Downloading");
        float buttonWidth = cancel ? 86f * u : (!busy ? 76f * u : 0f);
        float inner = width - 28f * u - buttonWidth;
        StaticLayout messageLayout = paragraph(message, inner, 11f, 500, BONE, u);
        float progressHeight = manager.progressTotal() > 0 ? 19f * u : 0f;
        float height = Math.max(62f * u, 29f * u + messageLayout.getHeight() + progressHeight + 14f * u);
        RectF card = new RectF(left, top, left + width, top + height);
        boolean good = title.contains("installed") || title.contains("selected") || title.contains("removed");
        panel(canvas, card, PANEL_ALT, good ? 0xff55734e : (busy ? RED : 0xff70414a), u);
        drawLine(canvas, title.toUpperCase(Locale.ROOT), left + 14f * u, top + 8f * u, 18f * u,
                11f, 700, good ? OK : RED_HOT, u, inner);
        drawParagraph(canvas, message, left + 14f * u, top + 29f * u, inner, 11f, 500, BONE, u);
        if (cancel) {
            RectF cancelBounds = new RectF(left + width - 96f * u, top + 12f * u,
                    left + width - 12f * u, top + 42f * u);
            button(canvas, ACTION_CANCEL, cancelBounds, "CANCEL", BUTTON_GHOST, true, u);
        } else if (!busy) {
            RectF clearBounds = new RectF(left + width - 88f * u, top + 12f * u,
                    left + width - 12f * u, top + 42f * u);
            button(canvas, ACTION_CLEAR_STATUS, clearBounds, "DISMISS", BUTTON_GHOST, true, u);
        }
        if (manager.progressTotal() > 0) {
            float barLeft = left + 14f * u;
            float barRight = left + width - 14f * u - buttonWidth;
            float barTop = top + height - 22f * u;
            paint.setColor(0xff35303a);
            canvas.drawRect(barLeft, barTop, barRight, barTop + 5f * u, paint);
            float fraction = Math.max(0f, Math.min(1f, manager.progressDone() / (float) manager.progressTotal()));
            paint.setColor(RED_HOT);
            canvas.drawRect(barLeft, barTop, barLeft + (barRight - barLeft) * fraction, barTop + 5f * u, paint);
            drawLine(canvas, String.format(Locale.ROOT, "%.1f / %.1f MiB  •  %d%%",
                    manager.progressDone() / 1048576.0, manager.progressTotal() / 1048576.0, Math.round(fraction * 100)),
                    barLeft, barTop - 16f * u, 14f * u, 10f, 600, DIM, u, barRight - barLeft);
        } else if (busy) {
            drawLine(canvas, "VALIDATING / WORKING…", left + 14f * u, top + height - 22f * u,
                    16f * u, 10f, 600, DIM, u, inner);
            postInvalidateDelayed(180);
        }
    }

    private float statusHeight(float width, float u) {
        if (manager.statusTitle().isEmpty()) return 0f;
        boolean cancel = manager.isBusy() && manager.operationTitle().startsWith("Downloading");
        float buttonWidth = cancel ? 86f * u : (!manager.isBusy() ? 76f * u : 0f);
        float inner = width - 28f * u - buttonWidth;
        StaticLayout message = paragraph(manager.statusMessage(), inner, 11f, 500, BONE, u);
        float progress = manager.progressTotal() > 0 ? 19f * u : 0f;
        return Math.max(62f * u, 29f * u + message.getHeight() + progress + 14f * u) + 12f * u;
    }

    private String backendLabel(String backend) {
        return DriverCatalog.PANVK.equals(backend) ? "PANVK  /  DIRECT ICD" : "TURNIP  /  ADRENOTOOLS";
    }

    private boolean isSelected(DriverRepository.Entry entry) {
        return settings.driverMode() == 3 && selectedId.equals(entry.id);
    }

    private void drawInstalledCard(Canvas canvas, DriverRepository.Entry entry, int index, float left, float top,
                                   float width, float u) {
        DriverPackage.Installed item = entry.installed;
        boolean selected = isSelected(entry);
        boolean narrow = width / u < 560f;
        float buttons = narrow ? 0f : 112f * u;
        float inner = width - 28f * u - buttons;
        String detail = (item.version.isEmpty() ? "Custom package" : item.version) + "  •  "
                + backendLabel(item.backend) + "\nSource: " + item.source;
        StaticLayout detailLayout = paragraph(detail, inner, 11f, 500, DIM, u);
        float contentHeight = 22f * u + detailLayout.getHeight() + 8f * u;
        float height = Math.max(74f * u, contentHeight + 22f * u) + (narrow ? 35f * u : 0f);
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, selected ? 0xf02b1b23 : PANEL, selected ? RED_HOT : 0xff3c3038, u);
        drawLine(canvas, item.name, left + 14f * u, top + 9f * u, 20f * u, 15f, 700, BONE, u, inner);
        drawParagraph(canvas, detail, left + 14f * u, top + 33f * u, inner, 11f, 500, DIM, u);
        if (selected) {
            drawLine(canvas, "SELECTED", left + 14f * u, top + height - (narrow ? 53f : 22f) * u,
                    16f * u, 10f, 700, OK, u, inner);
        }
        float buttonTop = narrow ? top + height - 32f * u : top + 12f * u;
        float buttonLeft = narrow ? left + 14f * u : left + width - 112f * u;
        float buttonWidth = narrow ? (width - 36f * u) / 2f : 98f * u;
        RectF select = new RectF(buttonLeft, buttonTop, buttonLeft + buttonWidth, buttonTop + 26f * u);
        button(canvas, ACTION_SELECT + index, select, selected ? "SELECTED" : "SELECT",
                selected ? BUTTON_NORMAL : BUTTON_PRIMARY, !selected && !manager.isBusy(), u);
        RectF remove = new RectF(buttonLeft + (narrow ? buttonWidth + 8f * u : 0f),
                narrow ? buttonTop : buttonTop + 32f * u,
                narrow ? buttonLeft + buttonWidth * 2f + 8f * u : buttonLeft + buttonWidth,
                (narrow ? buttonTop : buttonTop + 32f * u) + 26f * u);
        boolean armed = removeArmedId.equals(entry.id) && SystemClock.uptimeMillis() - removeArmedAt < 6000;
        button(canvas, ACTION_REMOVE + index, remove, armed ? "CONFIRM" : "REMOVE", BUTTON_GHOST,
                !manager.isBusy(), u);
    }

    private float installedCardHeight(DriverRepository.Entry entry, float width, float u) {
        boolean narrow = width / u < 560f;
        float inner = width - 28f * u - (narrow ? 0f : 112f * u);
        DriverPackage.Installed item = entry.installed;
        String detail = (item.version.isEmpty() ? "Custom package" : item.version) + "  •  "
                + backendLabel(item.backend) + "\nSource: " + item.source;
        StaticLayout detailLayout = paragraph(detail, inner, 11f, 500, DIM, u);
        return Math.max(74f * u, 22f * u + detailLayout.getHeight() + 30f * u) + (narrow ? 35f * u : 0f) + 8f * u;
    }

    private boolean catalogInstalled(DriverCatalog.Entry entry) {
        return catalogInstalledIds.contains(entry.id);
    }

    private void drawCatalogCard(Canvas canvas, DriverCatalog.Entry entry, int index, float left, float top,
                                 float width, float u) {
        boolean narrow = width / u < 560f;
        float buttons = narrow ? 0f : 116f * u;
        float inner = width - 28f * u - buttons;
        String detail = entry.compatibility + "\nGitHub  •  " + entry.source + "  •  "
                + String.format(Locale.ROOT, "%.2f MiB", entry.size / 1048576.0);
        StaticLayout description = paragraph(entry.description, inner, 11f, 500, BONE, u);
        StaticLayout detailLayout = paragraph(detail, inner, 10f, 600, DIM, u);
        float height = Math.max(102f * u, 34f * u + description.getHeight() + 6f * u
                + detailLayout.getHeight() + 17f * u) + (narrow ? 35f * u : 0f);
        RectF card = new RectF(left, top, left + width, top + height);
        panel(canvas, card, PANEL, DriverCatalog.PANVK.equals(entry.backend) ? 0xff66503a : 0xff3c3038, u);
        drawLine(canvas, entry.title, left + 14f * u, top + 8f * u, 21f * u, 15f, 700, BONE, u, inner);
        drawParagraph(canvas, entry.description, left + 14f * u, top + 33f * u, inner, 11f, 500, BONE, u);
        drawParagraph(canvas, detail, left + 14f * u, top + 33f * u + description.getHeight() + 6f * u,
                inner, 10f, 600, DIM, u);
        float buttonTop = narrow ? top + height - 32f * u : top + 14f * u;
        float buttonLeft = narrow ? left + 14f * u : left + width - 116f * u;
        float buttonWidth = narrow ? width - 28f * u : 102f * u;
        RectF get = new RectF(buttonLeft, buttonTop, buttonLeft + buttonWidth, buttonTop + 30f * u);
        button(canvas, ACTION_DOWNLOAD + index, get, catalogInstalled(entry) ? "REINSTALL" : "DOWNLOAD",
                BUTTON_PRIMARY, !manager.isBusy(), u);
        if (DriverCatalog.PANVK.equals(entry.backend)) {
            drawLine(canvas, "EXPERIMENTAL", left + 14f * u, top + height - (narrow ? 51f : 20f) * u,
                    15f * u, 9f, 700, AMBER, u, inner);
        }
    }

    private float catalogCardHeight(DriverCatalog.Entry entry, float width, float u) {
        boolean narrow = width / u < 560f;
        float inner = width - 28f * u - (narrow ? 0f : 116f * u);
        StaticLayout description = paragraph(entry.description, inner, 11f, 500, BONE, u);
        String detail = entry.compatibility + "\nGitHub  •  " + entry.source + "  •  "
                + String.format(Locale.ROOT, "%.2f MiB", entry.size / 1048576.0);
        StaticLayout detailLayout = paragraph(detail, inner, 10f, 600, DIM, u);
        return Math.max(102f * u, 34f * u + description.getHeight() + 6f * u
                + detailLayout.getHeight() + 17f * u) + (narrow ? 35f * u : 0f) + 8f * u;
    }

    private void drawImportAndRecovery(Canvas canvas, float left, float top, float width, float u) {
        String importBody = "Import an AdrenoTools ZIP, .adpkg ZIP, or raw ARM64 Android/Bionic .so. "
                + "Unsafe paths, Linux/glibc, x86, wrong-ABI, damaged, and incompatible packages are rejected before installation.";
        StaticLayout importLayout = paragraph(importBody, width - 160f * u, 11f, 500, DIM, u);
        float importHeight = Math.max(68f * u, importLayout.getHeight() + 32f * u);
        RectF importCard = new RectF(left, top, left + width, top + importHeight);
        panel(canvas, importCard, PANEL_ALT, 0xff3c3038, u);
        drawLine(canvas, "IMPORT YOUR OWN", left + 14f * u, top + 8f * u, 18f * u, 11f, 700, RED_HOT, u,
                width - 160f * u);
        drawParagraph(canvas, importBody, left + 14f * u, top + 29f * u, width - 160f * u, 11f, 500, DIM, u);
        RectF importButton = new RectF(left + width - 132f * u, top + 14f * u,
                left + width - 14f * u, top + 44f * u);
        button(canvas, ACTION_IMPORT, importButton, "IMPORT ZIP / .SO", BUTTON_NORMAL, !manager.isBusy(), u);

        float recoveryTop = top + importHeight + 10f * u;
        String recovery = "A failed custom probe uses System and logs the reason. A startup crash is guarded on the next "
                + "launch; see proto-native.prev.log. Turnip is never forced on Mali. Return to Auto at any time. "
                + "Packages are private to ProtoDroid and never replace the phone's global driver.";
        StaticLayout recoveryLayout = paragraph(recovery, width - 28f * u, 11f, 500, DIM, u);
        float recoveryHeight = recoveryLayout.getHeight() + 40f * u;
        RectF recoveryCard = new RectF(left, recoveryTop, left + width, recoveryTop + recoveryHeight);
        panel(canvas, recoveryCard, 0xb011141b, 0xff343039, u);
        drawLine(canvas, "SAFE RECOVERY", left + 14f * u, recoveryTop + 8f * u, 18f * u, 11f, 700, OK, u,
                width - 28f * u);
        drawParagraph(canvas, recovery, left + 14f * u, recoveryTop + 29f * u, width - 28f * u, 11f, 500, DIM, u);
    }

    private float importRecoveryHeight(float width, float u) {
        StaticLayout importLayout = paragraph("Import an AdrenoTools ZIP, .adpkg ZIP, or raw ARM64 Android/Bionic .so. "
                + "Unsafe paths, Linux/glibc, x86, wrong-ABI, damaged, and incompatible packages are rejected before installation.",
                width - 160f * u, 11f, 500, DIM, u);
        String recovery = "A failed custom probe uses System and logs the reason. A startup crash is guarded on the next "
                + "launch; see proto-native.prev.log. Turnip is never forced on Mali. Return to Auto at any time. "
                + "Packages are private to ProtoDroid and never replace the phone's global driver.";
        StaticLayout recoveryLayout = paragraph(recovery, width - 28f * u, 11f, 500, DIM, u);
        return Math.max(68f * u, importLayout.getHeight() + 32f * u) + 10f * u + recoveryLayout.getHeight() + 40f * u;
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (scroller.computeScrollOffset()) {
            scrollY = scroller.getCurrY();
            invalidate();
        }
        drawBackground(canvas);
        float u = unit();
        lastUnit = u;
        float left = insetLeft + 20f * u;
        float right = getWidth() - insetRight - 20f * u;
        float width = Math.max(1f, right - left);
        for (Integer action : usedActions) {
            if (actionBounds[action] != null) actionBounds[action].setEmpty();
        }
        usedActions.clear();
        drawHeader(canvas, left, right, insetTop + 12f * u, u);

        float contentTop = insetTop + 108f * u;
        float contentBottom = getHeight() - insetBottom - 12f * u;
        float available = Math.max(1f, contentBottom - contentTop);
        float y = contentTop - scrollY;
        canvas.save();
        canvas.clipRect(0, contentTop, getWidth(), contentBottom);

        section(canvas, "DRIVER MODE", left, right, y, u);
        y += 35f * u;
        float gap = 6f * u;
        float modeWidth = (width - gap * 3f) / 4f;
        for (int mode = 0; mode < 4; ++mode) {
            RectF bounds = new RectF(left + mode * (modeWidth + gap), y,
                    left + mode * (modeWidth + gap) + modeWidth, y + 43f * u);
            boolean selected = settings.driverMode() == mode;
            panel(canvas, bounds, selected ? 0xff3c1b24 : PANEL, selected ? RED_HOT : 0xff49353e, u);
            bind(ACTION_MODE + mode, bounds);
            drawLine(canvas, MODE_NAMES[mode], bounds.left + 8f * u, bounds.top + 5f * u, 18f * u,
                    13f, 700, selected ? BONE : DIM, u, bounds.width() - 16f * u);
            drawLine(canvas, MODE_SUBTITLES[mode], bounds.left + 8f * u, bounds.top + 24f * u, 14f * u,
                    8f, 600, selected ? RED_HOT : MUTED, u, bounds.width() - 16f * u);
        }
        y += 51f * u;
        String body = modeBody(settings.driverMode());
        String note = modeNote(settings.driverMode());
        float explanationHeight = 28f * u + paragraph(body, width - 28f * u, 12f, 500, BONE, u).getHeight()
                + 7f * u + paragraph(note, width - 28f * u, 11f, 600, DIM, u).getHeight() + 14f * u;
        drawModeExplanation(canvas, left, y, width, u);
        y += explanationHeight + 16f * u;

        float statusHeight = statusHeight(width, u);
        if (statusHeight > 0) {
            drawStatus(canvas, left, y, width, u);
            y += statusHeight;
        }

        section(canvas, "DRIVER LIBRARY", left, right, y, u);
        y += 34f * u;
        float filterWidth = (width - gap * 3f) / 4f;
        for (int i = 0; i < 4; ++i) {
            RectF bounds = new RectF(left + i * (filterWidth + gap), y,
                    left + i * (filterWidth + gap) + filterWidth, y + 29f * u);
            button(canvas, ACTION_FILTER + i, bounds, FILTER_NAMES[i],
                    filter == i ? BUTTON_PRIMARY : BUTTON_GHOST, !manager.isBusy(), u);
        }
        y += 40f * u;

        if (filter == 0 || filter == 3 || !visibleInstalled.isEmpty()) {
            drawLine(canvas, "INSTALLED PACKAGES", left, y, 19f * u, 11f, 700, DIM, u, width);
            y += 23f * u;
            if (visibleInstalled.isEmpty()) {
                String empty = filter == 3 ? "No custom packages are installed yet. Auto and System still work; download a pinned package below or import your own."
                        : "No installed package matches this filter. Choose ALL, or download a compatible package below.";
                StaticLayout emptyLayout = paragraph(empty, width - 28f * u, 11f, 500, DIM, u);
                RectF emptyCard = new RectF(left, y, left + width, y + emptyLayout.getHeight() + 24f * u);
                panel(canvas, emptyCard, PANEL, 0xff343039, u);
                drawParagraph(canvas, empty, left + 14f * u, y + 12f * u, width - 28f * u, 11f, 500, DIM, u);
                y += emptyCard.height() + 8f * u;
            } else {
                for (int i = 0; i < visibleInstalled.size(); ++i) {
                    DriverRepository.Entry entry = visibleInstalled.get(i);
                    drawInstalledCard(canvas, entry, i, left, y, width, u);
                    y += installedCardHeight(entry, width, u);
                }
            }
        }

        if (filter != 3) {
            drawLine(canvas, "AVAILABLE FROM GITHUB  /  PINNED + SHA-256 VERIFIED", left, y + 5f * u,
                    19f * u, 11f, 700, DIM, u, width);
            y += 29f * u;
            for (int i = 0; i < visibleCatalog.size(); ++i) {
                DriverCatalog.Entry entry = visibleCatalog.get(i);
                drawCatalogCard(canvas, entry, i, left, y, width, u);
                y += catalogCardHeight(entry, width, u);
            }
        }

        y += 5f * u;
        drawImportAndRecovery(canvas, left, y, width, u);
        y += importRecoveryHeight(width, u) + 18f * u;
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
            paint.setColor(0x503c3038);
            canvas.drawRect(right + 5f * u, trackTop, right + 7f * u, trackBottom, paint);
            float thumbHeight = Math.max(28f * u, (trackBottom - trackTop) * available / contentHeight);
            float thumbY = trackTop + (trackBottom - trackTop - thumbHeight) * scrollY / maxScrollY;
            paint.setColor(RED);
            canvas.drawRect(right + 5f * u, thumbY, right + 7f * u, thumbY + thumbHeight, paint);
        }
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
        if (action >= ACTION_MODE && action < ACTION_MODE + 4) {
            removeArmedId = "";
            settings.setDriverMode(action - ACTION_MODE);
            refresh();
            performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
            return;
        }
        if (action >= ACTION_FILTER && action < ACTION_FILTER + 4) {
            filter = action - ACTION_FILTER;
            scrollY = 0f;
            refresh();
            return;
        }
        if (manager.isBusy()) return;
        if (action >= ACTION_SELECT && action < ACTION_SELECT + 500) {
            int index = action - ACTION_SELECT;
            if (index >= 0 && index < visibleInstalled.size()) {
                DriverRepository.Entry entry = visibleInstalled.get(index);
                settings.setDriverMode(3);
                manager.select(entry.id);
                removeArmedId = "";
                refresh();
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
            }
            return;
        }
        if (action >= ACTION_REMOVE && action < ACTION_REMOVE + 500) {
            int index = action - ACTION_REMOVE;
            if (index < 0 || index >= visibleInstalled.size()) return;
            DriverRepository.Entry entry = visibleInstalled.get(index);
            long now = SystemClock.uptimeMillis();
            if (!removeArmedId.equals(entry.id) || now - removeArmedAt >= 6000) {
                removeArmedId = entry.id;
                removeArmedAt = now;
                manager.clearStatus();
                invalidate();
                return;
            }
            DriverRepository.Entry selected = manager.selected();
            boolean wasSelected = selected != null && selected.id.equals(entry.id);
            if (manager.remove(entry.id) && wasSelected) settings.setDriverMode(0);
            removeArmedId = "";
            refresh();
            return;
        }
        if (action >= ACTION_DOWNLOAD && action < ACTION_DOWNLOAD + 500) {
            int index = action - ACTION_DOWNLOAD;
            if (index >= 0 && index < visibleCatalog.size()) {
                removeArmedId = "";
                manager.download(visibleCatalog.get(index));
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
                if (scrolling) {
                    velocityTracker.computeCurrentVelocity(1000, ViewConfiguration.get(getContext()).getScaledMaximumFlingVelocity());
                    scroller.fling(0, Math.round(scrollY), 0, Math.round(-velocityTracker.getYVelocity()),
                            0, 0, 0, Math.round(maxScrollY));
                    invalidate();
                } else {
                    int action = actionAt(event.getX(), y);
                    if (action != ACTION_NONE && action == pressedAction) {
                        performClick();
                        activate(action);
                    }
                }
                pressedAction = ACTION_NONE;
                scrolling = false;
                velocityTracker.recycle();
                velocityTracker = null;
                invalidate();
                return true;
            case MotionEvent.ACTION_CANCEL:
                pressedAction = ACTION_NONE;
                scrolling = false;
                velocityTracker.recycle();
                velocityTracker = null;
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

    @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_DPAD_UP || keyCode == KeyEvent.KEYCODE_PAGE_UP) {
            scrollY = Math.max(0f, scrollY - 64f * lastUnit);
            invalidate();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_DPAD_DOWN || keyCode == KeyEvent.KEYCODE_PAGE_DOWN) {
            scrollY = Math.min(maxScrollY, scrollY + 64f * lastUnit);
            invalidate();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override protected void onDetachedFromWindow() {
        if (velocityTracker != null) {
            velocityTracker.recycle();
            velocityTracker = null;
        }
        scroller.forceFinished(true);
        super.onDetachedFromWindow();
    }
}
