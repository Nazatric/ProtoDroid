package com.protodroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.hardware.input.InputManager;
import android.view.HapticFeedbackConstants;
import android.os.Build;
import android.text.InputType;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * On-screen controls drawn over the game in the style of Prototype's interface (red neon on black, hexagons,
 * bracket corners). Everything goes into SDL as real keyboard and mouse input, so the engine sees it both as window
 * messages and through DirectInput.
 *
 * Modes (button in the top-left corner):
 *  - menu: a touch is a left mouse click at that point (menus are mouse-driven), plus Enter and Esc;
 *  - game: a stick on the left (WASD; optionally sprints at the rim), the action buttons in an arc around the
 *    big attack button under the right thumb, dragging anywhere else turns the camera.
 * The gear opens the settings panel; from there the layout editor lets the player drag every control to where
 * their thumbs rest. The performance overlay shows FPS, frame time and memory.
 */
public class TouchControls extends View implements InputManager.InputDeviceListener {
    // Performance counters and the frame limiter of the native host (main.cpp).
    static native float nativeFps();
    static native float nativeFrameMs();
    static native float nativeFrameP99Ms();
    static native int nativeMemoryMiB();
    static native void nativeSetFpsLimit(int fps);
    static native void nativeSetMenuFpsUnlocked(boolean unlocked);

    private static final int MOUSE_LEFT = 1, MOUSE_RIGHT = 2, MOUSE_MIDDLE = 4;
    private static final int ACTION_TOGGLE_NUMBERS = -1, ACTION_SHOW_KEYBOARD = -2;
    private static final int RED = 0xffe0261e, RED_DIM = 0xff7a1410, INK = 0xff0b0b0b;

    /**
     * A hexagonal button: a key (Android keycode) or a mouse button. Its default place is a dp offset from a corner
     * (right/bottom when the flags say so), scaled with the buttons so a cluster stays tight; the layout editor
     * replaces it with a position in fractions of the view.
     */
    private static final class Button {
        String label;
        final String defaultLabel;
        boolean labelEdited;
        int keycode;
        int secondKeycode;
        long customId;
        int builtInIndex = -1;
        boolean custom, hidden;
        int mouseButton;
        final boolean fromRight, fromBottom;
        final float dx, dy, sizeDp;
        final boolean gameOnly;
        float customX = -1, customY = -1;
        float size = 1;  // the layout editor's per-button size factor
        boolean quickPower;  // the extra buttons: shown only with the "Extra buttons" setting
        final boolean numberKey, numberToggle, keyboard;
        // POWERS and DISGUISE hold a selection wheel open: sliding the finger that holds the button moves the mouse,
        // which is how the game picks an entry, so one thumb is enough. fx/fy: where that finger is.
        final boolean wheel;
        float fx, fy;
        float cx, cy, r;
        int pointer = -1;

        Button(String label, int keycode, int mouseButton, boolean fromRight, float dx, boolean fromBottom, float dy,
                float sizeDp, boolean gameOnly) {
            this.label = this.defaultLabel = label;
            this.keycode = keycode;
            this.mouseButton = mouseButton;
            this.fromRight = fromRight;
            this.dx = dx;
            this.fromBottom = fromBottom;
            this.dy = dy;
            this.sizeDp = sizeDp;
            this.gameOnly = gameOnly;
            this.numberKey = keycode == KeyEvent.KEYCODE_1 || keycode == KeyEvent.KEYCODE_2
                    || keycode == KeyEvent.KEYCODE_3 || keycode == KeyEvent.KEYCODE_4;
            this.numberToggle = keycode == ACTION_TOGGLE_NUMBERS;
            this.keyboard = keycode == ACTION_SHOW_KEYBOARD;
            this.quickPower = keycode == KeyEvent.KEYCODE_F || keycode == KeyEvent.KEYCODE_1
                    || keycode == KeyEvent.KEYCODE_2 || keycode == KeyEvent.KEYCODE_3 || keycode == KeyEvent.KEYCODE_4
                    || keycode == KeyEvent.KEYCODE_DPAD_DOWN || keycode == KeyEvent.KEYCODE_F1
                    || keycode == KeyEvent.KEYCODE_F2;
            this.wheel = keycode == KeyEvent.KEYCODE_R || keycode == KeyEvent.KEYCODE_F;
        }

        Button(CustomButtonStore.Entry entry) {
            this.defaultLabel = ControlKeys.bindingLabel(entry.key1, entry.key2, entry.mouseButton);
            String customLabel = ControlKeys.cleanLabel(entry.label);
            this.labelEdited = !customLabel.isEmpty();
            this.label = labelEdited ? customLabel : defaultLabel;
            this.keycode = entry.key1;
            this.secondKeycode = entry.key2;
            this.customId = entry.id;
            this.custom = true;
            this.hidden = entry.hidden;
            this.mouseButton = entry.mouseButton;
            this.fromRight = true;
            this.dx = 130;
            this.fromBottom = true;
            this.dy = 290;
            this.sizeDp = 28;
            this.gameOnly = false;
            this.numberKey = this.numberToggle = this.keyboard = false;
            this.quickPower = this.wheel = false;
            this.customX = entry.x;
            this.customY = entry.y;
            this.size = entry.size;
        }

        CustomButtonStore.Entry toEntry() {
            CustomButtonStore.Entry entry = new CustomButtonStore.Entry(customId, keycode, secondKeycode);
            entry.mouseButton = mouseButton;
            entry.x = customX;
            entry.y = customY;
            entry.size = size;
            entry.hidden = hidden;
            entry.label = labelEdited ? label : "";
            return entry;
        }

        String bindingText() {
            if (numberToggle) return "1-4 PAD";
            if (keyboard) return "PC KEYS";
            return ControlKeys.bindingLabel(keycode, secondKeycode, mouseButton);
        }

        boolean hit(float x, float y) {
            float dx = x - cx, dy = y - cy;
            return dx * dx + dy * dy <= r * r * 1.25f;
        }
    }

    // Right thumb: ATTACK in the corner, the inner arc (radius ~105 dp around it) for SPECIAL / GRAB / JUMP, the
    // outer arc (~190 dp) for the rarer TARGET and SPRINT. Menu keys, POWERS and DISGUISE along the top right edge,
    // the quick powers below them, clear of the health bar.
    private final Button[] buttons = {
        new Button("ENTER", KeyEvent.KEYCODE_ENTER, 0, true, 280, false, 33, 24, false),
        new Button("ESC", KeyEvent.KEYCODE_ESCAPE, 0, true, 100, false, 33, 24, false),
        new Button("ATTACK", 0, MOUSE_LEFT, true, 78, true, 74, 46, true),
        new Button("SPECIAL", 0, MOUSE_RIGHT, true, 183, true, 62, 33, true),
        new Button("GRAB", KeyEvent.KEYCODE_E, 0, true, 158, true, 150, 31, true),
        new Button("JUMP", KeyEvent.KEYCODE_SPACE, 0, true, 66, true, 180, 33, true),
        new Button("TARGET", KeyEvent.KEYCODE_TAB, 0, true, 258, true, 140, 27, true),  // lock-on is Tab on PC
        new Button("SPRINT", KeyEvent.KEYCODE_SHIFT_LEFT, 0, true, 168, true, 245, 27, true),
        new Button("POWERS", KeyEvent.KEYCODE_R, 0, true, 190, false, 33, 24, true),  // Power Select Menu: R
        // Extra buttons use the reference port's PC Keyboard Setup keys: disguise powers menu, last attack,
        // defense and sensory power; then last disguise power, Web of Intrigue, take a disguise and back to Alex.
        // Appended so every saved position above keeps its index. Non-number extras remain hidden unless enabled;
        // the numbered power keys can also be opened with the compact 123 control for detector/QTE number prompts.
        new Button("DSG MENU", KeyEvent.KEYCODE_F, 0, true, 370, false, 33, 24, true),
        new Button("1 ATK", KeyEvent.KEYCODE_1, 0, true, 280, false, 95, 22, true),
        new Button("3 DEF", KeyEvent.KEYCODE_3, 0, true, 190, false, 95, 22, true),
        new Button("4 SENSE", KeyEvent.KEYCODE_4, 0, true, 100, false, 95, 22, true),
        new Button("2 DSG", KeyEvent.KEYCODE_2, 0, true, 370, false, 95, 22, true),
        new Button("WEB", KeyEvent.KEYCODE_DPAD_DOWN, 0, true, 460, false, 33, 22, true),
        new Button("DISGUISE", KeyEvent.KEYCODE_F1, 0, true, 460, false, 95, 22, true),
        new Button("ALEX", KeyEvent.KEYCODE_F2, 0, true, 550, false, 95, 22, true),
        // New controls are appended as well: existing saved positions and sizes retain their original indices.
        new Button("123", ACTION_TOGGLE_NUMBERS, 0, false, 145, false, 33, 22, true),
        new Button("ABC", ACTION_SHOW_KEYBOARD, 0, false, 210, false, 33, 22, false),
        new Button("DESCEND", KeyEvent.KEYCODE_V, 0, true, 258, true, 215, 25, true),
    };
    private final List<Button> customButtons = new ArrayList<>();
    private final List<Button> allButtons = new ArrayList<>();
    private long nextCustomId = 1;

