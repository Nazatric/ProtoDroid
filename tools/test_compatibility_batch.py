#!/usr/bin/env python3
"""Source regressions for the 0.15 controls, texture-quality, and orientation batch."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
touch = (ROOT / "android/java/com/protodroid/TouchControls.java").read_text(encoding="utf-8")
settings = (ROOT / "android/java/com/protodroid/GameSettings.java").read_text(encoding="utf-8")
activity = (ROOT / "android/java/com/protodroid/ProtoActivity.java").read_text(encoding="utf-8")
aa_policy = (ROOT / "android/java/com/protodroid/AaPolicy.java").read_text(encoding="utf-8")
manifest = (ROOT / "android/AndroidManifest.xml").read_text(encoding="utf-8")
d3d9 = (ROOT / "src/native/d3d9.cpp").read_text(encoding="utf-8")
main = (ROOT / "src/native/main.cpp").read_text(encoding="utf-8")
readme = (ROOT / "README.md").read_text(encoding="utf-8")

# Saved custom layouts are index-based: every new action must follow the existing last button.
ordered = ['new Button("ALEX"', 'new Button("123"', 'new Button("ABC"', 'new Button("DESCEND"']
positions = [touch.index(item) for item in ordered]
assert positions == sorted(positions), "new controls reindexed an existing saved touch layout"
assert 'new Button("DESCEND", KeyEvent.KEYCODE_V' in touch
assert 'new Button("1 ATK", KeyEvent.KEYCODE_1' in touch
assert 'new Button("2 DSG", KeyEvent.KEYCODE_2' in touch
assert 'new Button("3 DEF", KeyEvent.KEYCODE_3' in touch
assert 'new Button("4 SENSE", KeyEvent.KEYCODE_4' in touch
visible = touch.split("private boolean visible(Button b)", 1)[1].split("private void layout", 1)[0]
assert "b.numberKey && !numberPad && !powerButtons" in visible
assert "b.quickPower && !b.numberKey && !powerButtons" in visible
press = touch.split("private void press(Button b, int pointer)", 1)[1].split("private void release", 1)[0]
assert "numberPad = !numberPad;" in press
assert "keyboard.show();" in press, "ABC must open ProtoDroid's in-game PC keyboard"
assert "SDLActivity.showTextInput" not in touch, "the old Android IME action returned"
assert press.index("if (b.keyboard)") < press.index("key(b.keycode, true)"), "keyboard action must not become a keycode"

assert '"game_aa_level", "game_shadow_detail", "game_graphic_quality", "decode_16bit"' in settings
assert "{0, 2, 0, 1, 1, 1, 1}" in settings
assert "{1, 1, 0, 4, 2, 2, 1}" in settings, "Medium must avoid built-in MSAA and select FXAA"
assert "{3, 0, 3, 4, 3, 3, 0}" in settings, "High must select FXAA and full 32-bit DXT1 color"
assert 'new String[] {"Game default", "Off", "2x MSAA", "4x MSAA", "FXAA"}' in settings
assert 'new int[] {-1, 0, 1, 2, AaPolicy.FXAA_VALUE}' in settings, "AA preference indices were not append-only"
assert "AaPolicy.migratePresetAaIndex(graphicsPreset, aa.index)" in settings
assert "preset == MEDIUM_PRESET && currentIndex == OLD_MEDIUM_AA_INDEX" in aa_policy
assert "preset == HIGH_PRESET && currentIndex == OLD_HIGH_AA_INDEX" in aa_policy
assert 'setenv("PROTODROID_FXAA", fxaa ? "1" : "0", 1)' in main
assert "AaDxvkConfig(SettingInt(\"game_aa_level\", kAaGameDefault), dxvk_config)" in main
assert "AaPresentSampleType(aa, uint32_t(p.MultiSampleType))" in d3d9
assert "AaPresentSampleQuality(aa, p.MultiSampleQuality)" in d3d9
default_index = settings.split("int defaultIndex()", 1)[1].split("static boolean isLiveEngineSetting", 1)[0]
decode_default = default_index.split('key.equals("decode_16bit")', 1)[1].split("return 1;", 1)[0]
assert "return 0;" not in decode_default, "standalone DXT1 default changed from reduced 16-bit"
assert 'Setting("DXT1 color", "decode_16bit", new String[] {"32-bit", "16-bit"}' in settings
assert 'SettingInt("decode_16bit", 1)' in d3d9, "UI key is not connected to the real native decoder"
assert 'SettingInt("decode_16bit", 0)' in d3d9, "non-A1R5 fallback native default changed"

assert 'Setting("Screen orientation", "display_orientation", new String[] {"Auto", "Landscape", "Reverse"}' in settings
assert 'getInt("display_orientation", 0)' in activity
assert "if (orientation == 1)" in activity and "else if (orientation == 2)" in activity
assert "SCREEN_ORIENTATION_LANDSCAPE" in activity and "SCREEN_ORIENTATION_REVERSE_LANDSCAPE" in activity
on_create = activity.split("protected void onCreate", 1)[1]
assert "orientation == 0" not in on_create, "Auto must retain the manifest sensorLandscape behavior"
assert 'android:screenOrientation="sensorLandscape"' in manifest
assert "https://github.com/Nazatric/ProtoDroid/releases" in readme and "**VERSIONS**" in readme

# Static-texture scratch is released only after every consumer. Ordinary/dynamic shadows keep their reuse policy.
adapted_unlock = d3d9.split("bool AdaptedUnlock(void* tex", 1)[1].split("void ForgetResource", 1)[0]
assert adapted_unlock.index("ModTexturesReplace") < adapted_unlock.index("DecodeDxt")
assert adapted_unlock.index("DecodeDxt") < adapted_unlock.index("HeapFree(l.guest)")
assert adapted_unlock.index("HeapFree(l.guest)") < adapted_unlock.index("g_adapted_locks.erase(it)")
shadow_unlock = d3d9.split("void ShadowUnlock(void* object", 1)[1].split("void ForgetShadows", 1)[0]
assert shadow_unlock.index("ModTexturesReplace") < shadow_unlock.index("D3D9ShadowCopyBack")
assert shadow_unlock.index("D3D9ShadowCopyBack") < shadow_unlock.index("if (release_scratch)")
assert shadow_unlock.index("if (release_scratch)") < shadow_unlock.index("HeapFree(s.guest)")
texture_unlock = d3d9.split("M(IDirect3DTexture9, UnlockRect)", 1)[1].split(
    "M(IDirect3DCubeTexture9, LockRect)", 1
)[0]
assert "ShadowUnlock(t, level, &guest_desc, a != nullptr)" in texture_unlock
assert "ShadowUnlock(t, level, nullptr, a != nullptr)" in texture_unlock
cube_unlock = d3d9.split("M(IDirect3DCubeTexture9, UnlockRect)", 1)[1].split(
    "// D3DLOCKED_BOX", 1
)[0]
assert "ShadowUnlock(t, face * 64 + level, nullptr, a != nullptr)" in cube_unlock
print("Touch keyboard, AA/quality, orientation-default, archive, and texture-scratch lifetime checks passed")
