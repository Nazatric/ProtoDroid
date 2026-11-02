# ProtoDroid 0.15 — in-game keyboard and layout editor

Date: 2026-11-02. Working branch only; **do not merge or release 0.15**. This batch does not change the accepted AA, graphics, audio, save, mod, driver, updater, or recovery policies.

## Report and evidence

The report was that the Android keyboard did not write anything in Prototype's cheat-code menu, followed by a request for a compact in-game PC keyboard and custom buttons with one- or two-key bindings.

### Seen (source or binary)

- `TouchControls.java` has fixed built-in buttons, index-based saved positions/sizes, drag/resize/reset editing, and an `ABC` action that opens SDL/Android's IME. It has no custom-button or binding model.
- `input.cpp` converts SDL scancodes to Windows virtual keys and set-1 scancodes and forwards `SDL_KEYDOWN`/`SDL_KEYUP` to `EngineKeyboardService` and DirectInput.
- A read-only analysis copy of the original Steam `prototypef.exe` (SHA-256 `6d5b92b9dfcdf5edfdee9b63c1eb8a3ac8369ca5622b71fc2e44c11bc21fd548`) has a message loop at `0x401a61` that calls the keyboard service only for `WM_KEYDOWN` (`0x100`) and `WM_KEYUP` (`0x101`). It calls `TranslateMessage`/`DispatchMessageA` afterward, but does not forward `WM_CHAR` to that service. `EngineKeyboardService` is at `0x1085e560` in the matching original engine DLL (SHA-256 `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071`). Originals remained unchanged; only copies under `/tmp/protodroid-input-re` were inspected.
- SDL 2.30.9's Android backend produces key events through `onNativeKeyDown`/`onNativeKeyUp`. Its IME also produces `SDL_TEXTINPUT` and can synthesize key events for ASCII. Forwarding both as the same game action would risk duplication.
- SDL 2.30.9 and the current SDL2 branch map Android `KEYCODE_NUM_LOCK` (143) to `SDL_SCANCODE_UNKNOWN`. Other standard 104-key keyboard keys needed here have Android-to-SDL mappings.
- Android keycode values were checked against AOSP `KeyEvent.java`; SDL mappings were checked in the pinned `release-2.30.9` source.

### Inferred, bounded by evidence

- The existing SDL key path is the correct path for the in-game keyboard and custom buttons. This batch should not invent `WM_CHAR` delivery to an engine service that the original launcher did not call that way.
- Prototype's eight-position cheat screen is directional-entry UI, not a general text field. A mobile keyboard needs immediately accessible arrow keys there. The compact layout will therefore include arrows on its main page.

### Unknown / physical acceptance boundary

- No physical Android run in this session proves a particular device's IME failure mechanism, engine-screen response, touch feel, or complete keyboard compatibility.
- No engine-specific screen detector, automatic cheat entry, trainer, macro sequence, or unsupported automation will be added. The keyboard is user-operated and sends ordinary keys only.
- CI, Java/native tests, and screenshots do not substitute for the physical cheat-screen and gameplay checks listed below.

## Research sources

- SDL 2.30.9 `SDLActivity.java`, `SDL_android.c`, `SDL_androidkeyboard.c`, and `SDL_keyboard.c`: Android key/IME event ordering, text events, virtual-key synthesis, and keycode coverage.
- Microsoft Win32 keyboard-input documentation: `TranslateMessage` turns key messages into character messages; character and virtual-key messages are distinct.
- IGN and CheatCC Prototype cheat instructions: Body Surf uses Right, Right, Left, Down, Up, Up, Up, Down (also documented as D, D, A, S, W, W, W, S).
- GameFAQs Prototype thread “How do you input cheat codes?”: the eight-dot screen responds to direction buttons; users reporting that typing does nothing matches that UI.
- AOSP `KeyEvent.java`: numeric Android keycode identity; all 104 constants in the new catalog were cross-checked without a mismatch.
- Windows virtual-key references and DirectInput keyboard scan-code tables: `VK_SNAPSHOT/LWIN/RWIN/APPS` and `DIK_SYSRQ/LWIN/RWIN/APPS`, used to complete the native 104-key map.
- Repository issue search found no keyboard/layout-specific report to use as additional evidence.