    // ---- settings ----
    private final SharedPreferences prefs;
    private final GameSettings settingsStore;
    private final GameSettings.Setting[] settings;
    private final boolean layoutOnly;
    private float opacity, scale;
    private boolean vibrate, showFps, hideWithPad, autoRun, invertY, powerButtons;
    private float camGain;
    private InputManager inputManager;
    private boolean padConnected;
    private boolean panelOpen = false;
    // Pointer that opened the mod panel, so the UP of that same finger (which lands outside the panel) does not close it
    // again. The v1.1 archive's TouchControls.java uses this field but no longer declares it, which made javac fail with
    // "cannot find symbol: variable panelOpener" for the whole APK.
    private int panelOpener = -1;
    private final RectF panel = new RectF(), closeRect = new RectF(), layoutRect = new RectF();

    // ---- layout editor ----
    private boolean editing = false;
    private final RectF editorBar = new RectF(), selectionBar = new RectF();
    private final RectF doneRect = new RectF(), resetRect = new RectF(), addRect = new RectF(), stickRect = new RectF();
    private final RectF smallerRect = new RectF(), biggerRect = new RectF();
    private final RectF textRect = new RectF(), bindRect = new RectF(), hideRect = new RectF(), removeRect = new RectF();
    private Button selected;          // the button (or stick) the size buttons act on
    private boolean selectedStick, removeArmed;
    // MENU/GAME and the settings gear are controls too: the editor moves and resizes them like a button, and their
    // geometry is persisted next to the button layout. chrome: 0 none, 1 the MENU/GAME pill, 2 the gear.
    private int chrome, chromeDrag = -1;
    private float modeCustomX = -1, modeCustomY = -1, modeSize = 1;
    private float gearCustomX = -1, gearCustomY = -1, gearSize = 1;
    private Button bindingTarget;
    private boolean bindingNew;
    private float stickSize = 1;
    private Button dragButton;
    private boolean dragStick;
    private int dragPointer = -1;
    private float dragDx, dragDy;
    private boolean snapXActive, snapYActive;
    private float snapGuideX, snapGuideY;
    private long snapTargetKey = Long.MIN_VALUE;
    private float stickCustomX = -1, stickCustomY = -1;
    private boolean stickHidden;
    private final List<RectF> leftArrows = new ArrayList<>(), rightArrows = new ArrayList<>();

