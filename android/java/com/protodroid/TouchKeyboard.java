package com.protodroid;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.MotionEvent;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** A canvas-drawn PC keyboard. It is an input overlay, not Android's IME and not a game-screen detector. */
final class TouchKeyboard {
    interface Callback {
        void key(int keycode, boolean down);
        void closed();
        void bindingDone(int first, int second);
        void feedback();
    }

    private static final int RED = 0xffe0261e, RED_DIM = 0xff7a1410;
    private static final int ACTION_NONE = 0, ACTION_CLOSE = 1, ACTION_FULL = 2, ACTION_CLEAR = 3, ACTION_DONE = 4;

    private static final class Item {
        final int code;
        final float width;

        Item(int code, float width) {
            this.code = code;
            this.width = width;
        }
    }

    private static final class KeyBounds {
        final int code;
        final RectF rect = new RectF();

        KeyBounds(int code) {
            this.code = code;
        }
    }

    private final Callback callback;
    private final float density;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), edge = new Paint(Paint.ANTI_ALIAS_FLAG),
            text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF panel = new RectF(), closeRect = new RectF(), fullRect = new RectF(),
            clearRect = new RectF(), doneRect = new RectF();
    private final List<KeyBounds> keys = new ArrayList<>();
    private final Map<Integer, KeyBounds> pointers = new HashMap<>();
    private final Set<Integer> latchedModifiers = new HashSet<>();
    private final List<Item[]> compactRows = new ArrayList<>(), fullRows = new ArrayList<>();
    private boolean visible, picker, full;
    private int actionPointer = -1, action = ACTION_NONE;
    private int bindingFirst, bindingSecond;

    TouchKeyboard(float density, Callback callback) {
        this.density = density;
        this.callback = callback;
        text.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        text.setTextAlign(Paint.Align.CENTER);
        edge.setStyle(Paint.Style.STROKE);
        buildRows();
    }

    private static Item k(int code) {
        return new Item(code, 1);
    }

    private static Item k(int code, float width) {
        return new Item(code, width);
    }

    private static Item gap(float width) {
        return new Item(0, width);
    }

    private void buildRows() {
        compactRows.add(new Item[] {k(ControlKeys.ESCAPE), k(ControlKeys.GRAVE), k(ControlKeys.KEY_1),
                k(ControlKeys.KEY_2), k(ControlKeys.KEY_3), k(ControlKeys.KEY_4), k(ControlKeys.KEY_5),
                k(ControlKeys.KEY_6), k(ControlKeys.KEY_7), k(ControlKeys.KEY_8), k(ControlKeys.KEY_9),
                k(ControlKeys.KEY_0), k(ControlKeys.MINUS), k(ControlKeys.EQUALS), k(ControlKeys.BACKSPACE)});
        compactRows.add(new Item[] {k(ControlKeys.TAB), k(ControlKeys.Q), k(ControlKeys.W), k(ControlKeys.E),
                k(ControlKeys.R), k(ControlKeys.T), k(ControlKeys.Y), k(ControlKeys.U), k(ControlKeys.I),
                k(ControlKeys.O), k(ControlKeys.P), k(ControlKeys.LEFT_BRACKET), k(ControlKeys.RIGHT_BRACKET),
                k(ControlKeys.BACKSLASH), gap(1)});
        compactRows.add(new Item[] {k(ControlKeys.CAPS_LOCK), k(ControlKeys.A), k(ControlKeys.S), k(ControlKeys.D),
                k(ControlKeys.F), k(ControlKeys.G), k(ControlKeys.H), k(ControlKeys.J), k(ControlKeys.K),
                k(ControlKeys.L), k(ControlKeys.SEMICOLON), k(ControlKeys.APOSTROPHE), k(ControlKeys.ENTER), gap(2)});
        compactRows.add(new Item[] {k(ControlKeys.SHIFT_LEFT), k(ControlKeys.Z), k(ControlKeys.X), k(ControlKeys.C),
                k(ControlKeys.V), k(ControlKeys.B), k(ControlKeys.N), k(ControlKeys.M), k(ControlKeys.COMMA),
                k(ControlKeys.PERIOD), k(ControlKeys.SLASH), k(ControlKeys.SHIFT_RIGHT), k(ControlKeys.UP), gap(2)});
        compactRows.add(new Item[] {gap(2), k(ControlKeys.CTRL_LEFT), k(ControlKeys.ALT_LEFT),
                k(ControlKeys.SPACE, 4), k(ControlKeys.ALT_RIGHT), k(ControlKeys.CTRL_RIGHT), k(ControlKeys.LEFT),
                k(ControlKeys.DOWN), k(ControlKeys.RIGHT), gap(2)});

        fullRows.add(new Item[] {k(ControlKeys.ESCAPE), gap(1), k(ControlKeys.F1), k(ControlKeys.F2),
                k(ControlKeys.F3), k(ControlKeys.F4), gap(0.5f), k(ControlKeys.F5), k(ControlKeys.F6),
                k(ControlKeys.F7), k(ControlKeys.F8), gap(0.5f), k(ControlKeys.F9), k(ControlKeys.F10),
                k(ControlKeys.F11), k(ControlKeys.F12), gap(1), k(ControlKeys.PRINT_SCREEN),
                k(ControlKeys.SCROLL_LOCK), k(ControlKeys.PAUSE), gap(3)});
        fullRows.add(new Item[] {k(ControlKeys.GRAVE), k(ControlKeys.KEY_1), k(ControlKeys.KEY_2),
                k(ControlKeys.KEY_3), k(ControlKeys.KEY_4), k(ControlKeys.KEY_5), k(ControlKeys.KEY_6),
                k(ControlKeys.KEY_7), k(ControlKeys.KEY_8), k(ControlKeys.KEY_9), k(ControlKeys.KEY_0),
                k(ControlKeys.MINUS), k(ControlKeys.EQUALS), k(ControlKeys.BACKSPACE), gap(0.5f),
                k(ControlKeys.INSERT), k(ControlKeys.HOME), k(ControlKeys.PAGE_UP), gap(0.5f),
                k(ControlKeys.NUM_LOCK), k(ControlKeys.NUMPAD_DIVIDE), k(ControlKeys.NUMPAD_MULTIPLY),
                k(ControlKeys.NUMPAD_SUBTRACT)});
        fullRows.add(new Item[] {k(ControlKeys.TAB), k(ControlKeys.Q), k(ControlKeys.W), k(ControlKeys.E),
                k(ControlKeys.R), k(ControlKeys.T), k(ControlKeys.Y), k(ControlKeys.U), k(ControlKeys.I),
                k(ControlKeys.O), k(ControlKeys.P), k(ControlKeys.LEFT_BRACKET), k(ControlKeys.RIGHT_BRACKET),
                k(ControlKeys.BACKSLASH), gap(0.5f), k(ControlKeys.DELETE), k(ControlKeys.END),
                k(ControlKeys.PAGE_DOWN), gap(0.5f), k(ControlKeys.NUMPAD_7), k(ControlKeys.NUMPAD_8),
                k(ControlKeys.NUMPAD_9), k(ControlKeys.NUMPAD_ADD)});
        fullRows.add(new Item[] {k(ControlKeys.CAPS_LOCK), k(ControlKeys.A), k(ControlKeys.S), k(ControlKeys.D),
                k(ControlKeys.F), k(ControlKeys.G), k(ControlKeys.H), k(ControlKeys.J), k(ControlKeys.K),
                k(ControlKeys.L), k(ControlKeys.SEMICOLON), k(ControlKeys.APOSTROPHE), k(ControlKeys.ENTER),
                gap(0.5f), gap(3), gap(0.5f), k(ControlKeys.NUMPAD_4), k(ControlKeys.NUMPAD_5),
                k(ControlKeys.NUMPAD_6), gap(2)});
        fullRows.add(new Item[] {k(ControlKeys.SHIFT_LEFT), k(ControlKeys.Z), k(ControlKeys.X), k(ControlKeys.C),
                k(ControlKeys.V), k(ControlKeys.B), k(ControlKeys.N), k(ControlKeys.M), k(ControlKeys.COMMA),
                k(ControlKeys.PERIOD), k(ControlKeys.SLASH), k(ControlKeys.SHIFT_RIGHT), gap(0.5f), gap(1),
                k(ControlKeys.UP), gap(1), gap(0.5f), k(ControlKeys.NUMPAD_1), k(ControlKeys.NUMPAD_2),
                k(ControlKeys.NUMPAD_3), gap(3)});
        fullRows.add(new Item[] {k(ControlKeys.CTRL_LEFT), k(ControlKeys.META_LEFT), k(ControlKeys.ALT_LEFT),
                k(ControlKeys.SPACE, 4), k(ControlKeys.ALT_RIGHT), k(ControlKeys.META_RIGHT), k(ControlKeys.MENU),
                k(ControlKeys.CTRL_RIGHT), gap(0.5f), k(ControlKeys.LEFT), k(ControlKeys.DOWN),
                k(ControlKeys.RIGHT), gap(0.5f), k(ControlKeys.NUMPAD_0, 2), k(ControlKeys.NUMPAD_PERIOD),
                k(ControlKeys.NUMPAD_ENTER), gap(3)});
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
        full = false;
        actionPointer = -1;
        action = ACTION_NONE;
    }

    void showPicker(int first, int second) {
        releaseAll();
        visible = true;
        picker = true;
        full = true;
        bindingFirst = ControlKeys.isSupported(first) ? first : 0;
        bindingSecond = CustomButtonStore.validBinding(bindingFirst, second) ? second : 0;
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
        float horizontal = (picker || full ? 0.992f : 0.94f);
        float width = Math.min(viewWidth * horizontal, (picker || full ? 1120 : 820) * density);
        float height = Math.min(viewHeight * (picker ? 0.95f : 0.80f), (picker ? 410 : 350) * density);
        float left = (viewWidth - width) / 2f;
        float top = picker ? (viewHeight - height) / 2f : viewHeight - height - 5 * density;
        panel.set(left, top, left + width, top + height);

        float header = (picker ? 58 : 36) * density;
        float pad = 4 * density;
        closeRect.set(panel.right - 32 * density, panel.top + 4 * density,
                panel.right - 4 * density, panel.top + 32 * density);
        if (picker) {
            doneRect.set(closeRect.left - 78 * density, panel.top + 5 * density,
                    closeRect.left - 6 * density, panel.top + 33 * density);
            clearRect.set(doneRect.left - 76 * density, doneRect.top, doneRect.left - 6 * density, doneRect.bottom);
            fullRect.setEmpty();
        } else {
            fullRect.set(closeRect.left - 86 * density, panel.top + 5 * density,
                    closeRect.left - 6 * density, panel.top + 33 * density);
            clearRect.setEmpty();
            doneRect.setEmpty();
        }

        keys.clear();
        List<Item[]> rows = picker || full ? fullRows : compactRows;
        float units = picker || full ? 22 : 15;
        float availableHeight = panel.height() - header - pad * 2;
        float rowGap = 3 * density;
        float rowHeight = (availableHeight - rowGap * (rows.size() - 1)) / rows.size();
        float unitWidth = (panel.width() - pad * 2) / units;
        for (int row = 0; row < rows.size(); ++row) {
            float x = panel.left + pad;
            float y = panel.top + header + pad + row * (rowHeight + rowGap);
            for (Item item : rows.get(row)) {
                float itemWidth = item.width * unitWidth;
                if (item.code != 0) {
                    KeyBounds key = new KeyBounds(item.code);
                    key.rect.set(x + 1.2f * density, y, x + itemWidth - 1.2f * density, y + rowHeight);
                    keys.add(key);
                }
                x += itemWidth;
            }
        }
    }

    private KeyBounds keyAt(float x, float y) {
        for (int i = keys.size() - 1; i >= 0; --i) {
            KeyBounds key = keys.get(i);
            if (key.rect.contains(x, y)) return key;
        }
        return null;
    }

    private int headerAction(float x, float y) {
        if (closeRect.contains(x, y)) return ACTION_CLOSE;
        if (fullRect.contains(x, y)) return ACTION_FULL;
        if (clearRect.contains(x, y)) return ACTION_CLEAR;
        if (doneRect.contains(x, y)) return ACTION_DONE;
        return ACTION_NONE;
    }

    private void chooseBinding(int code) {
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
        for (KeyBounds key : pointers.values()) {
            if (!picker && !ControlKeys.isModifier(key.code)) callback.key(key.code, false);
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
            case ACTION_FULL:
                releaseAll();
                full = !full;
                layout(lastViewWidth, lastViewHeight);
                break;
            case ACTION_CLEAR:
                bindingFirst = bindingSecond = 0;
                break;
            case ACTION_DONE:
                if (CustomButtonStore.validBinding(bindingFirst, bindingSecond)) {
                    int first = bindingFirst, second = bindingSecond;
                    hide();
                    callback.bindingDone(first, second);
                }
                break;
            default:
                break;
        }
    }

    private int lastViewWidth, lastViewHeight;

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
                KeyBounds key = keyAt(x, y);
                if (key != null && !pointers.containsKey(id)) {
                    pointers.put(id, key);
                    callback.feedback();
                    if (!picker && !ControlKeys.isModifier(key.code)) callback.key(key.code, true);
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
                KeyBounds key = pointers.remove(id);
                if (key == null) return true;
                if (picker) {
                    if (key.rect.contains(x, y)) chooseBinding(key.code);
                } else if (ControlKeys.isModifier(key.code)) {
                    if (key.rect.contains(x, y)) {
                        if (latchedModifiers.contains(key.code)) {
                            latchedModifiers.remove(key.code);
                            callback.key(key.code, false);
                        } else {
                            latchedModifiers.add(key.code);
                            callback.key(key.code, true);
                        }
                    }
                } else {
                    callback.key(key.code, false);
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

    private void brackets(Canvas canvas, RectF r, float length) {
        canvas.drawLine(r.left, r.top, r.left + length, r.top, edge);
        canvas.drawLine(r.left, r.top, r.left, r.top + length, edge);
        canvas.drawLine(r.right, r.top, r.right - length, r.top, edge);
        canvas.drawLine(r.right, r.top, r.right, r.top + length, edge);
        canvas.drawLine(r.left, r.bottom, r.left + length, r.bottom, edge);
        canvas.drawLine(r.left, r.bottom, r.left, r.bottom - length, edge);
        canvas.drawLine(r.right, r.bottom, r.right - length, r.bottom, edge);
        canvas.drawLine(r.right, r.bottom, r.right, r.bottom - length, edge);
    }

    private void pill(Canvas canvas, RectF rect, String label, boolean active, boolean enabled) {
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(active ? 0xd08a120c : 0xe00b0b0b);
        canvas.drawRect(rect, fill);
        edge.setStrokeWidth(1.3f * density);
        edge.setColor(enabled ? RED : 0xff4b4444);
        brackets(canvas, rect, 7 * density);
        text.setTextSize(12 * density);
        text.setTextAlign(Paint.Align.CENTER);
        text.setColor(enabled ? 0xffffe0dc : 0xff777777);
        canvas.drawText(label, rect.centerX(), rect.centerY() + 4 * density, text);
    }

    private void drawKey(Canvas canvas, KeyBounds key) {
        boolean held = pointers.containsValue(key);
        boolean latched = latchedModifiers.contains(key.code);
        boolean selected = picker && (key.code == bindingFirst || key.code == bindingSecond);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(held || latched || selected ? 0xe09a1a12 : 0xe0141010);
        canvas.drawRect(key.rect, fill);
        edge.setStrokeWidth((selected ? 2.2f : 1.0f) * density);
        edge.setColor(held || latched ? 0xffffb0a6 : selected ? 0xffffffff : RED_DIM);
        canvas.drawRect(key.rect, edge);
        String label = ControlKeys.shortNameOf(key.code);
        float size = Math.min(13 * density, key.rect.height() * 0.36f);
        text.setTextSize(size);
        text.setColor(held || latched || selected ? 0xffffffff : 0xffffd6d0);
        float max = key.rect.width() - 5 * density;
        while (size > 7 * density && text.measureText(label) > max) {
            size -= density;
            text.setTextSize(size);
        }
        canvas.drawText(label, key.rect.centerX(), key.rect.centerY() + size * 0.36f, text);
    }

    void draw(Canvas canvas) {
        if (!visible || panel.isEmpty()) return;
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(0xf4080808);
        canvas.drawRect(panel, fill);
        edge.setStrokeWidth(1.8f * density);
        edge.setColor(RED);
        brackets(canvas, panel, 18 * density);
        text.setTextAlign(Paint.Align.LEFT);
        text.setColor(RED);
        if (picker) {
            text.setTextSize(16 * density);
            canvas.drawText("CUSTOM BUTTON // PC KEYBOARD", panel.left + 12 * density,
                    panel.top + 21 * density, text);
            text.setTextSize(12 * density);
            text.setColor(0xffffd6d0);
            String binding = "1: " + ControlKeys.nameOf(bindingFirst) + "    2: "
                    + (bindingSecond == 0 ? "OPTIONAL" : ControlKeys.nameOf(bindingSecond));
            canvas.drawText(binding, panel.left + 12 * density, panel.top + 42 * density, text);
            pill(canvas, clearRect, "CLEAR", false, true);
            pill(canvas, doneRect, "DONE", bindingFirst != 0, bindingFirst != 0);
        } else {
            text.setTextSize(15 * density);
            canvas.drawText(full ? "PC KEYBOARD // 104 KEYS" : "PC KEYBOARD // TAP MODIFIER, THEN KEY",
                    panel.left + 12 * density, panel.top + 23 * density, text);
            pill(canvas, fullRect, full ? "COMPACT" : "FULL", full, true);
        }
        text.setTextAlign(Paint.Align.CENTER);
        pill(canvas, closeRect, "X", false, true);
        for (KeyBounds key : keys) drawKey(canvas, key);
    }
}
