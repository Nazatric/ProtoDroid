package com.protodroid;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.MotionEvent;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static com.protodroid.ControlKeys.*;

/**
 * Paged, compact PC keyboard. The three pages partition the complete 104-key catalog;
 * modifier latching and binding callbacks remain identical to the original input overlay.
 */
final class TouchKeyboard {
    interface Callback {
        void key(int keycode, boolean down);
        void closed();
        void bindingDone(int first, int second, int mouseButton);
        void feedback();
    }

    private static final int ACTION_NONE = 0;
    private static final int ACTION_CLOSE = 1;
    private static final int ACTION_PAGE_MAIN = 2;
    private static final int ACTION_PAGE_MORE = 3;
    private static final int ACTION_PAGE_NUMPAD = 4;
    // Page actions occupy 2..5; picker commands must stay outside that range.
    private static final int ACTION_CLEAR = 6;
    private static final int ACTION_DONE = 7;

    private static final int PAGE_MAIN = 0;
    private static final int PAGE_MORE = 1;
    private static final int PAGE_NUMPAD = 2;
    private static final int PAGE_MOUSE = 3;
    private static final int ACTION_PAGE_MOUSE = ACTION_PAGE_MAIN + PAGE_MOUSE;
    private static final String[] PAGE_NAMES = {"MAIN", "MORE", "NUMPAD", "MOUSE"};

    private static final List<int[]> MAIN_ROWS = rows(
            keys(ESCAPE, TAB, Q, W, E, R, T, Y, U, I, O, P, BACKSPACE),
            keys(CAPS_LOCK, A, S, D, F, G, H, J, K, L, ENTER),
            keys(SHIFT_LEFT, Z, X, C, V, B, N, M, COMMA, PERIOD, SLASH, SHIFT_RIGHT, UP),
            keys(CTRL_LEFT, ALT_LEFT, SPACE, ALT_RIGHT, CTRL_RIGHT, LEFT, DOWN, RIGHT));

    private static final List<int[]> MORE_ROWS = rows(
            keys(GRAVE, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
                    KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, MINUS, EQUALS),
            keys(F1, F2, F3, F4, F5, F6),
            keys(F7, F8, F9, F10, F11, F12),
            keys(LEFT_BRACKET, RIGHT_BRACKET, BACKSLASH, SEMICOLON, APOSTROPHE,
                    PRINT_SCREEN, SCROLL_LOCK, PAUSE),
            keys(INSERT, HOME, PAGE_UP, DELETE, END, PAGE_DOWN, META_LEFT, META_RIGHT, MENU));

    private static final List<int[]> NUMPAD_ROWS = rows(
            keys(NUM_LOCK, NUMPAD_DIVIDE, NUMPAD_MULTIPLY, NUMPAD_SUBTRACT),
            keys(NUMPAD_7, NUMPAD_8, NUMPAD_9, NUMPAD_ADD),
            keys(NUMPAD_4, NUMPAD_5, NUMPAD_6),
            keys(NUMPAD_1, NUMPAD_2, NUMPAD_3, NUMPAD_ENTER),
            keys(NUMPAD_0, NUMPAD_PERIOD));

    // Negative codes are picker-only mouse actions, never part of the PC keyboard catalog.
    private static final int MOUSE_PICK_LEFT = -101;
    private static final int MOUSE_PICK_RIGHT = -102;
    private static final int MOUSE_PICK_MIDDLE = -103;
    private static final List<int[]> MOUSE_ROWS = rows(
            keys(MOUSE_PICK_LEFT, MOUSE_PICK_RIGHT, MOUSE_PICK_MIDDLE));

    private static final class Cap {
        final int code;
        final RectF rect = new RectF();

        Cap(int code) {
            this.code = code;
        }
    }

    private final Callback callback;
    private final float density;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint.FontMetrics fontMetrics = new Paint.FontMetrics();
    private final Typeface keyType = Typeface.create("sans-serif", Typeface.NORMAL);
    private final Typeface boldType = Typeface.create("sans-serif", Typeface.BOLD);
    private final List<Cap> caps = new ArrayList<>();
    private final RectF panel = new RectF();
    private final RectF closeRect = new RectF();
    private final RectF clearRect = new RectF();
    private final RectF doneRect = new RectF();
    private final RectF[] pageRects = {new RectF(), new RectF(), new RectF(), new RectF()};
    private final Map<Integer, Cap> pointers = new HashMap<>();
    private final Set<Integer> latchedModifiers = new HashSet<>();