## Implementation plan

### 1. Pure key catalog, persistence, and key ownership

- Add a pure-Java catalog covering all **104 standard PC keys**: main typing block, Escape/F1–F12, Print Screen/Scroll Lock/Pause, navigation/arrows, Windows/Menu modifiers, and the standard numpad.
- Use Android keycode integers at the existing SDL boundary. Validate the catalog for unique codes/names and expected standard-key coverage.
- Add a versioned `custom_buttons_v1` JSON preference for custom records only: stable ID, primary key, optional second key, normalized position, size, and hidden state. Existing built-in `pos<N>x`, `pos<N>y`, and `size<N>` preferences remain byte-for-byte compatible in meaning and are never reindexed.
- Validate malformed JSON, unknown/unbound keys, duplicates that make a chord meaningless, non-finite/out-of-range geometry, excessive records, and size limits. A bad custom record must not prevent the game or built-in controls from loading.
- Add reference-counted touch-key ownership. Two controls bound to the same key must not release each other's hold; unbalanced releases are ignored and global cleanup releases every remaining owned key.

### 2. Compact in-game keyboard (replace Android IME action)

- `ABC` opens a Prototype-styled in-game keyboard instead of Android's IME. No system IME is required.
- Main page: compact typing keys plus Enter, Escape, Backspace, Shift/Ctrl/Alt, Space, and all four arrows. Additional full-key/numpad access remains available without engine-specific automation.
- Keyboard touches are consumed by the overlay, never turned into camera movement or menu clicks behind it. Multi-touch ordinary keys are tracked by pointer.
- Shift/Ctrl/Alt support safe sequential mobile chords: tap a modifier, then a key; modifiers release after that key or when toggled off. Close, cancellation, mode/editor transitions, rotation, and detach release held and latched keys.
- A small, high-contrast `X` closes the keyboard. Opening is harmless and closing never sends a gameplay click.
- The keyboard sends real `SDLActivity.onNativeKeyDown`/`onNativeKeyUp` input through the existing native Windows/DirectInput conversion.

### 3. Custom buttons and substantially better editor

- Editor gains `ADD`; it opens a full 104-key PC-keyboard picker before creating a button.
- Select one key or two distinct keys. Show both selections, allow undo/clear, cancel without changing the old binding, and require at least one key before Done.
- A custom button presses its keys in selection order and releases in reverse order. It uses the same reference-counted real-key path as built-ins.
- Existing custom buttons can be rebound, dragged, resized, hidden/shown, and removed. Hidden buttons remain visible and identifiable in the editor so they are recoverable.
- Reset restores geometry/sizes without silently deleting bindings or changing built-in preference indices. Removal requires a deliberate confirmation.
- Preserve stick behavior, sprint ownership, power-wheel dragging, menu mouse clicks, camera touches, gamepad hiding, opacity/scale, and performance/settings behavior.

### 4. SDL coverage and build verification

- Add one narrowly scoped SDL2 patch mapping `AKEYCODE_NUM_LOCK` to `SDL_SCANCODE_NUMLOCKCLEAR`, so the displayed standard numpad key is functional rather than decorative.
- Complete the existing native map with the four missing standard keys: Print Screen, left/right Windows, and Application Menu. The resulting map has 104 unique SDL entries with standard Windows VK and DirectInput set-1 identities; no `WM_CHAR` path is added.
- Apply the patch through the existing ordered patch system. Test a fresh SDL 2.30.9 clone, ordered application, and the exact resulting mapping. The dependency cache already hashes `android/patches/**`.

### 5. Tests and acceptance

Local/source gates:

