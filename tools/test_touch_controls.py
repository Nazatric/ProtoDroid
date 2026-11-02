#!/usr/bin/env python3
"""Structural regressions for paged keys, touch-layout editing, and launcher-only customization."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JAVA = ROOT / "android/java/com/protodroid"
touch = (JAVA / "TouchControls.java").read_text(encoding="utf-8")
keyboard = (JAVA / "TouchKeyboard.java").read_text(encoding="utf-8")
catalog = (JAVA / "ControlKeys.java").read_text(encoding="utf-8")
store = (JAVA / "CustomButtonStore.java").read_text(encoding="utf-8")
state = (JAVA / "TouchKeyState.java").read_text(encoding="utf-8")
model_test = (ROOT / "android/tests/com/protodroid/ControlsModelTest.java").read_text(encoding="utf-8")


def rows(name):
    body = keyboard.split("List<int[]> " + name + " = rows(", 1)[1].split("));", 1)[0] + ")"
    blocks = re.findall(r"\bkeys\((.*?)\)", body, re.S)
    codes = []
    aliases = {"DPAD_UP": "UP", "DPAD_DOWN": "DOWN", "DPAD_LEFT": "LEFT",
               "DPAD_RIGHT": "RIGHT", "FORWARD_DEL": "DELETE"}
    for block in blocks:
        for code in re.findall(r"\b([A-Z][A-Z0-9_]*)\b", block):
            if code.startswith("KEYCODE_"):
                suffix = code[len("KEYCODE_"):]
                code = "KEY_" + suffix if suffix.isdigit() else suffix
            codes.append(aliases.get(code, code))
    return blocks, codes


main_blocks, main = rows("MAIN_ROWS")
more_blocks, more = rows("MORE_ROWS")
numpad_blocks, numpad = rows("NUMPAD_ROWS")
mouse_blocks, mouse = rows("MOUSE_ROWS")
assert len(main_blocks) == 4 and len(main) == len(set(main)) == 45
assert len(more_blocks) == 5 and len(more) == len(set(more)) == 42
assert len(numpad_blocks) == 5 and len(numpad) == len(set(numpad)) == 17
assert len(mouse_blocks) == 1 and set(mouse) == {"MOUSE_PICK_LEFT", "MOUSE_PICK_RIGHT", "MOUSE_PICK_MIDDLE"}
assert all(key in main for key in ("UP", "DOWN", "LEFT", "RIGHT")), "cheat-screen arrows left MAIN"
paged = main + more + numpad
assert len(paged) == len(set(paged)) == 104, "pages must partition all 104 keys without duplicates"
assert "ALL.size() != 104" in catalog and "Collections.unmodifiableList" in catalog
registered = set(re.findall(r"\bkey\(([A-Z0-9_]+)\s*,", catalog))
for array, values in re.findall(r"int\[\] (\w+) = \{([^}]+)\};", catalog):
    if re.search(r"key\(" + re.escape(array) + r"\[i\]", catalog):
        registered.update(re.findall(r"[A-Z][A-Z0-9_]*", values))
if "key(F1 + i" in catalog:
    registered.update(["F1"] + [f"F{i}" for i in range(2, 13)])
if "key(NUMPAD_0 + i" in catalog:
    registered.update(["NUMPAD_0"] + [f"NUMPAD_{i}" for i in range(1, 10)])
assert registered == set(paged), "paged picker and runtime key catalog differ"
keycap_body = catalog.split("static String keycap(int code)", 1)[1].split("static String chordLabel", 1)[0]
for case in re.findall(r"\bcase ([A-Z][A-Z0-9_]*):", keycap_body):
    assert case in registered, f"keycap uses a non-catalog constant: {case}"
for comparison in re.findall(r"\bcode == ([A-Z][A-Z0-9_]*)", keyboard):
    if comparison.startswith("MOUSE_PICK_"):
        continue
    assert comparison in registered, f"keyboard uses a non-catalog key constant: {comparison}"
assert "KEY_0 = 7" in catalog and "NUM_LOCK = 143" in catalog and "NUMPAD_ENTER = 160" in catalog

# Compact coverage and familiar labels replace the rejected 80/95%-height red grid and glyph/N-prefixed keycaps.
assert 'page == PAGE_MAIN ? 0.58f : 0.64f' in keyboard and 'picker ? 0.66f' in keyboard
assert 'page == PAGE_NUMPAD ? 0.62f : 0.90f' in keyboard
assert 'String[] pageNames = {"MAIN", "MORE", "NUMPAD", "MOUSE"}' in keyboard
assert "String label = capLabel(cap.code);" in keyboard and "shortNameOf(cap.code)" not in keyboard
assert 'return ControlKeys.keycap(code);' in keyboard and 'return "LEFT CLICK";' in keyboard
assert "picker ? 4 : 3" in keyboard and "MOUSE_ROWS" in keyboard
assert 'key(RIGHT, "Right Arrow", "RIGHT")' in catalog and '"NUM " + i' in catalog
assert '"N" + i' not in catalog and '"→"' not in catalog and '"←"' not in catalog
assert 'if (code >= NUMPAD_0 && code <= NUMPAD_9) return String.valueOf(code - NUMPAD_0);' in catalog
for label in ('return "LEFT";', 'return "RIGHT";', 'return "UP";', 'return "DOWN";',
              'return "SHIFT";', 'return "CTRL";', 'return "ALT";', 'return "WIN";'):
    assert label in catalog, f"missing conventional keycap: {label}"
assert "ACTION_FULL" not in keyboard and "compactRows" not in keyboard and "fullRows" not in keyboard
assert "if (code == SPACE) return 4.8f;" in keyboard

press = touch.split("private void press(Button b, int pointer)", 1)[1].split("private void release(Button b)", 1)[0]
release = touch.split("private void release(Button b)", 1)[1].split("private void setRun", 1)[0]
assert "keyboard.show();" in press and "SDLActivity.showTextInput" not in touch
assert "SDLActivity" not in keyboard, "keyboard must use the parent's reference-counted real-key boundary"
assert press.index("key(b.keycode, true)") < press.index("key(b.secondKeycode, true)"), "chord press order changed"
assert release.index("key(b.secondKeycode, false)") < release.index("key(b.keycode, false)"), (
    "chords must release in reverse order"
)
assert "keyState.set(keycode, down);" in touch and "keyState.releaseAll();" in touch
assert "counts.put(keycode, count + 1)" in state and "if (count == 0) return;" in state
assert "acquisitionOrder.get(i), false" in state and "counts.clear();" in state

assert "private void startBinding(Button target)" in touch and "keyboard.showPicker" in touch
assert "private void finishBinding(int first, int second, int mouseButton)" in touch
assert "bindingDone(int first, int second, int mouseButton)" in keyboard
assert "showPicker(target == null ? 0 : target.keycode" in touch
for action in ("drawPill(c, addRect", "drawPill(c, stickRect", "drawPill(c, textRect", "drawPill(c, bindRect", "drawPill(c, hideRect", "drawPill(c, removeRect"):
    assert action in touch, f"layout editor action missing: {action}"
assert 'removeArmed ? "CONFIRM" : "DELETE"' in touch, "custom-button deletion is not confirmed"
assert "if (selected != null) selected.hidden = !selected.hidden;" in touch
assert "else stickHidden = !stickHidden;" in touch and '"HIDDEN"' in touch
assert "customButtons.remove(selected);" in touch and "rebuildAllButtons();" in touch
assert 'prefs.getString(CustomButtonStore.PREFERENCE, "[]")' in touch
assert 'PREFERENCE = "custom_buttons_v1"' in store and "MAX_BUTTONS = 24" in store
assert "validBinding" in store and "ids.add(id)" in store and "saneCoordinate" in store
assert 'prefs.getFloat("pos" + i + "x", -1)' in touch and 'prefs.getFloat("size" + i, 1)' in touch
assert 'prefs.getBoolean("hide" + i, false)' in touch and 'putBoolean("hide" + i, b.hidden)' in touch
assert 'prefs.getBoolean("stick_hidden", false)' in touch and 'putBoolean("stick_hidden", stickHidden)' in touch
assert "for (int i = 0; i < buttons.length; i++)" in touch, "built-in index persistence loop missing"
assert "allButtons.addAll(customButtons);" in touch, "custom records reindexed the built-in array"
assert "private final RectF editorBar" in touch and "selectionBar" in touch
assert "editorBar.contains(x, y) || selectionBar.contains(x, y)" in touch
assert "if (editing && b != selected) opacity *= 0.52f;" in touch
assert 'prefs.getString("label" + i, "")' in touch and 'putString("label" + i, b.label)' in touch
assert 'remove("label" + i)' in touch and "defaultButtonText" in touch
assert "editButtonText(selected)" in touch and 'setNeutralButton("DEFAULT"' in touch
assert 'stickHidden ? "ADD STICK" : "STICK"' in touch and "stickHidden = false;" in touch
assert "snapDrag(nx, ny)" in touch and "Math.sqrt(3.0)" in touch
assert "snapGuideX" in touch and "snapGuideY" in touch and "clearSnap();" in touch
assert "clampCenter" in touch and "snapTargetKey" in touch
assert "mouseButton(b.mouseButton, true" in press and "mouseButton(b.mouseButton, false" in release
assert "entry.mouseButton = mouseButton;" in touch and "entry.label = labelEdited ? label" in touch
assert "b.bindingText()" in touch and "editing ? by - b.r * 0.20f" in touch

# Picker taps select only; normal taps inject. Modifiers latch for a sequential mobile chord and release after a key.
down = keyboard.split("case MotionEvent.ACTION_POINTER_DOWN:", 1)[1].split("case MotionEvent.ACTION_UP:", 1)[0]
assert "if (!picker && !isMousePick(cap.code) && !ControlKeys.isModifier(cap.code)) callback.key(cap.code, true);" in down
assert "if (picker)" in keyboard and "chooseBinding(cap.code);" in keyboard
assert "latchedModifiers.add(cap.code);" in keyboard and "releaseModifiers();" in keyboard
assert "CustomButtonStore.validBinding(bindingFirst, bindingSecond, bindingMouse)" in keyboard
assert "bindingMouse = mouseBit(code);" in keyboard and "bindingFirst = bindingSecond = 0;" in keyboard
assert "ACTION_PAGE_MOUSE" in keyboard and "pageCount()" in keyboard
assert "ACTION_CLEAR = 6" in keyboard and "ACTION_DONE = 7" in keyboard, "mouse page collides with picker commands"
assert "layout(lastViewWidth, lastViewHeight);" in keyboard
assert "lastViewWidth = viewWidth;" in keyboard and "lastViewHeight = viewHeight;" in keyboard
assert "case MotionEvent.ACTION_CANCEL:" in keyboard
assert keyboard.count("releaseAll();") >= 5, "show/hide/page/cancel cleanup paths were removed"
cancel = keyboard.split("case MotionEvent.ACTION_CANCEL:", 1)[1].split("default:", 1)[0]
assert "releaseAll();" in cancel

on_size = touch.split("protected void onSizeChanged", 1)[1].split("private void layoutButtons", 1)[0]
on_detach = touch.split("protected void onDetachedFromWindow", 1)[1].split("protected void onDraw", 1)[0]
assert "releaseAll();" in on_size and "releaseAll();" in on_detach
assert "if (keyboard.isVisible())" in touch and "keyboard.onTouch(e, action, id, x, y);" in touch
assert "if (editing)" in touch and "keyboard.draw(canvas);" in touch, "picker is not drawn above the editor"

# Launcher editing reuses saved controls without loading or configuring SDL.
layout_activity = (JAVA / "TouchLayoutActivity.java").read_text(encoding="utf-8")
compose_launcher = (ROOT / "android/launcher/src/commonMain/kotlin/com/protodroid/launcher/LauncherApp.kt").read_text(encoding="utf-8")
setup_host = (ROOT / "android/launcher/src/androidMain/kotlin/com/protodroid/ProtoDroidLauncherHost.kt").read_text(encoding="utf-8")
settings = (JAVA / "GameSettings.java").read_text(encoding="utf-8")
manifest = (ROOT / "android/AndroidManifest.xml").read_text(encoding="utf-8")
assert "new TouchControls(this, true)" in layout_activity and "controls.openLayoutEditor();" in layout_activity
assert "controls.handleBack()" in layout_activity
assert "SDLActivity" not in layout_activity and "ProtoActivity" not in layout_activity
assert "public TouchControls(Context context, boolean layoutOnly)" in touch
assert "if (!layoutOnly) applyLiveSettings();" in touch
assert "private void finishEditing()" in touch and "((Activity) getContext()).finish();" in touch
assert "if (layoutOnly) {" in touch and "inputManager.unregisterInputDeviceListener(this);" in touch
assert 'new Setting("Touch layout", "touch_layout", new String[] {"EDIT"}' in settings
assert 'if (setting.key.equals("touch_layout")) return CATEGORY_CONTROLS;' in settings
assert "LauncherPage.Layout" in compose_launcher and "OPEN TOUCH EDITOR" in compose_launcher
assert "TouchLayoutActivity::class.java" in setup_host
assert 'android:name="com.protodroid.TouchLayoutActivity"' in manifest

assert "104-key catalog" in model_test and "global release is not balanced reverse cleanup" in model_test
native_input = (ROOT / "src/native/input.cpp").read_text(encoding="utf-8")
native_map = native_input.split("const KeyMap kKeys[] = {", 1)[1].split("};", 1)[0]
native_keys = re.findall(r"\{SDL_SCANCODE_([A-Z0-9_]+),", native_map)
assert len(native_keys) == len(set(native_keys)) == 104, "native Windows/DirectInput map no longer covers 104 keys"
for mapping in (
    "{SDL_SCANCODE_PRINTSCREEN, 0x2c, 0x37, true}",
    "{SDL_SCANCODE_LGUI, 0x5b, 0x5b, true}",
    "{SDL_SCANCODE_RGUI, 0x5c, 0x5c, true}",
    "{SDL_SCANCODE_MENU, 0x5d, 0x5d, true}",
    "{SDL_SCANCODE_NUMLOCKCLEAR, 0x90, 0x45, false}",
):
    assert mapping in native_input, f"missing native key mapping: {mapping}"
patch = (ROOT / "android/patches/sdl2-zzzzzzzzzzzz-num-lock.patch").read_text(encoding="utf-8")
assert "-    SDL_SCANCODE_UNKNOWN,          /* AKEYCODE_NUM_LOCK */" in patch
assert "+    SDL_SCANCODE_NUMLOCKCLEAR,     /* AKEYCODE_NUM_LOCK */" in patch

print("Paged 104-key keyboard, editor visibility, launcher layout, chords, and cleanup checks passed")
