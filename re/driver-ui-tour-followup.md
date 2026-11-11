# Driver-screen and diagnostic-tour follow-up (0.14)

## Evidence inventory

Work was performed on read-only copies under `/tmp/protodroid-log-runs-2026-11-01` and `/tmp/protodroid-driver-research`; the `TestingFiles` branch was never checked out or pushed.

- **Run A:** `TestingFiles` at `aa8db7e1d30c28213af471462e33ba9da242ed39` (2026-11-01 04:40 UTC).
- **Run B:** `arena/c7fce402-protodroid` at `1604afeddaffe868812f796c30b7a3daea687836` (2026-11-01 04:50 UTC).
- Run B's `journal.prev.log` and `proto-native.prev.log` match Run A's current files, so these are consecutive runs rather than independent crash sets.
- Both runs identify source `51e51ea3b20927e40e94135a6a09633c616dcf61`, Auto on Mali-G720 MC7 through the direct system Vulkan 1.3 path.

## Seen

- Both tours entered gameplay and completed all 22 phases with recorded tested/skipped outcomes. Run A: 3,008 Presents and 4,934 gameplay dispatches. Run B: 3,132 Presents and 4,952 dispatches.
- Both `result.txt` files say `result=complete`. Both `crash.log` files contain only the armed/no-crash marker. Neither native log contains a crash signal, watchdog failure, startup failure, or Vulkan device-lost report.
- Each native log ends with `diagnostic tour finished; ending the isolated process without EngineTerminate`. The isolated process close is intentional. Whether this close is what the user perceived as a crash is **unknown**.
- Run A repeatedly recorded `present_batch ... timing=actions-late-to-engine-service-boundary`; Run B did too. The controller advanced its cursor once for every Present in a batch after one `EngineService`. Consequently, actions from multiple logical frames could be injected before the next game update. This mechanism is seen in source; which individual ability inputs the game ignored is **unknown**.
- Run A's summary includes a 3.431-second frame interval and additional multi-second intervals. Run B includes a 1.033-second interval during `shadow_motion`, with Mali/DXVK compiler threads active in the surrounding window. DXVK pipeline summaries report zero errors.
- Run A began without a tour DXVK state-cache file; Run B read 195 entries. These tours use isolated caches and tour-only pipeline diagnostics/serialization. They are not controlled benchmarks of ordinary Play.
- BC textures are reported unsupported by the GPU and the established decode path is active. `SetupFPU` and cursor warnings also appear, but neither is chronologically tied to a crash. Some file-open probes fail, while read-error counts remain zero; the aggregated log does not identify every optional path, so a defect cannot be inferred from that count alone.

## Fix policy

1. **Tour timing:** one logical action frame per engine update with new Present activity, even when multiple Presents coalesce. Actual `presented_frames` and logical `action_frames` are recorded separately. Each executable phase still requires nonzero `InGameState` activity. This prevents a batched press/release pair from being emitted before the same next game update.
2. **Expected close:** the launcher says that the automatic tour closes the app afterward, and `result.txt` records `exit_behavior=intentional-isolated-process-close`. This clarifies the observed behavior; it is not claimed as a fix for an unrecorded native crash.
3. **Stalls:** no rendering, shader, memory, threading, or driver rewrite is justified by these logs. The exact cause of every long frame remains **unknown**; the compiler correlation in one Run B window is not proof for all stalls. Preserve normal Play and collect a matched normal-Play log if a reproducible impact/freeze remains.
4. **Driver behavior:** package pins, SHA-256 checks, staged validation, explicit Custom selection, separate probing, recovery markers, native Auto/System/Turnip/Custom policy, and Vulkan paths are unchanged.

## Driver-screen research

- [Android accessibility guidance](https://developer.android.com/guide/topics/ui/accessibility/apps) recommends at least 48 dp × 48 dp touch targets and adequate text contrast. Custom Canvas controls must enforce this themselves.
- [PanVK-kbase DriversScreen](https://github.com/zenithblue-oss/panvk-kbase-android/blob/main/apps/panvk-launcher/app/src/main/java/dev/zenithblue/panvklauncher/DriversScreen.kt) uses full-card radio selection, progressive details, and confirmed deletion. Its landscape/portrait screenshots were inspected as layout references, not copied as ProtoDroid artwork.
- [GameNative DriverManagerDialog](https://github.com/utkarshdalal/GameNative/blob/master/app/src/main/java/app/gamenative/ui/screen/settings/DriverManagerDialog.kt) separates download, import, installed packages, progress, and deletion. ProtoDroid retains those operations but reduces their simultaneous presentation.
- [PPSSPP #22869](https://github.com/hrydgard/ppsspp/pull/22869) distinguishes Adreno/Mali package presentation and adds short explanations of custom-driver scope and risk. Family filtering is a hint, not proof of exact GPU/kernel compatibility.
- [libadrenotools](https://github.com/bylaws/libadrenotools) confirms that each app opts into custom loading; importing a package does not replace Android's global driver.
- Existing GameNative/Mesa wrapper, DXVK 2.5.3, Turnip, and PanVK research remains applicable to native behavior; this UI batch does not alter those implementations or pins.

## Implemented UI direction

- Current choice and next-Play scope appear before advanced controls.
- Four large, readable mode cards use plain names: Automatic, Phone driver, Bundled Turnip, and Custom package.
- Automatic is visibly recommended; Mali/Turnip and missing-Custom-selection warnings are explicit.
- Driver packages are collapsed for ordinary Auto/System use and open automatically for Custom.
- One Installed/Downloads split replaces four dense filters. Downloads default to the detected family with a Show all option; unknown families show all packages with a warning.
- Installed cards support tap-to-use, progressive details, and two-tap removal. Downloads retain pinned source/compatibility/size information.
- Interactive buttons are at least 48 dp; body text and contrast were increased. Prototype's dark panels, cut corners, red HUD lines, and condensed fonts remain.

## Validation boundary

Host policy, planner, binary-probe, and navigation checks are not an Android UI screenshot test or a physical gameplay run. Exact ARM64 Java/native compilation and APK packaging must pass for the final commit. Physical checks still need to cover Back, scrolling, font scaling, mode selection, Installed/Downloads, import/download progress, details/removal, and the tour's actual input timing. Visual ability results, auditory results, universal driver compatibility, stable 60 FPS, and the unrecorded impact/freeze remain unclaimed.