- Pure Java tests for the 104-key catalog, JSON round trips/migration-independent built-ins, corruption handling, limits, one/two-key validation, hidden/remove state, and key reference counting/chord-like simultaneous ownership.
- Source/policy tests for compact arrows, in-game rather than IME behavior, full picker, reverse chord release, all cleanup paths, stable built-in index persistence, and the Num Lock mapping.
- Fresh SDL patch application and existing broad compatibility/mod/save/driver/updater regressions.
- Java 11 compilation against Android and the exact pinned SDL Java source in CI; native ARM64 and APK/signature/package checks in the real workflow.

Exact-commit gate:

1. Commit and push the complete batch to `arena/78b126d7-protodroid`.
2. Run the real `build-apk.yml` workflow for that exact commit; do not build an APK for each edit.
3. Require ARM64 native builds, tests, Java/D8, packaging, signing, and embedded dependency checks to pass. Confirm release remains skipped and no 0.15 PR/release is created.
4. Record the run and artifact identity in this plan. A later documentation-only commit is not represented as the tested source commit.

Physical acceptance (not claimed by CI):

- Open `ABC`, tap `X`, and verify no click/key remains stuck.
- In Extras → Cheats, enter Right, Right, Left, Down, Up, Up, Up, Down with the on-screen arrows; verify the eight positions respond and the valid-code result. Also check W/A/S/D equivalents if using the documented keyboard mapping.
- Check ordinary menu Enter/Escape and several typing/navigation keys without Android's IME.
- Add one-key and two-key buttons, verify real game response, simultaneous holds with another control, hide/show, rebind, remove confirmation, rotation, background/foreground, and process restart persistence.
- Load a pre-batch saved layout and verify every built-in position/size and the stick are unchanged.

## Current status

Research, plan, and the complete implementation batch are present in the working tree. Local validation has passed:

- Exact 63-key compact / 104-key full layout identity, row geometry, catalog/picker equality, native 104-key map, cleanup, persistence, and old-layout source checks.
- Fresh SDL 2.30.9 ordered patch application and Num Lock mapping; all 104 Android constants cross-checked against AOSP.
- Fresh DXVK 2.5.3 ordered patches, FXAA/depth-resolve structure, and actual unified-memory function tests.
- Native UBSan graphics, mod, plugin, Lua (normal and ASan/UBSan), save, diagnostics, driver, state-cache, video-setting, AA, CPU, and presented-frame suites.
- Repository compatibility, updater, release/workflow, issue-form, diagnostic-tour, native-mod, and Vulkan-wrapper tests; Java syntax parsing and workflow YAML parsing; `git diff --check`.

The sandbox has no JDK/Android SDK, so the new Java model tests were not run locally. They subsequently ran and passed in the real CI test job along with Android/SDL Java compilation and the ARM64/APK build. Physical Android acceptance remains pending. No PR or release has been created.

## Exact-commit CI result

- **Tested source:** `ba1b01f26deffb1c538cc9d1b08c75d9986e4098` (`Add in-game keyboard and custom touch chords`).
- **Workflow:** `build-apk.yml`, run **41379542243**, push event on `arena/78b126d7-protodroid`: **success**. The run head SHA exactly matches the source above and has no pull request.
- The new keyboard/custom-button Java model tests passed, as did Android Java compilation, all host/ARM64 test jobs, fresh dependency patching, native ARM64 builds, D8/packaging, signing, and APK/embedded-DXVK verification.
- Final `build-apk` job: **16m31s, success**. The `release` job was **skipped**. Latest release remains 0.14; no 0.15 release or PR exists.
- Artifacts at query time (none expired): `prototype-apk` **13113462817** (58,332,457 bytes), `debug-symbols` **13112947968**, `make-logs` **13113422861**, `android-deps-arm64-v8a` **13112607690**, `protodroid-mod-example` **13113263642**, and `android-turnip-arm64-v8a` **13113345083**. These are CI artifacts, not a release.
- CI proves source/build/test integrity, not touch feel or the physical Prototype cheat-screen result. Use the physical acceptance list above on Android before calling that report resolved on a device.
