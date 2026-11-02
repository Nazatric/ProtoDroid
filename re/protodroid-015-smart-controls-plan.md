# ProtoDroid 0.15 — smart touch controls research and staged plan

Date: 2026-11-02. Branch: `arena/78b126d7-protodroid`. 0.15 remains unmerged and unreleased.

## Non-negotiable behavior

- **Classic remains the default and a complete fallback.** Smart is optional and uses the existing SDL keyboard/mouse path, multitouch ownership, custom bindings, layout editor, keyboard, and controller support.
- The built-in right-mouse control is **SPECIAL** ordinarily and **CONSUME** only while fresh engine evidence says Prototype's consume-description validity message is true. Unknown, stale, inconsistent, unavailable, or disabled evidence never produces CONSUME.
- Smart presentation must not change the physical binding. A custom button bound to right mouse is not relabeled; user text, positions, sizes, visibility, chords, and Classic behavior remain intact.
- No tap-versus-swipe or double-tap decision is added to a combat action. Button down continues to emit input immediately. No claim of literally zero latency is made.
- Unknown context never hides an essential control. Vehicle, power, disguise, interaction, grab/throw, and unlock availability are not inferred from a label or from proximity.
- Original game files are read-only. Engine analysis uses a separate copy; runtime integration uses the existing guarded native detour mechanism.

## Mobile-port and input research

### Official ports and platform guidance