    private boolean visible, picker;
    private int page = PAGE_MAIN;
    private int actionPointer = -1, action = ACTION_NONE;
    private int bindingFirst, bindingSecond, bindingMouse;
    private int lastViewWidth, lastViewHeight;

    TouchKeyboard(float density, Callback callback) {
        this.density = density;
        this.callback = callback;
    }

    boolean isVisible() {
        return visible;
    }

    boolean isPicker() {
        return visible && picker;
    }

    void show() {
        releaseAll();
        visible = true;
        picker = false;
        page = PAGE_MAIN;
        bindingFirst = bindingSecond = bindingMouse = 0;
        actionPointer = -1;
        action = ACTION_NONE;
    }

    void showPicker(int first, int second, int mouseButton) {
        releaseAll();
        visible = true;
        picker = true;
        bindingMouse = CustomButtonStore.validMouseButton(mouseButton) ? mouseButton : 0;
        page = bindingMouse != 0 ? PAGE_MOUSE : PAGE_MAIN;
        bindingFirst = bindingMouse == 0 && ControlKeys.isSupported(first) ? first : 0;
        bindingSecond = CustomButtonStore.validBinding(bindingFirst, second, bindingMouse) ? second : 0;
        actionPointer = -1;
        action = ACTION_NONE;
    }

    void hide() {
        releaseAll();
        visible = false;
        picker = false;
        actionPointer = -1;
        action = ACTION_NONE;
    }

    void layout(int viewWidth, int viewHeight) {
        if (viewWidth <= 0 || viewHeight <= 0) return;
        lastViewWidth = viewWidth;
        lastViewHeight = viewHeight;
        float widthCap = (page == PAGE_NUMPAD ? 420f : 780f) * density;
        float widthFraction = picker ? 0.92f : (page == PAGE_NUMPAD ? 0.62f : 0.90f);
        float panelWidth = Math.min(viewWidth * widthFraction, widthCap);
        float coverage = picker ? 0.66f : (page == PAGE_MAIN ? 0.58f : 0.64f);
        float header = (picker ? 72f : 44f) * density;
        List<int[]> rows = rowsForPage();
        float desiredHeight = (picker ? 72f : 44f) + rows.size() * 42f
                + (rows.size() - 1) * 3f + 8f;
        float panelHeight = Math.min(viewHeight * coverage, desiredHeight * density);
        float left = (viewWidth - panelWidth) / 2f;
        float top = picker ? (viewHeight - panelHeight) / 2f : viewHeight - panelHeight - 10f * density;
        panel.set(left, top, left + panelWidth, top + panelHeight);
        layoutHeader(header);

        float gap = 3f * density;
        float contentLeft = panel.left + 6f * density;
        float contentRight = panel.right - 6f * density;
        float contentTop = panel.top + header + 2f * density;
        float contentBottom = panel.bottom - 6f * density;
        float rowHeight = (contentBottom - contentTop - gap * (rows.size() - 1)) / rows.size();
        caps.clear();
        for (int rowIndex = 0; rowIndex < rows.size(); rowIndex++) {
            int[] row = rows.get(rowIndex);
            float units = 0f;
            for (int code : row) units += weight(code);
            float x = contentLeft;
            float y = contentTop + rowIndex * (rowHeight + gap);
            for (int code : row) {
                float cellWidth = (contentRight - contentLeft) * weight(code) / units;
                Cap cap = new Cap(code);
                cap.rect.set(x + density, y + density, x + cellWidth - density, y + rowHeight - density);
                caps.add(cap);
                x += cellWidth;
            }
        }
    }