    // ---- drawing ----
    private final float density;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), edge = new Paint(Paint.ANTI_ALIAS_FLAG),
            glow = new Paint(Paint.ANTI_ALIAS_FLAG), text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final TouchKeyboard keyboard;
    private final TouchKeyState keyState;
    private boolean gameMode = false;
    private boolean numberPad = false;
    private final RectF modeRect = new RectF(), gearRect = new RectF();
    private int modePointer = -1, gearPointer = -1;

    // stick
    private float stickHomeX, stickHomeY, stickX, stickY, stickR, knobX, knobY;
    private int stickPointer = -1;
    private final boolean[] dirDown = new boolean[4];  // W A S D
    private boolean stickRun, runButtonHeld, shiftDown;  // Shift comes from the rim of the stick or SPRINT
    private static final int[] DIR_KEYS = {KeyEvent.KEYCODE_W, KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_D};

    // camera / menu pointer
    private int lookPointer = -1;
    private float lookX, lookY, lastMouseX, lastMouseY;
    private int mouseState = 0;
    private final Map<Integer, Button> pointerButton = new HashMap<>();

    public TouchControls(Context context) {
        this(context, false);
    }

    /** Standalone launcher editing shares preferences but never starts or configures the SDL runtime. */
    public TouchControls(Context context, boolean layoutOnly) {
        super(context);
        this.layoutOnly = layoutOnly;
        density = context.getResources().getDisplayMetrics().density;
        prefs = context.getSharedPreferences("controls", Context.MODE_PRIVATE);
        settingsStore = new GameSettings(context);
        settings = settingsStore.gameplaySettings;
        for (int i = 0; i < settings.length; ++i) {
            leftArrows.add(new RectF());
            rightArrows.add(new RectF());
        }
        keyState = new TouchKeyState((keycode, down) -> {
            if (down) SDLActivity.onNativeKeyDown(keycode);
            else SDLActivity.onNativeKeyUp(keycode);
        });
        keyboard = new TouchKeyboard(density, new TouchKeyboard.Callback() {
            @Override public void key(int keycode, boolean down) {
                TouchControls.this.key(keycode, down);
            }

            @Override public void closed() {
                bindingTarget = null;
                bindingNew = false;
                invalidate();
            }

            @Override public void bindingDone(int first, int second, int mouseButton) {
                finishBinding(first, second, mouseButton);
            }

            @Override public void feedback() {
                buzz();
            }
        });
        loadLayout();
        applyUiSettings();
        if (!layoutOnly) applyLiveSettings();
        if (layoutOnly) setBackgroundColor(0xff080808);
        Typeface face = Typeface.create("sans-serif-condensed", Typeface.BOLD);
        text.setTypeface(face);
        text.setTextAlign(Paint.Align.CENTER);
        text.setLetterSpacing(0.08f);
        edge.setStyle(Paint.Style.STROKE);
        glow.setStyle(Paint.Style.STROKE);
        glow.setMaskFilter(new BlurMaskFilter(6 * density, BlurMaskFilter.Blur.NORMAL));
        setHapticFeedbackEnabled(true);
        inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
        if (inputManager != null) {
            inputManager.registerInputDeviceListener(this, null);
            padConnected = findGamepad(inputManager);
        }
    }

    // Glowing shapes are rendered once into bitmaps (blur needs the software pipeline) and then only blitted.
    private final Map<String, Bitmap> cache = new HashMap<>();
    private final Paint blit = new Paint(Paint.FILTER_BITMAP_FLAG);

    private interface Painter {
        void paint(Canvas c, float cx, float cy);
    }

    private Bitmap cached(String key, float halfSize, Painter painter) {
        Bitmap b = cache.get(key);
        if (b == null) {
            int n = (int) Math.ceil(halfSize * 2);
            b = Bitmap.createBitmap(n, n, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(b);
            float saved = opacity;
            opacity = 1f;  // opacity is applied when blitting
            painter.paint(c, n / 2f, n / 2f);
            opacity = saved;
            cache.put(key, b);
        }
        return b;
    }

    private void blitCentered(Canvas c, Bitmap b, float cx, float cy) {
        blit.setAlpha(Math.round(255 * opacity));
        c.drawBitmap(b, cx - b.getWidth() / 2f, cy - b.getHeight() / 2f, blit);
    }

    private void applyUiSettings() {
        cache.clear();
        opacity = settingsStore.valueOf("ui_opacity") / 100f;
        scale = settingsStore.valueOf("ui_scale") / 100f;
        vibrate = settingsStore.valueOf("ui_vibrate") != 0;
        showFps = settingsStore.valueOf("ui_fps") != 0;
        hideWithPad = settingsStore.valueOf("ui_pad_hide") != 0;
        autoRun = settingsStore.valueOf("ui_stick_run") != 0;
        camGain = settingsStore.valueOf("ui_cam") / 100f;
        invertY = settingsStore.valueOf("ui_invert") != 0;
        powerButtons = settingsStore.valueOf("ui_power_buttons") != 0;
        layoutButtons(getWidth(), getHeight());
    }

    private void applyLiveSettings() {
        int requested = settingsStore.valueOf("fps_limit");
        boolean batterySaver = settingsStore.valueOf("battery_saver") != 0;
        int effective = batterySaver && (requested == 0 || requested > 30) ? 30 : requested;
        nativeSetFpsLimit(effective);
        nativeSetMenuFpsUnlocked(settingsStore.valueOf("unlock_menu_fps") != 0);
    }

    private static void defaultCustomGeometry(CustomButtonStore.Entry entry, int index) {
        entry.x = 0.40f + (index % 4) * 0.10f;
        entry.y = 0.40f + ((index / 4) % 3) * 0.14f;
    }

    private void rebuildAllButtons() {
        allButtons.clear();
        for (Button b : buttons) allButtons.add(b);
        allButtons.addAll(customButtons);
    }

    private void loadLayout() {
        // These indices are the original persistence contract. New controls never enter this array.
        for (int i = 0; i < buttons.length; i++) {
            Button b = buttons[i];
            b.builtInIndex = i;
            b.customX = prefs.getFloat("pos" + i + "x", -1);
            b.customY = prefs.getFloat("pos" + i + "y", -1);
            b.size = prefs.getFloat("size" + i, 1);
            b.hidden = prefs.getBoolean("hide" + i, false);
            String label = ControlKeys.cleanLabel(prefs.getString("label" + i, ""));
            b.labelEdited = !label.isEmpty();
            b.label = b.labelEdited ? label : b.defaultLabel;
        }
        stickCustomX = prefs.getFloat("stick_x", -1);
        stickCustomY = prefs.getFloat("stick_y", -1);
        stickSize = prefs.getFloat("stick_size", 1);
        stickHidden = prefs.getBoolean("stick_hidden", false);
        modeCustomX = prefs.getFloat("mode_x", -1);
        modeCustomY = prefs.getFloat("mode_y", -1);
        modeSize = prefs.getFloat("mode_size", 1);
        gearCustomX = prefs.getFloat("gear_x", -1);
        gearCustomY = prefs.getFloat("gear_y", -1);
        gearSize = prefs.getFloat("gear_size", 1);
        customButtons.clear();
        long maxId = 0;
        List<CustomButtonStore.Entry> entries = CustomButtonStore.decode(
                prefs.getString(CustomButtonStore.PREFERENCE, "[]"));
        for (int i = 0; i < entries.size(); ++i) {
            CustomButtonStore.Entry entry = entries.get(i);
            if (entry.x < 0 || entry.y < 0) defaultCustomGeometry(entry, i);
            customButtons.add(new Button(entry));
            maxId = Math.max(maxId, entry.id);
        }
        nextCustomId = Math.max(maxId + 1, prefs.getLong("custom_next_id", 1));
        if (nextCustomId <= 0) nextCustomId = maxId + 1;
        rebuildAllButtons();
    }

    private void saveCustomButtons() {
        List<CustomButtonStore.Entry> entries = new ArrayList<>();
        for (Button b : customButtons) entries.add(b.toEntry());
        prefs.edit().putString(CustomButtonStore.PREFERENCE, CustomButtonStore.encode(entries))
                .putLong("custom_next_id", nextCustomId).apply();
    }

    private void saveLayout() {
        int w = Math.max(1, getWidth()), h = Math.max(1, getHeight());
        SharedPreferences.Editor ed = prefs.edit();
        for (int i = 0; i < buttons.length; i++) {
            Button b = buttons[i];
            ed.putFloat("size" + i, b.size).putBoolean("hide" + i, b.hidden);
            if (b.labelEdited) ed.putString("label" + i, b.label);
            else ed.remove("label" + i);
            if (b.customX < 0) continue;
            ed.putFloat("pos" + i + "x", b.cx / w).putFloat("pos" + i + "y", b.cy / h);
        }
        ed.putFloat("stick_size", stickSize).putBoolean("stick_hidden", stickHidden);
        if (stickCustomX >= 0) ed.putFloat("stick_x", stickHomeX / w).putFloat("stick_y", stickHomeY / h);
        ed.putFloat("mode_size", modeSize).putFloat("gear_size", gearSize);
        if (modeCustomX >= 0) ed.putFloat("mode_x", modeRect.left / w).putFloat("mode_y", modeRect.top / h);
        if (gearCustomX >= 0) ed.putFloat("gear_x", gearRect.left / w).putFloat("gear_y", gearRect.top / h);
        ed.apply();
        saveCustomButtons();
    }

    private void resetLayout() {
        SharedPreferences.Editor ed = prefs.edit();
        for (int i = 0; i < buttons.length; i++) {
            buttons[i].customX = buttons[i].customY = -1;
            buttons[i].size = 1;
            buttons[i].hidden = false;
            buttons[i].label = buttons[i].defaultLabel;
            buttons[i].labelEdited = false;
            ed.remove("pos" + i + "x").remove("pos" + i + "y").remove("size" + i)
                    .remove("hide" + i).remove("label" + i);
        }
        for (int i = 0; i < customButtons.size(); ++i) {
            CustomButtonStore.Entry entry = customButtons.get(i).toEntry();
            defaultCustomGeometry(entry, i);
            customButtons.get(i).customX = entry.x;
            customButtons.get(i).customY = entry.y;
            customButtons.get(i).size = 1;
            customButtons.get(i).hidden = false;
            customButtons.get(i).labelEdited = false;
            customButtons.get(i).label = customButtons.get(i).bindingText();
        }
        stickCustomX = stickCustomY = -1;
        stickSize = 1;
        stickHidden = false;
        modeCustomX = modeCustomY = gearCustomX = gearCustomY = -1;
        modeSize = gearSize = 1;
        selected = null;
        selectedStick = false;
        chrome = 0;
        chromeDrag = -1;
        removeArmed = false;
        ed.remove("stick_x").remove("stick_y").remove("stick_size").remove("stick_hidden")
                .remove("mode_x").remove("mode_y").remove("mode_size")
                .remove("gear_x").remove("gear_y").remove("gear_size").apply();
        saveCustomButtons();
        cache.clear();
        layoutButtons(getWidth(), getHeight());
    }

    // ---- gamepads ----
    // A real controller has gamepad buttons and a stick (joystick axes). Phones list built-in devices that claim
    // SOURCE_GAMEPAD too (Xiaomi: fingerprint sensor, side keys), which used to hide the touch controls with no
    // controller anywhere; those have no joystick axes, and on Android 10+ they are not external.
    private static boolean findGamepad(InputManager im) {
        for (int id : im.getInputDeviceIds()) {
            InputDevice d = im.getInputDevice(id);
            if (d == null || d.isVirtual()) continue;
            int sources = d.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) != InputDevice.SOURCE_GAMEPAD
                    || (sources & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK) continue;
            if (d.getMotionRange(MotionEvent.AXIS_X, InputDevice.SOURCE_JOYSTICK) == null
                    || d.getMotionRange(MotionEvent.AXIS_Y, InputDevice.SOURCE_JOYSTICK) == null) continue;
            if (Build.VERSION.SDK_INT >= 29 && !d.isExternal()) continue;
            return true;
        }
        return false;
    }

    private void updatePads() {
        InputManager im = (InputManager) getContext().getSystemService(Context.INPUT_SERVICE);
        if (im != null) padConnected = findGamepad(im);
        invalidate();
    }

    @Override public void onInputDeviceAdded(int id) { updatePads(); }
    @Override public void onInputDeviceRemoved(int id) { updatePads(); }
    @Override public void onInputDeviceChanged(int id) { updatePads(); }

    private boolean controlsHidden() {
        return hideWithPad && padConnected;
    }

    // ---- layout ----
    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        releaseAll();
        dragPointer = modePointer = gearPointer = -1;
        dragButton = null;
        dragStick = false;
        chromeDrag = -1;
        clearSnap();
        removeArmed = false;
        layoutButtons(w, h);
        lastMouseX = w / 2f;
        lastMouseY = h / 2f;
    }

    private void layoutButtons(int w, int h) {
        final float u = density * scale;
        for (Button b : allButtons) {
            b.r = b.sizeDp * u * b.size;
            if (b.customX >= 0) {
                b.cx = b.customX * w;
                b.cy = b.customY * h;
            } else {
                b.cx = b.fromRight ? w - b.dx * u : b.dx * u;
                b.cy = b.fromBottom ? h - b.dy * u : b.dy * u;
            }
        }
        stickR = 62 * u * stickSize;
        if (stickCustomX >= 0) {
            stickHomeX = stickCustomX * w;
            stickHomeY = stickCustomY * h;
        } else {
            stickHomeX = 225 * u;  // right of the stock HUD
            stickHomeY = h - 112 * u;
        }
        stickX = knobX = stickHomeX;
        stickY = knobY = stickHomeY;
        // Defaults reproduce the stock corners (14,14 + 96x38 for MENU/GAME; a 44 dp square for the gear); the editor
        // stores a position in fractions of the view and a size factor instead.
        float modeW = 96 * density * modeSize, modeH = 38 * density * modeSize;
        float modeX = modeCustomX >= 0 ? modeCustomX * w : 14 * density;
        float modeY = modeCustomY >= 0 ? modeCustomY * h : 14 * density;
        modeRect.set(modeX, modeY, modeX + modeW, modeY + modeH);
        float gearUnit = 44 * density * gearSize;
        float gearX = gearCustomX >= 0 ? gearCustomX * w : w - 58 * density;
        float gearY = gearCustomY >= 0 ? gearCustomY * h : 12 * density;
        gearRect.set(gearX, gearY, gearX + gearUnit, gearY + gearUnit);
        float pw = Math.min(w * 0.92f, 860 * density), ph = Math.min(h * 0.94f, 500 * density);
        panel.set((w - pw) / 2, (h - ph) / 2, (w + pw) / 2, (h + ph) / 2);
        keyboard.layout(w, h);
    }

    // ---- input to SDL ----
    private void key(int keycode, boolean down) {
        keyState.set(keycode, down);
    }

    private void mouseButton(int bit, boolean down, float x, float y) {
        if (down) mouseState |= bit;
        else mouseState &= ~bit;
        int action = down ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP;
        if (gameMode) {
            // SDL sends a motion with every button: in game mode a zero relative one, as an absolute position would
            // jerk the camera (the cursor is wherever the relative camera drags left it)
            SDLActivity.onNativeMouse(mouseState, action, 0, 0, true);
            return;
        }
        lastMouseX = x;
        lastMouseY = y;
        SDLActivity.onNativeMouse(mouseState, action, x, y, false);
    }

    private void mouseMoveTo(float x, float y) {
        lastMouseX = x;
        lastMouseY = y;
        SDLActivity.onNativeMouse(mouseState, MotionEvent.ACTION_MOVE, x, y, false);
    }

    private float moveRestX, moveRestY;  // SDL takes whole pixels: carry the fractions

    private void mouseMoveBy(float dx, float dy) {
        dx += moveRestX;
        dy += moveRestY;
        int ix = (int) dx, iy = (int) dy;
        moveRestX = dx - ix;
        moveRestY = dy - iy;
        if (ix != 0 || iy != 0) SDLActivity.onNativeMouse(mouseState, MotionEvent.ACTION_MOVE, ix, iy, true);
    }

    private void buzz() {
        if (vibrate) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }

    private void press(Button b, int pointer) {
        if (b.numberToggle) {
            releaseAll();
            numberPad = !numberPad;
            buzz();
            return;
        }
        if (b.keyboard) {
            releaseAll();
            buzz();
            keyboard.show();
            keyboard.layout(getWidth(), getHeight());
            return;
        }
        b.pointer = pointer;
        pointerButton.put(pointer, b);
        buzz();
        if (b.custom) {
            if (b.mouseButton != 0) {
                mouseButton(b.mouseButton, true, lastMouseX, lastMouseY);
            } else {
                key(b.keycode, true);
                if (b.secondKeycode != 0) key(b.secondKeycode, true);
            }
        } else if (b.mouseButton != 0) mouseButton(b.mouseButton, true, lastMouseX, lastMouseY);
        else if (b.keycode == KeyEvent.KEYCODE_SHIFT_LEFT) setRun(stickRun, true);
        else key(b.keycode, true);
    }

    private void release(Button b) {
        pointerButton.remove(b.pointer);
        b.pointer = -1;
        if (b.custom) {
            if (b.mouseButton != 0) {
                mouseButton(b.mouseButton, false, lastMouseX, lastMouseY);
            } else {
                if (b.secondKeycode != 0) key(b.secondKeycode, false);
                key(b.keycode, false);
            }
        } else if (b.mouseButton != 0) mouseButton(b.mouseButton, false, lastMouseX, lastMouseY);
        else if (b.keycode == KeyEvent.KEYCODE_SHIFT_LEFT) setRun(stickRun, false);
        else key(b.keycode, false);
    }

    private void setRun(boolean fromStick, boolean fromButton) {
        stickRun = fromStick;
        runButtonHeld = fromButton;
        boolean want = stickRun || runButtonHeld;
        if (want != shiftDown) {
            shiftDown = want;
            key(KeyEvent.KEYCODE_SHIFT_LEFT, want);
        }
    }

    private void updateStick(float x, float y) {
        float dx = x - stickX, dy = y - stickY;
        float len = (float) Math.sqrt(dx * dx + dy * dy);
        if (len > stickR) {
            dx *= stickR / len;
            dy *= stickR / len;
        }
        knobX = stickX + dx;
        knobY = stickY + dy;
        float dead = stickR * 0.3f;
        boolean[] want = {dy < -dead, dx < -dead, dy > dead, dx > dead};
        for (int i = 0; i < 4; i++) {
            if (want[i] != dirDown[i]) {
                dirDown[i] = want[i];
                key(DIR_KEYS[i], want[i]);
            }
        }
        // Pushed to the rim (with hysteresis): sprint, as with Shift held.
        boolean run = autoRun && stickPointer >= 0 && len > stickR * (stickRun ? 0.85f : 0.95f);
        if (run != stickRun) setRun(run, runButtonHeld);
    }

    private void releaseStick() {
        stickPointer = -1;
        updateStick(stickX, stickY);
        stickX = knobX = stickHomeX;
        stickY = knobY = stickHomeY;
    }

    private void releaseAll() {
        keyboard.releaseAll();
        for (Button b : allButtons) if (b.pointer >= 0) release(b);
        if (stickPointer >= 0) releaseStick();
        setRun(false, false);
        if (lookPointer >= 0 && !gameMode) mouseButton(MOUSE_LEFT, false, lookX, lookY);
        lookPointer = -1;
        keyState.releaseAll();  // fail closed after rotation/cancellation or an interrupted editor transition
    }

    private boolean visible(Button b) {
        if (editing) return true;  // hidden and optional controls must remain recoverable/editable
        if (b.hidden) return false;
        if (b.numberKey && !numberPad && !powerButtons) return false;
        if (b.quickPower && !b.numberKey && !powerButtons) return false;  // optional screen-space extras
        return !controlsHidden() && (gameMode || !b.gameOnly);
    }

    // ---- layout editor ----
    private void startEditing() {
        keyboard.hide();
        releaseAll();
        bindingTarget = null;
        bindingNew = false;
        removeArmed = false;
        clearSnap();
        panelOpen = false;
        chrome = 0;
        chromeDrag = -1;
        editing = true;
    }

    void openLayoutEditor() {
        startEditing();
        invalidate();
    }

    /** @return true when Back only closed the picker; otherwise the host may finish after the layout is saved. */
    boolean handleBack() {
        if (keyboard.isVisible()) {
            keyboard.hide();
            bindingTarget = null;
            bindingNew = false;
            invalidate();
            return true;
        }
        if (editing) saveLayout();
        return false;
    }

    private void finishEditing() {
        saveLayout();
        removeArmed = false;
        chrome = 0;
        chromeDrag = -1;
        buzz();
        if (layoutOnly) {
            // Stay in the editor until the activity is gone; a final normal frame must not touch SDL/FPS natives.
            if (getContext() instanceof Activity) ((Activity) getContext()).finish();
            return;
        }
        editing = false;
    }

    private void startBinding(Button target) {
        releaseAll();
        bindingTarget = target;
        bindingNew = target == null;
        removeArmed = false;
        keyboard.showPicker(target == null ? 0 : target.keycode, target == null ? 0 : target.secondKeycode,
                target == null ? 0 : target.mouseButton);
        keyboard.layout(getWidth(), getHeight());
    }

    private String defaultButtonText(Button b) {
        return b.custom ? b.bindingText() : b.defaultLabel;
    }

    private void editButtonText(final Button target) {
        if (target == null) return;
        releaseAll();
        removeArmed = false;
        final EditText input = new EditText(getContext());
        input.setSingleLine(true);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        input.setText(target.label);
        input.setSelection(input.getText().length());
        input.setHint("Button text");
        new AlertDialog.Builder(getContext())
                .setTitle("BUTTON TEXT")
                .setView(input)
                .setPositiveButton("SAVE", (dialog, which) -> {
                    String value = ControlKeys.cleanLabel(input.getText().toString());
                    target.labelEdited = !value.isEmpty();
                    target.label = target.labelEdited ? value : defaultButtonText(target);
                    cache.clear();
                    saveLayout();
                    layoutButtons(getWidth(), getHeight());
                    invalidate();
                })
                .setNeutralButton("DEFAULT", (dialog, which) -> {
                    target.labelEdited = false;
                    target.label = defaultButtonText(target);
                    cache.clear();
                    saveLayout();
                    layoutButtons(getWidth(), getHeight());
                    invalidate();
                })
                .setNegativeButton("CANCEL", null)
                .show();
    }

    private void finishBinding(int first, int second, int mouseButton) {
        if (!CustomButtonStore.validBinding(first, second, mouseButton)) return;
        if (bindingNew) {
            if (customButtons.size() >= CustomButtonStore.MAX_BUTTONS) return;
            CustomButtonStore.Entry entry = new CustomButtonStore.Entry(nextCustomId++, first, second);
            entry.mouseButton = mouseButton;
            defaultCustomGeometry(entry, customButtons.size());
            Button created = new Button(entry);
            customButtons.add(created);
            rebuildAllButtons();
            selected = created;
            selectedStick = false;
        } else if (bindingTarget != null && bindingTarget.custom) {
            bindingTarget.keycode = first;
            bindingTarget.secondKeycode = second;
            bindingTarget.mouseButton = mouseButton;
            if (!bindingTarget.labelEdited) bindingTarget.label = ControlKeys.bindingLabel(first, second, mouseButton);
            selected = bindingTarget;
            selectedStick = false;
        }
        bindingTarget = null;
        bindingNew = false;
        cache.clear();
        saveCustomButtons();
        layoutButtons(getWidth(), getHeight());
        invalidate();
    }

    private void clearSnap() {
        snapXActive = snapYActive = false;
        snapTargetKey = Long.MIN_VALUE;
    }

    private long buttonSnapKey(Button b) {
        if (b == null) return -1;
        return b.custom ? 1000000L + b.customId : b.builtInIndex + 1L;
    }

    /** Snap in editor space only. These pointy-topped hexagons have vertical sides; side contact is horizontal. */
    private void snapDrag(float nx, float ny) {
        final float threshold = 8f * density;
        float bestX = threshold, bestY = threshold;
        long xTarget = Long.MIN_VALUE, yTarget = Long.MIN_VALUE;
        snapXActive = snapYActive = false;
        snappedX = nx;
        snappedY = ny;
        float movingRadius = dragButton != null ? dragButton.r : stickR;
        for (Button target : allButtons) {
            if (target == dragButton) continue;
            long key = buttonSnapKey(target);
            float dx = Math.abs(nx - target.cx);
            if (dx <= bestX) {
                bestX = dx;
                snappedX = target.cx;
                snapGuideX = target.cx;
                snapXActive = true;
                xTarget = key;
            }
            float dy = Math.abs(ny - target.cy);
            if (dy <= bestY) {
                bestY = dy;
                snappedY = target.cy;
                snapGuideY = target.cy;
                snapYActive = true;
                yTarget = key;
            }
            if (!dragStick && movingRadius > 0 && target.r > 0) {
                float edgeDistance = (float) (Math.sqrt(3.0) * 0.5 * (movingRadius + target.r));
                float edgeDelta = Math.abs(Math.abs(nx - target.cx) - edgeDistance);
                // Side contact is useful only while the polygons have vertical overlap.
                if (edgeDelta <= bestX && Math.abs(ny - target.cy) <= (movingRadius + target.r) * 0.9f) {
                    float direction = nx >= target.cx ? 1f : -1f;
                    bestX = edgeDelta;
                    snappedX = target.cx + direction * edgeDistance;
                    snapGuideX = target.cx + direction * target.r * (float) (Math.sqrt(3.0) * 0.5);
                    snapXActive = true;
                    xTarget = key;
                }
            }
        }
        if (dragButton != null || dragStick) {
            long stickKey = -1;
            float sx = stickHomeX, sy = stickHomeY;
            if (!dragStick) {
                float dx = Math.abs(nx - sx);
                if (dx <= bestX) {
                    bestX = dx;
                    snappedX = sx;
                    snapGuideX = sx;
                    snapXActive = true;
                    xTarget = stickKey;
                }
                float dy = Math.abs(ny - sy);
                if (dy <= bestY) {
                    bestY = dy;
                    snappedY = sy;
                    snapGuideY = sy;
                    snapYActive = true;
                    yTarget = stickKey;
                }
            }
        }
        long newTarget = xTarget == Long.MIN_VALUE ? yTarget
                : yTarget == Long.MIN_VALUE ? xTarget : xTarget * 131L + yTarget;
        if (newTarget != snapTargetKey) {
            if (newTarget != Long.MIN_VALUE) buzz();
            snapTargetKey = newTarget;
        }
        float w = getWidth(), h = getHeight();
        float r = movingRadius;
        snappedX = clampCenter(snappedX, r, w);
        snappedY = clampCenter(snappedY, r, h);
    }

    private float snappedX, snappedY;

    private static float clampCenter(float value, float radius, float extent) {
        if (extent <= 0) return value;
        if (extent <= radius * 2) return extent / 2f;
        return Math.max(radius, Math.min(extent - radius, value));
    }

    private void editTouch(MotionEvent e, int action, int id, float x, float y) {
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (dragPointer >= 0) break;
                if (doneRect.contains(x, y)) {
                    finishEditing();
                    break;
                }
                if (resetRect.contains(x, y)) {
                    resetLayout();
                    buzz();
                    break;
                }
                if (addRect.contains(x, y)) {
                    if (customButtons.size() < CustomButtonStore.MAX_BUTTONS) startBinding(null);
                    buzz();
                    break;
                }
                if (stickRect.contains(x, y)) {
                    selected = null;
                    selectedStick = true;
                    stickHidden = false;
                    removeArmed = false;
                    saveLayout();
                    buzz();
                    break;
                }
                if (selected != null && textRect.contains(x, y)) {
                    editButtonText(selected);
                    buzz();
                    break;
                }
                if (selected != null && selected.custom && bindRect.contains(x, y)) {
                    startBinding(selected);
                    buzz();
                    break;
                }
                if ((selected != null || selectedStick) && hideRect.contains(x, y)) {
                    if (selected != null) selected.hidden = !selected.hidden;
                    else stickHidden = !stickHidden;
                    removeArmed = false;
                    saveLayout();
                    buzz();
                    break;
                }
                if (selected != null && selected.custom && removeRect.contains(x, y)) {
                    if (removeArmed) {
                        customButtons.remove(selected);
                        rebuildAllButtons();
                        selected = null;
                        removeArmed = false;
                        saveCustomButtons();
                        layoutButtons(getWidth(), getHeight());
                    } else {
                        removeArmed = true;
                    }
                    buzz();
                    break;
                }
                if ((selected != null || selectedStick || chrome != 0)
                        && (smallerRect.contains(x, y) || biggerRect.contains(x, y))) {
                    removeArmed = false;
                    float step = biggerRect.contains(x, y) ? 0.1f : -0.1f;
                    if (chrome == 1) {
                        modeSize = Math.max(0.6f, Math.min(2f, modeSize + step));
                    } else if (chrome == 2) {
                        gearSize = Math.max(0.6f, Math.min(2f, gearSize + step));
                    } else if (selected != null) {
                        selected.size = Math.max(0.6f, Math.min(2f, selected.size + step));
                    } else {
                        stickSize = Math.max(0.6f, Math.min(2f, stickSize + step));
                    }
                    float sx = stickHomeX, sy = stickHomeY;
                    layoutButtons(getWidth(), getHeight());
                    if (stickCustomX < 0) {  // keep a stick that was not moved where it is drawn
                        stickHomeX = stickX = knobX = sx;
                        stickHomeY = stickY = knobY = sy;
                    }
                    buzz();
                    break;
                }
                // Opaque editor chrome owns its touches; never drag a control hidden underneath it.
                if (editorBar.contains(x, y) || selectionBar.contains(x, y)) {
                    removeArmed = false;
                    break;
                }
                // The MENU/GAME pill and the settings gear are draggable too, and they win over a button parked on top
                // of them: otherwise a control dropped over the gear could never be moved off it again.
                if (modeRect.contains(x, y) || gearRect.contains(x, y)) {
                    chrome = modeRect.contains(x, y) ? 1 : 2;
                    chromeDrag = id;
                    selected = null;
                    selectedStick = false;
                    removeArmed = false;
                    clearSnap();
                    RectF r = chrome == 1 ? modeRect : gearRect;
                    dragDx = r.centerX() - x;
                    dragDy = r.centerY() - y;
                    buzz();
                    break;
                }
                for (Button b : allButtons) {
                    if (visible(b) && b.hit(x, y)) {
                        dragButton = b;
                        dragDx = b.cx - x;
                        dragDy = b.cy - y;
                        break;
                    }
                }
                if (dragButton == null) {
                    float dx = x - stickHomeX, dy = y - stickHomeY;
                    if (dx * dx + dy * dy <= stickR * stickR) {
                        dragStick = true;
                        dragDx = stickHomeX - x;
                        dragDy = stickHomeY - y;
                    }
                }
                if (dragButton != null || dragStick) {
                    dragPointer = id;
                    selected = dragButton;
                    selectedStick = dragStick;
                    chrome = 0;
                    removeArmed = false;
                    clearSnap();
                    buzz();
                } else {
                    // A tap on empty space clears the selection, so the option panel stops covering the controls
                    // underneath it while the rest of the layout is rearranged.
                    selected = null;
                    selectedStick = false;
                    chrome = 0;
                    removeArmed = false;
                    clearSnap();
                }
                break;
            case MotionEvent.ACTION_MOVE: {
                int ci = chromeDrag >= 0 ? e.findPointerIndex(chromeDrag) : -1;
                if (ci >= 0) {
                    float viewW = getWidth(), viewH = getHeight();
                    RectF moving = chrome == 1 ? modeRect : gearRect;
                    float chromeX = clampCenter(e.getX(ci) + dragDx, moving.width() / 2f, viewW);
                    float chromeY = clampCenter(e.getY(ci) + dragDy, moving.height() / 2f, viewH);
                    float left = chromeX - 0.5f * moving.width(), top = chromeY - 0.5f * moving.height();
                    if (chrome == 1) {
                        modeCustomX = left / viewW;
                        modeCustomY = top / viewH;
                    } else {
                        gearCustomX = left / viewW;
                        gearCustomY = top / viewH;
                    }
                    layoutButtons((int) viewW, (int) viewH);
                    invalidate();
                    break;
                }
                int i = e.findPointerIndex(dragPointer);
                if (i < 0) {
                    clearSnap();
                    break;
                }
                float w = getWidth(), h = getHeight();
                float movingRadius = dragButton != null ? dragButton.r : stickR;
                float nx = clampCenter(e.getX(i) + dragDx, movingRadius, w);
                float ny = clampCenter(e.getY(i) + dragDy, movingRadius, h);
                snapDrag(nx, ny);
                nx = snappedX;
                ny = snappedY;
                if (dragButton != null) {
                    dragButton.cx = nx;
                    dragButton.cy = ny;
                    dragButton.customX = nx / w;
                    dragButton.customY = ny / h;
                } else if (dragStick) {
                    stickHomeX = stickX = knobX = nx;
                    stickHomeY = stickY = knobY = ny;
                    stickCustomX = nx / w;
                    stickCustomY = ny / h;
                }
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                if (id == chromeDrag || action == MotionEvent.ACTION_CANCEL) {
                    chromeDrag = -1;
                }
                if (action == MotionEvent.ACTION_CANCEL || id == dragPointer) {
                    dragPointer = -1;
                    dragButton = null;
                    dragStick = false;
                    clearSnap();
                }
                break;
            default:
                break;
        }
    }

    // ---- touch ----
    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        int id = e.getPointerId(index);
        float x = e.getX(index), y = e.getY(index);
        if (keyboard.isVisible()) {
            keyboard.onTouch(e, action, id, x, y);
            invalidate();
            return true;
        }
        if (panelOpen) {
            if (action == MotionEvent.ACTION_UP) {
                if (id == panelOpener) panelOpener = -1;  // opening on DOWN means this UP lands outside the
                else panelTap(x, y);                      // panel and would instantly close it again
            } else if (action == MotionEvent.ACTION_CANCEL) {
                panelOpener = -1;
            }
            invalidate();
            return true;
        }
        if (editing) {
            editTouch(e, action, id, x, y);
            invalidate();
            return true;
        }
        // Camera-only movement has no overlay visual to redraw. Native mouse motion is still delivered for every
        // pointer below; only the expensive full-overlay invalidation is conditional.
        boolean redraw = action != MotionEvent.ACTION_MOVE;
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                if (gearRect.contains(x, y)) {
                    gearPointer = id;
                    break;
                }
                if (!controlsHidden() && modeRect.contains(x, y)) {
                    modePointer = id;
                    break;
                }
                Button hit = null;
                for (Button b : allButtons) {
                    if (visible(b) && b.pointer < 0 && b.hit(x, y)) {
                        hit = b;
                        break;
                    }
                }
                if (hit != null) {
                    hit.fx = x;
                    hit.fy = y;
                    press(hit, id);
                } else if (gameMode && !controlsHidden() && !stickHidden && stickPointer < 0
                        && x < getWidth() * 0.4f && y > getHeight() * 0.35f) {
                    stickPointer = id;
                    stickX = x;  // the stick centres where the thumb lands
                    stickY = y;
                    updateStick(x, y);
                } else if (lookPointer < 0) {
                    lookPointer = id;
                    lookX = x;
                    lookY = y;
                    if (!gameMode) {
                        mouseMoveTo(x, y);
                        mouseButton(MOUSE_LEFT, true, x, y);
                    }
                }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) {
                    int pid = e.getPointerId(i);
                    float px = e.getX(i), py = e.getY(i);
                    Button held = pointerButton.get(pid);
                    if (held != null && held.wheel) {
                        float g = 2.5f * camGain;
                        mouseMoveBy((px - held.fx) * g, (py - held.fy) * g);
                        held.fx = px;
                        held.fy = py;
                        redraw = true;
                    } else if (pid == stickPointer) {
                        updateStick(px, py);
                        redraw = true;
                    } else if (pid == lookPointer) {
                        float g = 1.5f * camGain;
                        if (gameMode) mouseMoveBy((px - lookX) * g, (py - lookY) * g * (invertY ? -1 : 1));
                        else mouseMoveTo(px, py);
                        lookX = px;
                        lookY = py;
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (id == gearPointer) {
                    gearPointer = -1;
                    if (gearRect.contains(x, y)) {
                        releaseAll();
                        panelOpen = true;
                        panelOpener = id;  // the finger that opened it: its UP must not close the panel again
                        buzz();
                    }
                } else if (id == modePointer) {
                    modePointer = -1;
                    if (modeRect.contains(x, y)) {
                        releaseAll();
                        gameMode = !gameMode;
                        buzz();
                    }
                } else if (pointerButton.containsKey(id)) {
                    release(pointerButton.get(id));
                } else if (id == stickPointer) {
                    releaseStick();
                } else if (id == lookPointer) {
                    lookPointer = -1;
                    if (!gameMode) mouseButton(MOUSE_LEFT, false, x, y);
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                modePointer = gearPointer = -1;
                break;
            default:
                break;
        }
        if (redraw) invalidate();
        return true;
    }

    // ---- settings panel ----
    private void panelTap(float x, float y) {
        if (closeRect.contains(x, y) || !panel.contains(x, y)) {
            panelOpen = false;
            panelOpener = -1;
            return;
        }
        if (layoutRect.contains(x, y)) {
            startEditing();
            return;
        }
        for (int i = 0; i < settings.length; i++) {
            int delta = leftArrows.get(i).contains(x, y) ? -1 : rightArrows.get(i).contains(x, y) ? 1 : 0;
            if (delta == 0) continue;
            GameSettings.Setting s = settings[i];
            settingsStore.setIndex(s, s.index + delta);
            buzz();
            applyUiSettings();
            applyLiveSettings();
            return;
        }
    }

    // ---- drawing ----
    private int alpha(int color, float a) {
        return Color.argb(Math.round(Color.alpha(color) * a * opacity), Color.red(color), Color.green(color),
                Color.blue(color));
    }

    private void hexagon(float cx, float cy, float r) {
        path.reset();
        for (int i = 0; i < 6; i++) {
            double a = Math.PI / 3 * i + Math.PI / 6;
            float px = cx + (float) Math.cos(a) * r, py = cy + (float) Math.sin(a) * r;
            if (i == 0) path.moveTo(px, py);
            else path.lineTo(px, py);
        }
        path.close();
    }

    /** Prototype's bracket corners around a rectangle. */
    private void brackets(Canvas c, RectF r, float len, Paint p) {
        c.drawLine(r.left, r.top, r.left + len, r.top, p);
        c.drawLine(r.left, r.top, r.left, r.top + len, p);
        c.drawLine(r.right, r.top, r.right - len, r.top, p);
        c.drawLine(r.right, r.top, r.right, r.top + len, p);
        c.drawLine(r.left, r.bottom, r.left + len, r.bottom, p);
        c.drawLine(r.left, r.bottom, r.left, r.bottom - len, p);
        c.drawLine(r.right, r.bottom, r.right - len, r.bottom, p);
        c.drawLine(r.right, r.bottom, r.right, r.bottom - len, p);
    }

    private void drawButton(Canvas c, Button b) {
        final boolean down = b.pointer >= 0;
        if (down && b.wheel) {  // where the held finger points the wheel
            edge.setStrokeWidth(3 * density);
            edge.setColor(0xccffffff);
            c.drawLine(b.cx, b.cy, b.fx, b.fy, edge);
            c.drawCircle(b.fx, b.fy, 10 * density, edge);
        }
        Bitmap bmp = cached(b.label + ":" + b.bindingText() + (editing ? ":E" : ":N")
                        + (down ? ":1:" : ":0:") + (b.hidden ? ":H:" : ":V:") + b.r,
                b.r + 12 * density, (cc, x, y) -> paintButton(cc, b, x, y, down));
        float savedOpacity = opacity;
        if (editing && b != selected) opacity *= 0.52f;
        blitCentered(c, bmp, b.cx, b.cy);
        opacity = savedOpacity;
    }

    private void paintButton(Canvas c, Button b, float bx, float by, boolean down) {
        hexagon(bx, by, b.r);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(alpha(down ? 0xd0b0180f : 0xa00b0b0b, 1f));
        c.drawPath(path, fill);
        glow.setStrokeWidth(4 * density);
        glow.setColor(alpha(RED, down ? 1f : 0.55f));
        c.drawPath(path, glow);
        edge.setStrokeWidth(1.8f * density);
        edge.setColor(alpha(down ? 0xffffd0c8 : RED, 1f));
        c.drawPath(path, edge);
        hexagon(bx, by, b.r * 0.78f);
        edge.setStrokeWidth(0.8f * density);
        edge.setColor(alpha(RED_DIM, 0.9f));
        c.drawPath(path, edge);
        float labelSize = Math.min(14 * density, b.r * 0.42f);
        text.setTextSize(labelSize);
        while (labelSize > 7 * density && text.measureText(b.label) > b.r * 1.45f) {
            labelSize -= density;
            text.setTextSize(labelSize);
        }
        text.setColor(alpha(down ? 0xffffffff : 0xffffd6d0, 1f));
        float labelBaseline = editing ? by - b.r * 0.20f : by + labelSize * (b.hidden ? 0.05f : 0.36f);
        c.drawText(b.label, bx, labelBaseline, text);
        if (editing) {
            String binding = b.bindingText();
            float bindingSize = Math.min(9 * density, b.r * 0.25f);
            text.setTextSize(bindingSize);
            while (bindingSize > 6 * density && text.measureText(binding) > b.r * 1.45f) {
                bindingSize -= density;
                text.setTextSize(bindingSize);
            }
            text.setColor(alpha(0xffffa69d, 1f));
            c.drawText(binding, bx, by + b.r * (b.hidden ? 0.30f : 0.52f), text);
        }
        if (b.hidden) {
            text.setTextSize(Math.max(7 * density, labelSize * 0.65f));
            text.setColor(alpha(0xffff8d82, 1f));
            c.drawText("HIDDEN", bx, editing ? by + b.r * 0.78f : by + labelSize * 1.15f, text);
        }
    }

    private void drawStick(Canvas c) {
        blitCentered(c, cached("stick:" + stickR, stickR + 12 * density, this::paintStickBase), stickX, stickY);
        final boolean held = stickPointer >= 0;
        blitCentered(c, cached("knob:" + held + stickR, stickR * 0.5f + 4 * density,
                (cc, x, y) -> paintKnob(cc, x, y, held)), knobX, knobY);
    }

    private void paintKnob(Canvas c, float x, float y, boolean held) {
        hexagon(x, y, stickR * 0.42f);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(alpha(held ? 0xe0c01a10 : 0xa0300806, 1f));
        c.drawPath(path, fill);
        edge.setStrokeWidth(1.6f * density);
        edge.setColor(alpha(0xffff6a5e, 1f));
        c.drawPath(path, edge);
    }

    private void paintStickBase(Canvas c, float stickX, float stickY) {
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(alpha(0x700b0b0b, 1f));
        c.drawCircle(stickX, stickY, stickR, fill);
        glow.setStrokeWidth(4 * density);
        glow.setColor(alpha(RED, 0.5f));
        c.drawCircle(stickX, stickY, stickR, glow);
        edge.setStrokeWidth(1.6f * density);
        edge.setColor(alpha(RED, 0.9f));
        c.drawCircle(stickX, stickY, stickR, edge);
        // tick marks every 30 degrees, longer on the four directions
        for (int i = 0; i < 12; i++) {
            double a = Math.PI / 6 * i;
            float in = stickR * (i % 3 == 0 ? 0.80f : 0.88f);
            c.drawLine(stickX + (float) Math.cos(a) * in, stickY + (float) Math.sin(a) * in,
                    stickX + (float) Math.cos(a) * stickR, stickY + (float) Math.sin(a) * stickR, edge);
        }
    }

    private void drawPill(Canvas c, RectF r, String label, boolean active) {
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(alpha(active ? 0xc08a120c : 0xa00b0b0b, 1f));
        c.drawRect(r, fill);
        edge.setStrokeWidth(1.5f * density);
        edge.setColor(alpha(RED, 1f));
        brackets(c, r, 9 * density, edge);
        float labelSize = 14 * density;
        text.setTextSize(labelSize);
        while (labelSize > 8 * density && text.measureText(label) > r.width() - 12 * density) {
            labelSize -= density;
            text.setTextSize(labelSize);
        }
        text.setColor(alpha(0xffffe0dc, 1f));
        c.drawText(label, r.centerX(), r.centerY() + labelSize * 0.36f, text);
    }

    private void drawGear(Canvas c) {
        float cx = gearRect.centerX(), cy = gearRect.centerY(), r = gearRect.width() * 0.32f;
        edge.setStrokeWidth(2.2f * density);
        edge.setColor(alpha(RED, 1f));
        for (int i = 0; i < 8; i++) {
            double a = Math.PI / 4 * i;
            c.drawLine(cx + (float) Math.cos(a) * r, cy + (float) Math.sin(a) * r,
                    cx + (float) Math.cos(a) * r * 1.35f, cy + (float) Math.sin(a) * r * 1.35f, edge);
        }
        c.drawCircle(cx, cy, r, edge);
        c.drawCircle(cx, cy, r * 0.4f, edge);
    }

    private void drawPerf(Canvas c) {
        float fps, ms, p99;
        int mem;
        try {
            fps = nativeFps();
            ms = nativeFrameMs();
            p99 = nativeFrameP99Ms();
            mem = nativeMemoryMiB();
        } catch (UnsatisfiedLinkError e) {
            return;
        }
        String line = String.format("%.0f FPS  AVG %.1f  P99 %.1f MS  %d MB", fps, ms, p99, mem);
        text.setTextSize(13 * density);
        text.setTextAlign(Paint.Align.LEFT);
        text.setColor(fps >= 50 ? 0xff7dff7a : fps >= 28 ? 0xffffd060 : 0xffff5a4a);
        c.drawText(line, 16 * density, 74 * density, text);
        text.setTextAlign(Paint.Align.CENTER);
    }

    private void drawPanel(Canvas c) {
        c.drawColor(0x99000000);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(0xf00b0b0b);
        c.drawRect(panel, fill);
        edge.setStrokeWidth(3 * density);
        edge.setColor(0x60e0261e);
        c.drawRect(panel, edge);
        edge.setStrokeWidth(2 * density);
        edge.setColor(RED);
        brackets(c, panel, 22 * density, edge);
        float pad = 22 * density;
        text.setTextSize(20 * density);
        text.setColor(RED);
        c.drawText("SETTINGS", panel.centerX(), panel.top + 36 * density, text);
        // Keep the corners empty: the centered title is the only header text.
        // two columns of settings
        int rows = (settings.length + 1) / 2;
        float rowH = (panel.height() - 128 * density) / rows;
        float colW = (panel.width() - pad * 3) / 2;
        float top = panel.top + 54 * density;
        text.setTextSize(Math.min(15 * density, rowH * 0.42f));
        for (int i = 0; i < settings.length; i++) {
            GameSettings.Setting s = settings[i];
            float colLeft = panel.left + pad + (i / rows) * (colW + pad), colRight = colLeft + colW;
            float y = top + rowH * (i % rows) + rowH / 2;
            text.setTextAlign(Paint.Align.LEFT);
            text.setColor(0xffe8d6d4);
            c.drawText(s.label, colLeft, y + 5 * density, text);
            float vx = colRight - 62 * density;
            RectF l = leftArrows.get(i), r = rightArrows.get(i);
            l.set(vx - 64 * density, y - rowH / 2, vx - 32 * density, y + rowH / 2);
            r.set(vx + 32 * density, y - rowH / 2, vx + 64 * density, y + rowH / 2);
            text.setTextAlign(Paint.Align.CENTER);
            text.setColor(RED);
            c.drawText("‹", l.centerX(), y + 6 * density, text);
            c.drawText("›", r.centerX(), y + 6 * density, text);
            text.setColor(0xffffffff);
            c.drawText(s.names[s.index], vx, y + 5 * density, text);
            edge.setStrokeWidth(0.6f * density);
            edge.setColor(0x40e0261e);
            c.drawLine(colLeft, y + rowH / 2, colRight, y + rowH / 2, edge);
        }
        text.setTextAlign(Paint.Align.CENTER);
        float by = panel.bottom - 48 * density, pillW = 150 * density, gap = 12 * density;
        float cx = panel.centerX();
        layoutRect.set(cx - pillW * 1.5f - gap, by - 18 * density, cx - pillW * 0.5f - gap, by + 18 * density);
        drawPill(c, layoutRect, "LAYOUT", false);
        closeRect.set(cx + pillW * 0.5f + gap, by - 18 * density, cx + pillW * 1.5f + gap, by + 18 * density);
        drawPill(c, closeRect, "CLOSE", false);
    }

    private void drawEditor(Canvas c) {
        c.drawColor(0x77000000);
        float savedOpacity = opacity;
        if (!selectedStick) opacity *= 0.52f;
        drawStick(c);
        opacity = savedOpacity;
        if (stickHidden) {
            text.setTextAlign(Paint.Align.CENTER);
            text.setTextSize(11 * density);
            text.setColor(0xffff8d82);
            c.drawText("HIDDEN", stickHomeX, stickHomeY + 4 * density, text);
        }
        for (Button b : allButtons) {
            if (visible(b)) drawButton(c, b);
        }
        // MENU/GAME and the gear are part of the layout, so they are drawn and selectable here as well.
        drawPill(c, modeRect, gameMode ? "GAME" : "MENU", gameMode);
        drawGear(c);
        if (selected != null || selectedStick) {
            float sx = selected != null ? selected.cx : stickHomeX;
            float sy = selected != null ? selected.cy : stickHomeY;
            float sr = selected != null ? selected.r : stickR;
            edge.setStrokeWidth(2.5f * density);
            edge.setColor(0xffffffff);
            c.drawCircle(sx, sy, sr + 6 * density, edge);
        }
        if (chrome != 0) {
            RectF r = chrome == 1 ? modeRect : gearRect;
            float pad = 6 * density;
            edge.setStrokeWidth(2.5f * density);
            edge.setColor(0xffffffff);
            c.drawRect(r.left - pad, r.top - pad, r.right + pad, r.bottom + pad, edge);
        }
        if (snapXActive || snapYActive) {
            edge.setStyle(Paint.Style.STROKE);
            edge.setStrokeWidth(1.2f * density);
            edge.setColor(0x99ff9b8f);
            if (snapXActive) c.drawLine(snapGuideX, 0, snapGuideX, getHeight(), edge);
            if (snapYActive) c.drawLine(0, snapGuideY, getWidth(), snapGuideY, edge);
        }

        // The editor's own chrome sits at the bottom. It used to be a bar across the top of the screen, which owned
        // every touch in that strip and made the player's top-row controls (the power row, F1/F2, ENTER/ESC)
        // impossible to drag. Those controls now belong to the player everywhere.
        float margin = 10 * density;
        float gap = 6 * density;
        float barHeight = 44 * density;
        float barBottom = getHeight() - 8 * density;
        editorBar.set(margin, barBottom - barHeight, getWidth() - margin, barBottom);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(0xf5100d0f);
        c.drawRect(editorBar, fill);
        edge.setStrokeWidth(1.2f * density);
        edge.setColor(0x88504a4d);
        c.drawRect(editorBar, edge);
        edge.setColor(RED);
        c.drawLine(editorBar.left, editorBar.top, editorBar.right, editorBar.top, edge);

        float actionW = (editorBar.width() - gap * 3) / 4f;
        float actionTop = editorBar.top + 4 * density;
        float actionBottom = editorBar.bottom - 4 * density;
        doneRect.set(editorBar.right - actionW, actionTop, editorBar.right, actionBottom);
        resetRect.set(doneRect.left - gap - actionW, actionTop, doneRect.left - gap, actionBottom);
        stickRect.set(resetRect.left - gap - actionW, actionTop, resetRect.left - gap, actionBottom);
        addRect.set(stickRect.left - gap - actionW, actionTop, stickRect.left - gap, actionBottom);

        text.setTextAlign(Paint.Align.CENTER);
        drawPill(c, addRect, customButtons.size() >= CustomButtonStore.MAX_BUTTONS ? "MAX 24" : "ADD", false);
        drawPill(c, stickRect, stickHidden ? "ADD STICK" : "STICK", false);
        drawPill(c, resetRect, "RESET", false);
        drawPill(c, doneRect, "DONE", true);
        if (selected == null && !selectedStick && chrome == 0) {
            text.setTextSize(10 * density);
            text.setColor(0xe0b8aaa5);
            c.drawText("DRAG ANY CONTROL TO MOVE IT  \u2022  TAP FOR OPTIONS  \u2022  DONE SAVES",
                    editorBar.centerX(), editorBar.top - 9 * density, text);
        }

        if (selected != null || selectedStick || chrome != 0) {
            float panelWidth = Math.min(getWidth() - margin * 2, 620 * density);
            float panelLeft = (getWidth() - panelWidth) / 2f;
            float panelHeight = 82 * density;
            selectionBar.set(panelLeft, editorBar.top - gap * 2 - panelHeight,
                    panelLeft + panelWidth, editorBar.top - gap * 2);
            fill.setStyle(Paint.Style.FILL);
            fill.setColor(0xf7100d0f);
            c.drawRect(selectionBar, fill);
            edge.setStrokeWidth(1.2f * density);
            edge.setColor(0x88504a4d);
            c.drawRect(selectionBar, edge);
            edge.setColor(RED);
            c.drawLine(selectionBar.left, selectionBar.top, selectionBar.right, selectionBar.top, edge);

            float size = chrome == 1 ? modeSize : chrome == 2 ? gearSize
                    : selected != null ? selected.size : stickSize;
            String name = chrome == 1 ? "MENU/GAME" : chrome == 2 ? "GEAR"
                    : selected != null ? selected.label : "STICK";
            String binding = chrome != 0 ? "UI" : selected != null ? selected.bindingText() : "MOVE";
            boolean hidden = chrome != 0 ? false : selected != null ? selected.hidden : stickHidden;
            text.setTextSize(13 * density);
            text.setColor(0xffffe0dc);
            c.drawText(name + "  •  " + binding + "  •  SIZE " + Math.round(size * 100) + "%",
                    selectionBar.centerX(), selectionBar.top + 22 * density, text);

            boolean custom = selected != null && selected.custom;
            boolean hasText = selected != null;
            int count = chrome != 0 ? 2 : custom ? 6 : hasText ? 4 : 3;
            float rowWidth = Math.min(selectionBar.width() - 20 * density,
                    (chrome != 0 ? 200 : custom ? 590 : hasText ? 390 : 310) * density);
            float itemWidth = (rowWidth - gap * (count - 1)) / count;
            float x = selectionBar.centerX() - rowWidth / 2f;
            float y = selectionBar.top + 34 * density;
            float bottom = y + 44 * density;
            smallerRect.set(x, y, x + itemWidth, bottom);
            biggerRect.set(x + itemWidth + gap, y, x + itemWidth * 2 + gap, bottom);
            int next = 2;
            if (hasText) {
                textRect.set(x + (itemWidth + gap) * next, y,
                        x + (itemWidth + gap) * next + itemWidth, bottom);
                drawPill(c, textRect, "TEXT", false);
                next++;
            } else {
                textRect.setEmpty();
            }
            if (custom) {
                bindRect.set(x + (itemWidth + gap) * next, y,
                        x + (itemWidth + gap) * next + itemWidth, bottom);
                drawPill(c, bindRect, "BIND", false);
                next++;
            } else {
                bindRect.setEmpty();
            }
            drawPill(c, smallerRect, "\u2212", false);
            drawPill(c, biggerRect, "+", false);
            if (chrome != 0) {
                hideRect.setEmpty();
            } else {
                hideRect.set(x + (itemWidth + gap) * next, y,
                        x + (itemWidth + gap) * next + itemWidth, bottom);
                drawPill(c, hideRect, hidden ? "SHOW" : "HIDE", false);
                next++;
            }
            if (custom) {
                removeRect.set(x + (itemWidth + gap) * next, y,
                        x + (itemWidth + gap) * next + itemWidth, bottom);
                drawPill(c, removeRect, removeArmed ? "CONFIRM" : "DELETE", removeArmed);
            } else {
                removeRect.setEmpty();
            }
        } else {
            selectionBar.setEmpty();
            smallerRect.setEmpty();
            biggerRect.setEmpty();
            textRect.setEmpty();
            bindRect.setEmpty();
            hideRect.setEmpty();
            removeRect.setEmpty();
        }
    }

    // Refresh the performance overlay twice a second. Attachment owns this callback so finishing an Activity cannot
    // leave a Handler retaining the view, and standalone layout editing never starts a gameplay statistics loop.
    private final Runnable fpsRefresh = new Runnable() {
        @Override public void run() {
            if (showFps) invalidate();
            postDelayed(this, 500);
        }
    };

    // Screenshot of this layer for testing (the game's own shot_now trigger only has the game frame): a file named
    // ui_shot_now in the app's files folder makes the controls draw themselves into ui_shot.png there.
    private final Runnable uiShotPoll = new Runnable() {
        @Override public void run() {
            File dir = getContext().getExternalFilesDir(null);
            File trigger = dir == null ? null : new File(dir, "ui_shot_now");
            if (trigger != null && trigger.delete() && getWidth() > 0) {
                Bitmap bmp = Bitmap.createBitmap(getWidth(), getHeight(), Bitmap.Config.ARGB_8888);
                draw(new Canvas(bmp));
                try (java.io.FileOutputStream out = new java.io.FileOutputStream(new File(dir, "ui_shot.png"))) {
                    bmp.compress(Bitmap.CompressFormat.PNG, 100, out);
                } catch (java.io.IOException ignored) {
                    // a test aid only
                }
                bmp.recycle();
            }
            postDelayed(this, 1000);
        }
    };

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (inputManager == null) {
            inputManager = (InputManager) getContext().getSystemService(Context.INPUT_SERVICE);
            if (inputManager != null) {
                inputManager.registerInputDeviceListener(this, null);
                padConnected = findGamepad(inputManager);
            }
        }
        if (!layoutOnly) {
            removeCallbacks(fpsRefresh);
            removeCallbacks(uiShotPoll);
            postDelayed(fpsRefresh, 500);
            postDelayed(uiShotPoll, 1000);
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        if (inputManager != null) {
            inputManager.unregisterInputDeviceListener(this);
            inputManager = null;
        }
        releaseAll();
        clearSnap();
        keyboard.hide();
        removeCallbacks(fpsRefresh);
        removeCallbacks(uiShotPoll);
        super.onDetachedFromWindow();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (editing) {
            drawEditor(canvas);
        } else {
            if (!controlsHidden()) drawPill(canvas, modeRect, gameMode ? "GAME" : "MENU", gameMode);
            drawGear(canvas);
            if (showFps) drawPerf(canvas);
            if (gameMode && !controlsHidden() && !stickHidden) drawStick(canvas);
            for (Button b : allButtons) {
                if (visible(b)) drawButton(canvas, b);
            }
            if (panelOpen) drawPanel(canvas);
        }
        if (keyboard.isVisible()) {
            canvas.drawColor(0x77000000);
            keyboard.draw(canvas);
        }
    }
}