- Rockstar's [GTA: San Andreas mobile announcement](https://www.rockstargames.com/newswire/article/o349k55252497o/grand-theft-auto-san-andreas-mobile-coming-this-december.html) explicitly describes contextual buttons displayed only when needed and three control schemes. Design consequence: context can reduce clutter, but a complete alternative scheme must remain available.
- Feral's [Guardian of Light mobile design diary](https://www.feralinteractive.com/en/games/laracroftguardianoflightmobile/android-ios/dev-diary/) recommends few ergonomic buttons, context/location prompts only when required, two-stage button art, and traditional-button alternatives to gestures. Design consequence: use immediate buttons for combat; do not make a gesture the only route.
- Feral's [Hitman: Blood Money — Reprisal interview](https://toucharcade.com/2023/12/13/hitman-blood-money-reprisal-nintendo-switch-release-window-frame-rate-mobile-features-iphone-android/) says its touch UI is as context-sensitive as possible to avoid clutter, retains Classic/Modern choices on other inputs, and supports customization. Its official store listing also confirms customizable touch, controller, and keyboard/mouse support. This is design evidence, not evidence about Prototype engine hooks.
- The official [Saints Row: The Third Android listing](https://play.google.com/store/apps/details?id=com.feralinteractive.saintsrow3_android&hl=en_IN) confirms a current PC/console-to-Android port with customizable touch controls. Store reviews are not device-validation evidence for ProtoDroid.
- [Android accessibility guidance](https://developer.android.com/guide/topics/ui/accessibility/apps) recommends at least 48 × 48 dp touch targets. The existing controls use expanded hit radii; Smart must not shrink them.
- [Android controller/input guidance](https://developer.android.com/games/sdk/game-controller/controller-input) documents batched motion samples. Preserve the existing direct event path and controller handling. `requestUnbufferedDispatch` is not a universal latency fix: Android's own API documentation warns that it can increase latency/jitter and disable useful resampling. Do not enable it speculatively.

### Community and related-project evidence

- Reddit discussions of organic/emulated touch controls consistently report fat-finger errors, screen occlusion, difficult multitouch chords, and tap/swipe disambiguation delay. Consequence: keep the core cluster stable, preserve simultaneous buttons, and offer explicit chord controls rather than timing-sensitive gesture interpretation.
- [Player124413/proto-re](https://github.com/Player124413/proto-re) lists contextual combat buttons, power/disguise wheels, target lock, and vehicle layouts as **TODO**. A related project's roadmap is not proof those features work and is not an engine-state source.
- Reddit reports around the native Prototype port request missing vehicle keys and number prompts. The existing DESCEND, 123, and complete keyboard affordances therefore remain available; Smart must not hide them without evidence.

## Prototype action graph

Primary control evidence includes the official Steam manual search extract (`Manual_english.pdf`, app 10150), the [console control list](https://www.supercheats.com/guides/prototype/controls), [upgrade list](https://www.supercheats.com/guides/prototype/upgrades), and corroborating walkthroughs. The direct manual PDF fetch failed in this environment; only the indexed extract is treated as read evidence.

| Context | Original input/action graph | Smart policy and current truth |
|---|---|---|
| Movement / camera | WASD + mouse; left stick + right stick on console | Existing floating stick and swipe camera; no context claim. Direct input, simultaneous with buttons. |
| Sprint / parkour / glide | Shift / RT holds combine with movement and jump | Preserve hold ownership and stick-edge sprint. No new gesture delay. |
| Ordinary attack | LMB / X; taps, holds, and power-dependent combos | Same physical LMB. ATTACK is the ordinary label; no power-specific outcome claim. |
| Grab / pickup / throw | E / B grabs or picks up; LMB throws held objects; holds affect throws | GRAB remains available. THROW detection is not yet evidenced, so do not dynamically claim it. |
| Ordinary Special | RMB / Y; holds and combinations depend on equipped power | SPECIAL is the safe default and the unchanged physical input. |
| Held consumable humanoid | Grab first, then RMB / Y consumes | **Stage 1:** CONSUME only from the engine's actual transformation-consume validity message. A generic grab, nearby target, or elapsed timer is insufficient. |
| Held non-consumable object / invalid target | RMB is not thereby proven to be Consume | SPECIAL/unknown fallback; never infer Consume from HaveGrabTarget alone. |
| Air held-enemy follow-ups | Console/upgrade evidence describes conditional Powerbomb and other follow-ups | No separate POWERBOMB label until its priority and engine state are evidenced. |
| Stealth consume | Disguise-action context plus grab; conditional prompt | Do not reuse the ordinary held-target signal as proof of stealth availability or outcome. |
| Attack + Special | LMB+RMB / X+Y; hold variants and Critical Mass Groundspike Graveyard | Explicit chord input can preserve the physical chord; unlock, Critical Mass, and air/ground availability require separate evidence. |
| Special + Grab | RMB+E / Y+B; Knuckle Shockwave and Critical Mass Tendril Barrage variants | Same: chord delivery is not proof the move is unlocked or selected. |
| Attack + Grab | LMB+E / X+B; Palm Slam and Critical Pain variants | Same; no fabricated availability detection. |
| Powers / disguise wheels | R and F hold with mouse selection; quick powers 1–4; F1/F2 forms | Existing one-finger wheel drag and optional quick buttons remain. Selection/form outcome is unverified. |
| Number/QTE prompts | Number keys are needed by missions and QTEs | Keep 123 and full PC keyboard recovery. Do not hide them on a guess. |
| Tank / APC | Enter/hijack/exit plus drive, target, primary/secondary fire | Vehicle-specific labels and layout require a reliable player-vehicle state source. Unknown keeps every control available. |
| Helicopter | Enter/hijack/exit, pitch/roll, ascend/descend, weapon switch/fire | Keep Space and V available. No HELI layout claim until enter/exit and control outcome are evidenced. |
| Interaction / missions | Context-sensitive E/A and on-screen prompts vary by mission | No generic INTERACT relabel from proximity. Engine prompt observation is a possible later stage. |
| Menus / pause / Web | Enter, Esc, Web, mouse-driven menus | Existing MENU/GAME mode and buttons remain; gameplay probes must not leak into menu presentation. |

## Reverse-engineering evidence

Analysis followed the `aicodeking/leanrea` seen / inferred / unknown workflow. A separate analysis copy was made at `/tmp/protodroid-smart-controls-re/prototypeenginef.dll`; source and copy SHA-256 both equal `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071`. The original was not edited. A full objdump listing exists only in that temporary analysis directory.

### Seen in the original DLL and generated recompilation

- `re/functions.csv` names `proto::fightitems::motion::TransformationConsumeDescriptionValidCondition::vfunction10` at preferred VA `0x10122160` (RVA `0x00122160`, rebased `0x7c122160`).
- The function resolves its object handle, sends message class `0x1e`, subtype `0x0a`, and compares the returned byte with the condition's expected byte at `this+0x0c`. Recovered message RTTI names it `ConsumeTransformationDescriptionIsValidMessage`.
- If the object cannot be resolved, the function returns false **without sending the message**. Treating that early return as an ordinary false probe could invert a negated condition into a false CONSUME result. The integration must distinguish this path.
- Handle normalization is visible at `0x107fc7a0`: object markers `0x10000..0x2ffff` use the handle at object `+0x0c`; other inputs become the invalid `0x0000ffff` handle. The object table pointer is preferred VA `0x1128348c` (rebased `0x7d28348c`); index/generation and non-null entry checks mirror the original.
- `IsPlayerCharacterCondition::vfunction10` at `0x101d9040` resolves the same handle format through the same object table, then compares it with the player-character handle at preferred VA `0x1126e40c` (rebased `0x7d26e40c`). The Smart probe additionally requires this player handle before accepting a sample. Non-player and unresolved evaluations do not overwrite the player's last sample; if the player source stops evaluating, freshness still expires to UNKNOWN.
- The returned message byte is read at entry-ESP minus 4 after the original returns. The generated C confirms the same stack calculation. The detour can therefore inspect the actual message byte rather than infer it only from the condition's boolean result.
- For safety, the decoded probe is accepted only when the message byte and expected byte are boolean, and the original condition result equals `(message == expected)`. This covers negated conditions and detects an incorrect stack/path assumption.
- `HaveGrabTargetCondition` at `0x100c3500` and `ConsumeAction` at `0x100ac540` exist, but they are not substitutes for the validity message: a held target is not necessarily consumable, and ConsumeAction starts only after the input.

### Inferred, with bounded runtime use

- This condition is transformation-specific and is expected to be evaluated by Prototype's action-selection system. A fresh, player-associated, resolved, consistent true message is strong enough to drive the optional CONSUME presentation. The handle gate is grounded in the engine's own player test, but actual gameplay coverage still requires measurement.
- Evaluation frequency is not proven by static analysis. Therefore the snapshot has a short freshness lifetime and returns UNKNOWN when no recent resolved probe arrives. The Java presentation maps UNKNOWN to SPECIAL. Real gameplay must measure probe cadence and label stability before this is called device-validated.

### Still unknown

- Whether every held-consumable situation evaluates this exact condition frequently enough on every mission/form.
- Vehicle enter/exit and per-vehicle right-mouse meaning; stealth-consume prompt state; selected power/form outcomes; reliable grab/throw and interaction prompt state.
- Physical feel, multitouch behavior on real panels, end-to-end latency, and gameplay outcomes on ARM64 devices. Host/model/CI tests cannot establish these.

## Implementation stages and gates

### Stage 1 — opt-in mode and truthful Consume/Special presentation

1. Add a keyed **Touch controls: Classic / Smart** setting, default Classic, available in launcher Controls and the in-game panel. Preserve every existing preference key/index contract.
2. Add a small host-testable context reducer and native snapshot. Use a single packed atomic sample (monotonic milliseconds + state) so the UI never reads a torn context/timestamp pair. Initial freshness bound: 250 ms; unknown maps to Special.
3. Install the guarded consume-condition detour before engine startup. It always calls the original exactly once. Accept only the original resolved-object path whose handle also matches the engine's player-character handle. When Smart is disabled it does no probe reads beyond the original call. Missing recompiled target/Dobby failure leaves the probe unavailable and produces UNKNOWN, not a crash or a fabricated Consume.
4. Poll only while Smart gameplay presentation is active. Reset on mode/lifecycle transitions. Catch native-link failure as UNKNOWN.
5. Keep the same right-mouse down/up calls. Relabel only the built-in Special button; do not rewrite custom labels or custom right-mouse buttons.
6. Add lightweight vector iconography for the core Smart cluster: claw slashes for Attack, tendrils for Special, an absorption/biomass mark for Consume, grab brackets, and a jump chevron. Keep immediate pressed contrast and existing hit areas. Classic drawing remains unchanged.
7. Tests: all boolean/negated condition combinations, unresolved early-out, invalid bytes/inconsistent paths, stale/future timestamps, reset/disable/hook-unavailable fallback, Java label mapping, existing keyboard/mouse/custom persistence suites, Java syntax, workflow structure, and native compilation.

### Current Stage 1 implementation status

- The working tree now contains the Classic-first `touch_controls` setting in launcher Controls and the existing in-game live settings panel. `SmartControlsModel.java` owns the fail-safe Java label mapping; only the built-in Special presentation changes, while its `MOUSE_RIGHT` down/up path is untouched.
- `src/native/smart_controls.cpp` installs the existing Dobby detour before engine startup, ignores disabled/unresolved/non-player evaluations, verifies the raw message against the original condition result, and publishes one packed 64-bit atomic sample. It performs no guest-memory writes. Java polls at 50 ms only during visible, non-panel, non-editor Smart gameplay; native samples expire after 250 ms. These are bounded implementation choices, not measured end-to-end latency or probe-cadence claims.
- Smart adds cached vector marks to Attack, Special/Consume, Grab, and Jump. Context labels and icon IDs are in the existing bitmap cache keys, and pressed states use the existing immediate feedback. Classic uses the original labels and no Smart icons. Editor/custom-button labels and bindings are not rewritten.
- Lifecycle, game/menu mode, settings panel, editor, gamepad-hidden controls, and detach transitions stop polling and clear/disable the active native sample. Missing JNI/Dobby/recompiled targets retain Special. No input batching, gesture recognizer, or delayed action path was introduced.
- Focused host UBSAN tests cover positive and negated conditions, all inconsistent Boolean combinations, unresolved objects, invalid bytes, packed snapshots, and future/stale samples. A separate Java model test covers Consume/Special fallback mapping. Structural tests pin the DLL identity, player-handle evidence, dispatcher `ret 4` stack derivation, read-only probe, right-click binding, cache key, lifecycle cleanup, and CI inclusion. Local native compilation passes with and without Dobby; local Java syntax and structural suites pass. Full Java/Android compilation and real ARM64 CI remain required for the exact eventual commit.
- **Not yet established:** physical gameplay outcomes, actual probe cadence/stability, every mission/form's evaluation coverage, and real-device touch feel. No vehicle, stealth, throw, power, interaction, or combo availability detector is claimed. Stage 1 must not be described as device-validated merely because host or CI tests pass.

### Stage 2 — explicit chords and further contexts, only with separate evidence

- Mouse/keyboard chord delivery needs reference-counted ownership just like `TouchKeyState`; one control must not release another control's mouse bit. Add explicit combo controls only if they do not crowd the default cluster. Their presence must not claim unlock, Critical Mass, or air/ground eligibility.
- Investigate vehicle and HUD prompt messages with targeted read-only disassembly and runtime diagnostics. Add a context only when its enter/exit source, priority, freshness, and physical outcome are identified. Otherwise retain the Classic label/control and record the limitation.
- Add diagnostic-tour observations for probe cadence and context transitions without claiming target acquisition or consume success unless the tour independently verifies them. Persistent failures must remain detailed.

### Final gate for this batch

- Review the cumulative diff, including the pre-existing dirty/grafted work, without resetting it.
- Run focused host/model/structural tests and exact-current-commit real ARM64 CI. Verify the run's `github.sha`, native revision, APK signing, and artifact uploads.
- Do not merge, tag, or release 0.15. Do not claim physical-device approval; provide a concrete real-device checklist for consume/no-consume/negated or invalid states, chords, vehicles, wheels, rotation/cancel, controller coexistence, and Classic rollback.

## Required real-device gameplay checklist

Use the APK from the exact tested ARM64 commit and record device, Android version, GPU/driver, mission/form, and native log. CI success alone does not check any item below.

1. **Classic rollback:** launch with the default Classic mode; verify the original core labels, direct right-click Special, custom layouts/buttons, multitouch, wheels, keyboard, mouse, and controller behavior. Switch Smart off/on/off in the live panel and confirm there is no stuck input or missing control.
2. **Ordinary Special:** in Smart gameplay with no consumable held target, confirm the built-in button remains SPECIAL and performs the same right-click action as Classic. A custom button bound to right mouse must retain its own label.
3. **Valid held-target Consume:** grab a known consumable humanoid and record whether/when CONSUME appears. Press it and verify the actual game outcome. Repeat across representative forms/missions; an absent or flickering label is a failed cadence/coverage result, not something to explain away.
4. **Negative and invalid states:** repeat with no grab, a non-consumable object, after dropping/throwing, after target loss, and while moving in/out of range. CONSUME must never be inferred from grab alone. Unknown or expired evidence must return to SPECIAL without hiding the button.
5. **Transitions:** open/close the gear and editor, switch MENU/GAME, connect a controller with Hide with gamepad, rotate where permitted, background/foreground, and cancel touches. Verify polling/context does not leave a stale Consume label and every pressed key/mouse bit is released.
6. **Simultaneous input:** hold movement/camera with Attack, Special/Consume, Grab, Jump, Sprint, powers, and explicit custom chords. Check immediate pressed feedback and that one control never releases another control's binding. No gesture delay is acceptable.
7. **Unclaimed contexts:** vehicles, stealth consume, throws, power/form eligibility, interactions, and combo availability must retain their ordinary controls and must not be reported as detected. DESCEND, 123, ABC, wheels, and recovery controls remain reachable.
8. **Evidence capture:** save logs and short screen recordings for both successful and failed transitions. Do not mark production readiness until actual Consume outcomes, label stability/cadence, touch feel, and Classic rollback have been observed on hardware.