    private void layoutHeader(float header) {
        closeRect.set(panel.right - 40f * density, panel.top + 4f * density,
                panel.right - 4f * density, panel.top + 40f * density);
        clearRect.setEmpty();
        doneRect.setEmpty();
        for (RectF rect : pageRects) rect.setEmpty();
        int pageCount = pageCount();
        if (picker) {
            doneRect.set(closeRect.left - 62f * density, panel.top + 4f * density,
                    closeRect.left - 6f * density, panel.top + 40f * density);
            clearRect.set(doneRect.left - 62f * density, panel.top + 4f * density,
                    doneRect.left - 6f * density, panel.top + 40f * density);
            float tabsWidth = Math.min(panel.width() - 16f * density, 336f * density);
            float tabsLeft = (panel.left + panel.right - tabsWidth) / 2f;
            float tabWidth = tabsWidth / pageCount;
            for (int i = 0; i < pageCount; i++) {
                pageRects[i].set(tabsLeft + i * tabWidth, panel.top + 42f * density,
                        tabsLeft + (i + 1) * tabWidth - 3f * density, panel.top + 68f * density);
            }
        } else {
            float tabsLeft = panel.left + 70f * density;
            float tabsRight = closeRect.left - 6f * density;
            float tabWidth = Math.max(1f, (tabsRight - tabsLeft) / pageCount);
            for (int i = 0; i < pageCount; i++) {
                pageRects[i].set(tabsLeft + i * tabWidth, panel.top + 7f * density,
                        tabsLeft + (i + 1) * tabWidth - 3f * density, panel.top + 37f * density);
            }
        }
    }

    private List<int[]> rowsForPage() {
        if (page == PAGE_MORE) return MORE_ROWS;
        if (page == PAGE_NUMPAD) return NUMPAD_ROWS;
        if (page == PAGE_MOUSE) return MOUSE_ROWS;
        return MAIN_ROWS;
    }

    private int pageCount() {
        return picker ? 4 : 3;
    }

    private static boolean isMousePick(int code) {
        return code == MOUSE_PICK_LEFT || code == MOUSE_PICK_RIGHT || code == MOUSE_PICK_MIDDLE;
    }

    private static int mouseBit(int code) {
        if (code == MOUSE_PICK_LEFT) return 1;
        if (code == MOUSE_PICK_RIGHT) return 2;
        if (code == MOUSE_PICK_MIDDLE) return 4;
        return 0;
    }

    private static String capLabel(int code) {
        if (code == MOUSE_PICK_LEFT) return "LEFT CLICK";
        if (code == MOUSE_PICK_RIGHT) return "RIGHT CLICK";
        if (code == MOUSE_PICK_MIDDLE) return "MIDDLE CLICK";
        return ControlKeys.keycap(code);
    }

    private static float weight(int code) {
        if (code == SPACE) return 4.8f;
        if (code == BACKSPACE || code == ENTER) return 1.65f;
        if (code == TAB || code == CAPS_LOCK) return 1.35f;
        if (code == SHIFT_LEFT || code == SHIFT_RIGHT) return 1.55f;
        if (code == CTRL_LEFT || code == CTRL_RIGHT || code == ALT_LEFT || code == ALT_RIGHT) return 1.15f;
        if (code == LEFT || code == RIGHT || code == UP || code == DOWN) return 1.25f;
        if (code == NUMPAD_0) return 1.8f;
        return 1f;
    }

    private Cap keyAt(float x, float y) {
        for (int i = caps.size() - 1; i >= 0; --i) {
            Cap cap = caps.get(i);
            if (cap.rect.contains(x, y)) return cap;
        }
        return null;
    }

    private int headerAction(float x, float y) {
        if (closeRect.contains(x, y)) return ACTION_CLOSE;
        if (picker && clearRect.contains(x, y)) return ACTION_CLEAR;
        if (picker && doneRect.contains(x, y)) return ACTION_DONE;
        for (int i = 0; i < pageCount(); i++) if (pageRects[i].contains(x, y)) return ACTION_PAGE_MAIN + i;
        return ACTION_NONE;
    }

