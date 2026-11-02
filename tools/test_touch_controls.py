#!/usr/bin/env python3
"""Structural regressions for the in-game keyboard, custom chords, and saved-layout compatibility."""

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


def rows(kind):
    blocks = re.findall(r"\b" + kind + r"\.add\(new Item\[\] \{(.*?)\}\);", keyboard, re.S)
    codes = []
    for block in blocks:
        codes.extend(re.findall(r"\bk\(ControlKeys\.([A-Z0-9_]+)", block))
    return blocks, codes


compact_blocks, compact = rows("compactRows")
full_blocks, full = rows("fullRows")
assert len(compact_blocks) == 5 and len(compact) == len(set(compact)) == 63
assert all(key in compact for key in ("UP", "DOWN", "LEFT", "RIGHT")), "cheat-screen arrows left the compact page"
assert len(full_blocks) == 6 and len(full) == len(set(full)) == 104, "full PC keyboard is incomplete or duplicated"
assert "ALL.size() != 104" in catalog and "Collections.unmodifiableList" in catalog
registered = set(re.findall(r"\bkey\(([A-Z0-9_]+)\s*,", catalog))
for array, values in re.findall(r"int\[\] (\w+) = \{([^}]+)\};", catalog):
    if re.search(r"key\(" + re.escape(array) + r"\[i\]", catalog):
        registered.update(re.findall(r"[A-Z][A-Z0-9_]*", values))
if "key(F1 + i" in catalog:
    registered.update(["F1"] + [f"F{i}" for i in range(2, 13)])
if "key(NUMPAD_0 + i" in catalog:
    registered.update(["NUMPAD_0"] + [f"NUMPAD_{i}" for i in range(1, 10)])
assert registered == set(full), "full picker and runtime key catalog differ"
for blocks, codes, units in ((compact_blocks, compact, 15), (full_blocks, full, 22)):
    for block in blocks:
        width = 0
        for kind, arguments in re.findall(r"\b(k|gap)\(([^)]*)\)", block):
            parts = [part.strip() for part in arguments.split(",")]
            width += float(parts[0].rstrip("f")) if kind == "gap" else (
                float(parts[1].rstrip("f")) if len(parts) == 2 else 1)
        assert width == units, f"keyboard row overflows/underflows its {units}-unit grid: {width}"
assert "KEY_0 = 7" in catalog and "NUM_LOCK = 143" in catalog and "NUMPAD_ENTER = 160" in catalog

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
assert "private void finishBinding(int first, int second)" in touch
for action in ('drawPill(c, addRect', 'drawPill(c, bindRect', 'drawPill(c, hideRect', 'drawPill(c, removeRect'):
    assert action in touch, f"layout editor action missing: {action}"
assert 'removeArmed ? "CONFIRM" : "REMOVE"' in touch, "custom-button removal is not confirmed"
assert "selected.hidden = !selected.hidden;" in touch and '"HIDDEN"' in touch
assert "customButtons.remove(selected);" in touch and "rebuildAllButtons();" in touch
assert 'prefs.getString(CustomButtonStore.PREFERENCE, "[]")' in touch
assert 'PREFERENCE = "custom_buttons_v1"' in store and "MAX_BUTTONS = 24" in store
assert "validBinding" in store and "ids.add(id)" in store and "saneCoordinate" in store
assert 'prefs.getFloat("pos" + i + "x", -1)' in touch and 'prefs.getFloat("size" + i, 1)' in touch
assert "for (int i = 0; i < buttons.length; i++)" in touch, "built-in index persistence loop missing"
assert "allButtons.addAll(customButtons);" in touch, "custom records reindexed the built-in array instead of appending"

# Picker taps select only; normal taps inject. Modifiers latch for a sequential mobile chord and release after a key.
down = keyboard.split("case MotionEvent.ACTION_POINTER_DOWN:", 1)[1].split("case MotionEvent.ACTION_UP:", 1)[0]
assert "if (!picker && !ControlKeys.isModifier(key.code)) callback.key(key.code, true);" in down
assert "if (picker)" in keyboard and "chooseBinding(key.code);" in keyboard
assert "latchedModifiers.add(key.code);" in keyboard and "releaseModifiers();" in keyboard
assert "CustomButtonStore.validBinding(bindingFirst, bindingSecond)" in keyboard
assert "layout(lastViewWidth, lastViewHeight);" in keyboard
assert "lastViewWidth = viewWidth;" in keyboard and "lastViewHeight = viewHeight;" in keyboard
assert "case MotionEvent.ACTION_CANCEL:" in keyboard
assert keyboard.count("releaseAll();") >= 5, "show/hide/toggle/cancel cleanup paths were removed"
cancel = keyboard.split("case MotionEvent.ACTION_CANCEL:", 1)[1].split("default:", 1)[0]
assert "releaseAll();" in cancel

on_size = touch.split("protected void onSizeChanged", 1)[1].split("private void layoutButtons", 1)[0]
on_detach = touch.split("protected void onDetachedFromWindow", 1)[1].split("protected void onDraw", 1)[0]
assert "releaseAll();" in on_size and "releaseAll();" in on_detach
assert "if (keyboard.isVisible())" in touch and "keyboard.onTouch(e, action, id, x, y);" in touch
assert "if (editing)" in touch and "keyboard.draw(canvas);" in touch, "picker is not drawn above the editor"

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

print("In-game keyboard, 104-key picker, custom chords, cleanup, and saved-layout checks passed")
