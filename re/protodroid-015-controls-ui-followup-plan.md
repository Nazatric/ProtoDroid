# ProtoDroid 0.15 — controls UI follow-up

Date: 2026-11-02. Branch `arena/78b126d7-protodroid`; **do not merge or release 0.15**. This is a presentation/editor follow-up to tested source `ba1b01f26deffb1c538cc9d1b08c75d9986e4098` and CI run `41379542243`.

## Supplied evidence

Two physical screenshots were supplied:

- The custom-key picker occupies almost the entire display with 104 similarly styled red rectangles. Numpad labels such as `N0` read as `NO`, numpad and main-key labels are inconsistent, and arrow glyphs can be mistaken for Android navigation.
- The layout editor's Add/Reset/Done and selection controls float over the game controls and underlying game text. The selected built-in `123` has no visibility action because only custom buttons had hide/remove actions. The screen is visually noisy and the hierarchy is unclear.

These are direct UI acceptance failures despite the previous build/tests passing. They are not evidence that the real key injection or persistence regressed.

## Research and applied conclusions

- Android accessibility guidance recommends 48dp touch targets where practical, with separation; visible icons may be smaller inside a larger target. Apply this to editor actions and keyboard cells while reducing panel coverage.
- TeamGDB/Yakumo PR #179 describes selected-control rebind/resize/hide/reset editing and independent touches. OpenCE PR #58 feedback explicitly requests readable selected-item labels, per-item hide, and editing from settings rather than only over gameplay.
- Winlator's controls-editor documentation uses selection followed by explicit delete/edit actions. This supports a stable selection panel rather than scattered controls.
- General mobile-control guidance consistently favors fewer clear buttons, thumb-comfort placement, and avoiding overcrowding. Context-sensitive controls are a future design task, not something to fake with unsupported engine-state detection in this batch.

## Implementation design

### Keyboard

- Replace the single 104-key wall with three clearly named pages: **MAIN**, **MORE**, and **NUMPAD**. Every one of the same 104 functional keys remains available exactly once across the pages.
- MAIN is the compact four-row everyday page with Escape, Tab, QWERTY, Enter, Backspace, Shift/Ctrl/Alt, Space, and all four arrows. MORE contains number/symbol keys, F1–F12, Print/Scroll/Pause, navigation, and Windows/Menu keys. NUMPAD is a narrow grouped keypad.
- Cap panel coverage and width by page; MAIN is substantially shorter than the old 80%-height keyboard, and the picker no longer uses 95% of the screen. NUMPAD is narrower than MAIN/MORE.
- Use dark keycap shapes with restrained borders and red only for active/selected states. Keep Prototype's black/red identity without a bright red grid around every key.
- Use familiar key labels: LEFT/RIGHT/UP/DOWN rather than navigation-like arrow glyphs; ordinary digits on the numpad rather than N0/N1; SHIFT/CTRL/ALT/WIN keycaps while retaining detailed binding names elsewhere.
- Keep the existing real SDL input, sequential modifiers, pointer tracking, one/two-key picker, close X, and all release/cancellation paths.

### Layout editor

- Add an opaque top editor bar with a clear title and separate Add/Reset/Done actions so toolbar text never overlaps the controls or game HUD.
- Move selection actions into an opaque bottom panel: selected name/size, −/+, Hide/Show for **every button and the stick**, Rebind for customs, and confirmed Delete for customs. Dim unselected controls instead of making the whole screen compete for attention.
- Persist built-in visibility using new append-only `hide<N>` preferences and stick visibility using `stick_hidden`; do not change existing position/size indices. Reset restores visibility and geometry.
- Preserve custom-button create/rebind/delete, one/two-key chords, dragging, sizing, normalized positions, old saved layouts, and input cleanup.

### Launcher access

- Add **Settings → Controls → Touch layout → EDIT**.
- Open a small standalone `TouchLayoutActivity`, not `ProtoActivity` and not the game engine. It shares the same `controls` preferences and enters the same editor directly.
- Skip native FPS/limiter calls in standalone mode. Picker operations never inject game keys; Done/system Back saves and returns to the launcher.
- Register the activity as landscape/fullscreen and preserve launcher transitions/immersive behavior.

## Verification

- Source tests must prove MAIN/MORE/NUMPAD contain 45/42/17 unique keys, their union is exactly the validated 104-key catalog, and keyboard coverage stays within the new caps.
- Check standard keycap labels, page switching/cleanup, opaque editor regions, built-in/stick visibility persistence, custom delete confirmation, and old position/size preference compatibility.
- Check the launcher row/action, standalone activity registration, no native live-setting calls in standalone construction, and save/finish behavior.
- Run the existing Java model tests in CI, all previous host/ARM64 regressions, fresh SDL patching, Android Java compilation, and exact-commit ARM64/APK CI.
- Physical acceptance: compare new screenshots with the supplied failures; exercise all three keyboard pages, arrow labels, built-in hide/show, stick hide/show, custom create/rebind/delete, launcher-open/save/reopen, rotation, and in-game input. CI is not physical UI approval.

## Current status

Implementation is complete in the working tree: paged MAIN/MORE/NUMPAD keyboard, conventional keycaps, opaque editor bars, built-in/stick visibility persistence, standalone launcher editor, documentation, and regressions. Local structural, compatibility, native-mod, SDL-patch, launcher/update/release, immersive, diagnostic-tour, Vulkan, and patched-DXVK checks pass. Changed Java sources parse and the manifest is valid XML. Local `javac`/Android SDK remain unavailable, so Android Java/D8 and ARM64 compilation still require exact-commit CI. Next: review, commit, push, and run CI; no physical UI/input approval is claimed.