    private void chooseBinding(int code) {
        if (isMousePick(code)) {
            bindingFirst = bindingSecond = 0;
            bindingMouse = mouseBit(code);
            return;
        }
        bindingMouse = 0;
        if (code == bindingFirst) {
            bindingFirst = bindingSecond;
            bindingSecond = 0;
        } else if (code == bindingSecond) {
            bindingSecond = 0;
        } else if (bindingFirst == 0) {
            bindingFirst = code;
        } else if (bindingSecond == 0) {
            bindingSecond = code;
        } else {
            bindingSecond = code;
        }
    }

    private void releaseModifiers() {
        for (Integer code : latchedModifiers) callback.key(code, false);
        latchedModifiers.clear();
    }

    void releaseAll() {
        for (Cap cap : pointers.values()) {
            if (!picker && !isMousePick(cap.code) && !ControlKeys.isModifier(cap.code)) callback.key(cap.code, false);
        }
        pointers.clear();
        releaseModifiers();
        actionPointer = -1;
        action = ACTION_NONE;
    }

    private void performHeaderAction(int requested) {
        switch (requested) {
            case ACTION_CLOSE:
                hide();
                callback.closed();
                break;
            case ACTION_CLEAR:
                bindingFirst = bindingSecond = bindingMouse = 0;
                break;
            case ACTION_DONE:
                if (CustomButtonStore.validBinding(bindingFirst, bindingSecond, bindingMouse)) {
                    int first = bindingFirst, second = bindingSecond, mouse = bindingMouse;
                    hide();
                    callback.bindingDone(first, second, mouse);
                }
                break;
            case ACTION_PAGE_MAIN:
            case ACTION_PAGE_MORE:
            case ACTION_PAGE_NUMPAD:
            case ACTION_PAGE_MOUSE:
                releaseAll();
                page = requested - ACTION_PAGE_MAIN;
                layout(lastViewWidth, lastViewHeight);
                break;
            default:
                break;
        }
    }

    boolean onTouch(MotionEvent event, int maskedAction, int id, float x, float y) {
        if (!visible) return false;
        switch (maskedAction) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int header = headerAction(x, y);
                if (header != ACTION_NONE) {
                    if (actionPointer < 0) {
                        actionPointer = id;
                        action = header;
                    }
                    return true;
                }
                Cap cap = keyAt(x, y);
                if (cap != null && !pointers.containsKey(id)) {
                    pointers.put(id, cap);
                    callback.feedback();
                    if (!picker && !isMousePick(cap.code) && !ControlKeys.isModifier(cap.code)) callback.key(cap.code, true);
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                if (id == actionPointer) {
                    int requested = action;
                    actionPointer = -1;
                    action = ACTION_NONE;
                    if (headerAction(x, y) == requested) performHeaderAction(requested);
                    return true;
                }
                Cap cap = pointers.remove(id);
                if (cap == null) return true;
                if (picker) {
                    if (cap.rect.contains(x, y)) chooseBinding(cap.code);
                } else if (ControlKeys.isModifier(cap.code)) {
                    if (cap.rect.contains(x, y)) {
                        if (latchedModifiers.contains(cap.code)) {
                            latchedModifiers.remove(cap.code);
                            callback.key(cap.code, false);
                        } else {
                            latchedModifiers.add(cap.code);
                            callback.key(cap.code, true);
                        }
                    }
                } else {
                    callback.key(cap.code, false);
                    releaseModifiers();
                }
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                return true;
            default:
                return true;
        }
    }

