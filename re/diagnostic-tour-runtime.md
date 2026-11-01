# Diagnostic-tour entry, activity, and shutdown analysis

Status: source candidate only. Exact-commit ARM64 CI and physical tour execution are still required. This note does not claim visual, audio, ability-result, or every-feature verification.

## Read-only inventory

- `TestingFiles` commit `9fa845ee1fee1485ba04e04dc365b9d782b82d29` supplied `journal.log`, `phase.txt`, `result.txt`, `crash.log`, native/DXVK logs, and pipeline diagnostics. Copies were analyzed outside Git; the branch was not checked out, modified, or pushed.
- The run identifies source `ca9e365bc30969963f473f868738e88d5a55955d`, Android 16, Mali-G720 MC7, and direct Vulkan 1.3.278. It is not evidence for another GPU or driver.
- The original DLL remained read-only: SHA-256 `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071` (Steam build 19788432).
- Inspected `diagnostic_tour.*`, `diagnostic_tour_plan.h`, `runtime.cpp`, `main.cpp`, `src/recomp/runtime.h`, generated recompilation, `re/functions.csv`, and `TouchControls.java`.

## Seen

### The old tour never started its gameplay actions

- `journal.log` records all 17 phases as requested, successful isolated-save setup, and `EngineInitialize=returned-success`.
- It contains no `tour status=running`, phase `status=running`, or action records. After 120 seconds it records `phase=gameplay_wait status=failed` and `result=failed` at 4,856 Presents.
- Therefore this run did not execute the advertised movement/combat/power sequence. Manually playable rendering alongside `gameplay_wait` was possible and does not contradict the journal.

### The subsequent stop matches the reported frozen game

- Immediately after the entry timeout, native code left its main loop for `EngineTerminate`.
- The native log has no `EngineTerminate done`. At `mono=112622.030`, the watchdog reports no main-loop frame for ten seconds at frame 4,857. The main guest thread is waiting in a host condition-variable path; later diagnostics show zero engine-loop iterations while Present statistics continue.
- This matches a frozen last game scene with responsive Android controls. `crash.log` records no native crash, and the DXVK tail has no device-lost report. The logs do not establish an independent impact-related rendering fault.

### Why the gameplay probe was missed

- `re/functions.csv` identifies the simulation and real-time task bodies at preferred VAs `0x104e45c0` and `0x104e4ad0` (`0x7c4e45c0` and `0x7c4e4ad0` at ProtoDroid's loaded base).
- The old probe checked only those final addresses in `RecompDispatch`.
- Read-only DLL probes find vtable entries `0x100246b3` and `0x1002bc01` at raw `.rdata` offsets `0xe1a7d0` and `0xe1a7dc`. Their near jumps resolve to `0x104e45c0` and `0x104e4ad0` respectively.
- Both thunks exist in generated recompilation. Under `RECOMP_FLAT_MEMORY`, `RecompDirect()` always returns true, so a dispatched vtable thunk runs its task directly without another `RecompDispatch` or `FollowThunks` call. The old final-address-only probe is bypassed.

### Additional ordinary controls with concrete mappings

`TouchControls.java` documents and implements Tab targeting, E grab, R power wheel, F disguise wheel, F1 take-disguise, and F2 return-to-Alex. Wheel selection uses held input plus relative mouse motion. These provide evidence for separate input phases; they do not prove that a target, disguise, or power is available in the direct test level.

## Candidate policy and implementation

1. A host-testable classifier recognizes both vtable thunk offsets and their resolved task offsets. It does not disable `RecompDirect`, edit generated engine code, or alter normal Play dispatch.
2. Every observed gameplay dispatch increments an atomic counter. Every non-skipped phase records its delta and fails rather than reporting `tested` if Presents elapsed without `InGameState` task activity. Result/crash markers include the total count.
3. The plan has 22 requested phases: 20 executable phases totaling 2,570 presented frames and 124 actions, plus two honest skips. It adds short targeting, grab-follow-up, power-wheel, disguise-wheel, and disguise-form sequences. Outcomes remain explicitly unverified; inputs are not evidence that an ability or target was available.
4. At the main-loop boundary, an isolated tour now releases injected input and ends its process without calling `EngineTerminate`. Failure uses the existing detailed startup-failure path; success flushes diagnostics/mirrors and exits. Ordinary Play retains the original `EngineTerminate` path.
5. If the engine never returns to that boundary, the existing watchdog first captures its backtraces, rechecks that the frame counter did not recover, records a durable failed tour, and exits only the isolated process. It makes no guest/input calls on the watchdog thread. Ordinary Play keeps the existing report-only watchdog behavior.
6. Bink playback and physical Android-touch behavior remain honest skips in this direct-level SDL tour. No visual or auditory correctness claim is added.

## Validation completed locally

- Host C++17 UBSan plan tests pass all 22 phases, action ordering/release rules, present-frame scheduling, both thunk mappings, both direct task mappings, and unrelated-target rejection.
- `tools/test_diagnostic_tour_runtime.py` passes the DLL hash/vtable/jump probes and source policy checks for thunk classification, isolated exit, and unchanged ordinary `EngineTerminate` wiring.
- The new binary/source policy test is wired into `build-apk`; the C++ plan/probe test already runs in both host and native ARM64 test jobs.

## Inferred

- Missing the recompiled vtable thunk route fully explains the observed absence of gameplay signals without extending the timeout.
- The post-timeout `EngineTerminate` path explains the reported frozen scene and live Android UI. Avoiding that path only for the process-ending isolated tour is narrower than changing engine shutdown for everyone.
- The simulation/real-time tasks should provide recurring activity during real gameplay. Per-phase counts will test that inference on device; a zero delta is deliberately a failure, not a presumed pass.

## Unknown / physical validation required

- Whether the corrected probe observes recurrent dispatch on the user's exact retail data and how many dispatches each phase records.
- Whether injected movement, targeting, grab, wheel, and form inputs produce their intended game actions in `manhattanTest`; unavailable targets and abilities must not be called successful.
- Whether any separate impact-related game-logic or rendering stall remains after the entry/exit defects are removed.
- Visual shadows/effects, audio correctness, Bink playback, physical touch, save-game behavior beyond the isolated path probe, and complete feature/crash coverage.
