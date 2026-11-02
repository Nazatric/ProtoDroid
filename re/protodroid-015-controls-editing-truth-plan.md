# ProtoDroid 0.15 — control editing, mouse bindings, snapping, and documentation truth

Date: 2026-11-02. Branch `arena/78b126d7-protodroid`; **keep 0.15 unmerged and unreleased**. Baseline: docs `ab72335`, exact-tested UI source `aa50df08778ee336452955068481955c3491ded7`, CI `41383625342`.

## Request and evidence

The user wants a logical editor whose button text is editable, whose bindings are visible on every button, whose custom bindings can include mouse buttons, whose stick is easy to restore, and whose polygon buttons snap together. The user also asks to remove fake/unreal claims from the README and other documentation.

The previous UI made custom keyboard chords work but did not persist custom text, show built-in binding text, or support custom mouse records. Stick Hide/Show technically existed, but restoration depended on understanding selection; that is not an obvious recovery flow.

GitHub issue search found no additional editor-specific report. Issue #26 reports missing touch controls during gameplay but does not establish an editor/root cause; this batch does not claim to fix it.

## Research applied

- Droid-Deck PR #490: center-line snapping, visible guides, haptic feedback on a new snap, and clearing guides on release. ProtoDroid additionally needs edge snapping for its pointy hexagons.
- Droid-Deck issue #571: explicit keyboard/mouse custom bindings and snapping are expected controller-editor operations; a follow-up comment also shows that save/recovery must remain obvious when controls are hidden.
- OpenCE PR #58: stable per-control identity, per-control size, readable labels, and launcher editing remain relevant. Existing built-in preference indices must not be reordered.
- Existing Android/Java dialogs provide a real IME text editor; no fake canvas text editor is needed.

## Implementation design

### Labels and binding visibility

- Add **TEXT** to the selected-button panel for built-in and custom buttons. Use a platform `EditText`/`AlertDialog` with Save, Default, and Cancel; trim control characters and cap labels at 16 Unicode code points.
- Persist built-in label overrides as append-only `label<N>` preferences. Keep existing `pos<N>x`, `pos<N>y`, `size<N>`, and `hide<N>` contracts unchanged.
- Extend custom JSON records with optional `label` and `mouse` fields while retaining `custom_buttons_v1` and decoding every old key/chord record. Missing fields mean automatic label and keyboard binding.
- Preserve an edited custom name across rebinding. Automatic names continue to follow the current binding. Reset restores default/automatic labels without deleting custom buttons.
- In edit mode, draw each button's name and binding inside its polygon; show the same binding in the selection panel. Normal gameplay stays uncluttered. Built-in special roles and bindings remain fixed; arbitrary rebinding stays on custom buttons so wheel, 123, ABC, and sprint behavior cannot be silently broken.

### Mouse bindings

- Add a picker-only **MOUSE** page with LEFT CLICK, RIGHT CLICK, and MIDDLE CLICK.
- A custom record is either a valid one/two-key keyboard chord or one mouse bit (1/2/4), never an ambiguous mixed chord. Add validation and host model tests for round trips, malformed records, mixed records, and old records.
- Route custom mouse press/release through the existing real SDL mouse path and existing cancellation cleanup. Do not pass mouse bits through `TouchKeyState`.

### Stick recovery and polygon snapping

- Add a persistent toolbar **STICK** action. It selects the stick normally and becomes **ADD STICK** when hidden, restoring and selecting it without requiring Reset.
- Snap dragged buttons/stick to other controls' horizontal/vertical center lines and to side-by-side hexagon edge adjacency using `sqrt(3)/2 * (r1 + r2)` (the actual hexagons have vertical sides). Show guides during the drag, clear them on release/cancel, and buzz only when the snap target changes.
- Clamp dragged centers to the screen instead of allowing controls to be lost beyond its edges. Preserve normalized saved positions and multi-touch ownership.

### Documentation truth audit

Verified code supports FIFO interval 1, ADPF hints, logical-CPU reporting, Java Android UI, plugin ABI v3, and no-overwrite save migration. Correct inaccurate or inflated wording rather than removing real features:

- Replace “stable FIFO” and “unrestricted CPU scheduling” with exact FIFO/ADPF/Android-controlled scheduling wording.
- Replace “native Android layer” with the actual Java Android UI layer.
- Replace the stale “ABC Android keyboard input” changelog wording with the real paged PC keyboard and editor capabilities.
- Keep compatibility/device outcomes qualified. Do not invent universal support, physical success, context-sensitive controls, or FPS claims.

## Verification

- Pure-Java tests: old/new custom-record round trips, labels, mouse validation, key/mouse exclusivity, key ownership, and keycap labels.
- Source regressions: all 104 keyboard keys still partitioned once, picker-only mouse page, all-button binding display, TEXT dialog/persistence, STICK restore, hex snapping/guides/cleanup, unchanged built-in indices, and launcher standalone isolation.
- Existing host/ARM64/native/mod/driver/update/readme policy suites and fresh SDL patching must remain green.
- Finish with exact-commit Android Java/D8 and real ARM64 CI/APK verification. No merge, release, or physical-success claim.

## Current status

Implementation and local validation are complete: optional labels/mouse fields preserve old records; the picker has an isolated mouse page; every editor control exposes its binding; TEXT/DEFAULT, ADD STICK, side-edge/center snapping, and cleanup are implemented. README/changelog wording is corrected. Structural, compatibility, SDL, release, updater, issue-form, and patched-DXVK checks pass; modified Java syntax passes. No local JDK/Android SDK or physical device is available, so exact-commit CI remains required for Java model execution, Android compilation, ARM64 native packaging, and artifact verification. 0.15 remains unmerged and unreleased.