    void draw(Canvas canvas) {
        if (!visible || panel.isEmpty()) return;
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(null);
        paint.setColor(0xf40b0b0d);
        canvas.drawRoundRect(panel, 7f * density, 7f * density, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(1f, density * 0.8f));
        paint.setColor(0x70554b4e);
        canvas.drawRoundRect(panel, 7f * density, 7f * density, paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xffa7292e);
        canvas.drawRect(panel.left + 8f * density, panel.top,
                panel.right - 8f * density, panel.top + 2f * density, paint);

        if (picker) {
            String binding = bindingMouse != 0 ? ControlKeys.mouseLabel(bindingMouse)
                    : bindingFirst == 0 ? "TAP 1 OR 2 KEYS"
                    : ControlKeys.chordLabel(bindingFirst, bindingSecond);
            text(canvas, "BIND  " + binding, panel.left + 12f * density,
                    panel.top + 4f * density, clearRect.left - panel.left - 20f * density,
                    36f * density, 13f, 0xfff0e8e1, false);
            drawAction(canvas, clearRect, "CLEAR", false);
            drawAction(canvas, doneRect, "DONE",
                    CustomButtonStore.validBinding(bindingFirst, bindingSecond, bindingMouse));
        } else {
            text(canvas, "KEYS", panel.left + 12f * density, panel.top + 4f * density,
                    54f * density, 36f * density, 13f, 0xffe8dfd8, false);
        }
        for (int i = 0; i < pageCount(); i++) {
            boolean chosen = page == i;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(chosen ? 0xb33b1115 : 0x00111111);
            canvas.drawRoundRect(pageRects[i], 3f * density, 3f * density, paint);
            textCentered(canvas, PAGE_NAMES[i], pageRects[i], 10.5f,
                    chosen ? 0xfff4e8e2 : 0xffa99e99, chosen);
        }
        drawAction(canvas, closeRect, "×", false);

        for (Cap cap : caps) {
            boolean held = pointers.containsValue(cap) || latchedModifiers.contains(cap.code);
            boolean chosen = picker && (cap.code == bindingFirst || cap.code == bindingSecond
                    || (isMousePick(cap.code) && mouseBit(cap.code) == bindingMouse));
            float radius = 3f * density;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(held || chosen ? 0xe05a171b : 0xed202023);
            canvas.drawRoundRect(cap.rect, radius, radius, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(1f, density * 0.65f));
            paint.setColor(held || chosen ? 0xffd34b50 : 0x6660575a);
            canvas.drawRoundRect(cap.rect, radius, radius, paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(held || chosen ? 0xffee7779 : 0x33111111);
            canvas.drawRect(cap.rect.left + 3f * density, cap.rect.bottom - 2f * density,
                    cap.rect.right - 3f * density, cap.rect.bottom - density, paint);
            String label = capLabel(cap.code);
            float size = label.length() > 5 ? 10f : (label.length() > 3 ? 11f : 14f);
            textCentered(canvas, label, cap.rect, size, held || chosen ? 0xffffffff : 0xffeee7e1, false);
        }
    }

    private void drawAction(Canvas canvas, RectF rect, String label, boolean emphasized) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(emphasized ? 0xa03b1115 : 0x00111111);
        canvas.drawRoundRect(rect, 3f * density, 3f * density, paint);
        textCentered(canvas, label, rect, label.length() > 2 ? 11f : 18f,
                emphasized ? 0xfff4e8e2 : 0xffc5b9b3, false);
    }

    private void textCentered(Canvas canvas, String value, RectF rect, float maxSp,
                              int color, boolean bold) {
        paint.setTypeface(bold ? boldType : keyType);
        paint.setTextAlign(Paint.Align.CENTER);
        float available = Math.max(1f, rect.width() - 7f * density);
        fitText(value, maxSp * density, available);
        paint.setColor(color);
        paint.getFontMetrics(fontMetrics);
        float baseline = rect.centerY() - (fontMetrics.ascent + fontMetrics.descent) / 2f;
        canvas.drawText(value, rect.centerX(), baseline, paint);
        paint.setTextAlign(Paint.Align.LEFT);
    }

    private void text(Canvas canvas, String value, float x, float y, float width, float height,
                      float maxSp, int color, boolean bold) {
        paint.setTypeface(bold ? boldType : keyType);
        paint.setTextAlign(Paint.Align.LEFT);
        fitText(value, maxSp * density, Math.max(1f, width));
        paint.setColor(color);
        paint.getFontMetrics(fontMetrics);
        float baseline = y + height / 2f - (fontMetrics.ascent + fontMetrics.descent) / 2f;
        canvas.drawText(value, x, baseline, paint);
    }

    private void fitText(String value, float initial, float width) {
        paint.setTextSize(initial);
        float measured = paint.measureText(value);
        if (measured > width) paint.setTextSize(Math.max(8.5f * density, initial * width / measured));
    }

    private static int[] keys(int... codes) {
        return codes;
    }

    private static List<int[]> rows(int[]... rows) {
        return Arrays.asList(rows);
    }
}
