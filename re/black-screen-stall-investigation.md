# Black screen after the Activision logo: what the device log actually shows

Status: open. This note records the evidence and the two experiments that decide the next fix, so the ground is not
re-covered.

## Evidence

Log: `proto-native.log` at commit `4e0c24d` (build of the v1.1 tree, source `cecaf6cf`, "Nov 12 2026 12:32:27").

* The Activision Bink movie opens, uploads its first frame (`frame=1`, `yuv420p`, `wait=paced`) at t=572806, and then
  the engine presents nothing.
* 19 s later the watchdog fires: `no frame for 10 s (frame 6726)`.
* Every job thread (tid 4160-4170) is parked in `protodroid::FastSemaphore::Wait` with the stack
  `DispatchImport -> RecompDispatch -> f_7c63d2f0 (guest thread entry) -> ...`. That is the engine's worker pool
  waiting for work: idle, not spinning, and therefore not the thing that is stuck.
* The main thread (tid 4149) is *not* blocked. It is three levels deep in the engine's Scaleform ActionScript VM:
  `GASAsFunctionObject::vfunction8` (0x10a24c00) -> `GASObject::vfunction12` (0x10a368d0) -> `FUN_109d77b0` ->
  `FUN_109dde30` (a 19 KB engine function) -> back into `GASAsFunctionObject::vfunction8`, and the same trio repeats.
  `FUN_109d0bb0` on that stack is the "invoke method, else raise" path whose failure string is
  `"Error: Invoked method is not a function"` (0x10e91adc), and `FUN_10a26bf0` is the tagged-value copy helper.
* No `BinkWait*`, file, or D3D9 import appears anywhere on the stuck stack, so it is not a movie wait, a file wait, or
  a driver wait. It is the engine's UI script running without ever presenting.

Interpretation: the front-end Flash/ActionScript layer runs after the logo and does not return to the frame loop. Two
shapes fit: an ActionScript loop polling a condition that never becomes true, or a genuine mis-execution in the
recompiled code that keeps the loop from terminating. The v1.1 tree is the only change in play: the same engine binary
with the pre-v1.1 host ran for minutes and reported 51-58 FPS in device logs from earlier builds.

## Second device run (build 589991d9, `sync_futex=0`): the futex path is exonerated

The same stall reproduces with the futex primitives bypassed: every job thread is in `pthread_cond_wait` (the generic
condition-variable path), the main thread still stops presenting, and the watchdog now reports **11,366,400 computed
guest calls per second** on the main thread while it does so. So:

* not the futex primitives - the stall survives without them;
* not a wait at all - the main thread is running flat out, three script-VM frames deep
  (`GASAsFunctionObject::vfunction8` / `GASObject::vfunction12` / `FUN_109dde30` / the invoke path `FUN_109d0bb0`);
* not a movie, file or driver wait - no import call appears anywhere on that stack.

A backtrace cannot localise it: frames in recompiled code resolve to the nearest preceding symbol, and this log shows
offsets past the end of the named function (`f_7ca28120+0x1248` in a 0x30e-byte function; the innermost address lands
in `int3` padding). That is what the dispatch ring in `runtime.cpp` is for.

## Found: the recompiler was dropping real flag definitions

v1.1 writes only the flags a later instruction reads, which needs the "writes" set to be exact. Capstone's is not: the
x87 condition codes C0-C3 are reported as the integer flags, so `fstp st(0)` claims to write CF and `fxch` claims to
write OF, and `fstp` carries no capstone group at all, so a group filter misses it. Liveness then kills the real
definition and the next reader sees a stale flag. The engine's script comparison is exactly
`fucompi st(1); fstp st(0); lahf; test ah,0x44; jp` - the pop sat between the compare and the `lahf`.

Generating the whole engine from both versions and comparing every emitted mask: **1826 call sites** were losing a
flag definition; the script VM's comparison at `0x10a282fa` now writes all six status flags (`0x3f`) instead of five
(`0x3e`). `tools/test_recomp_flag_masks.py` pins the metadata down and runs in CI before the engine is generated.

## Experiments this build supports

1. Default run. The watchdog now repeats its snapshot while a stall lasts and prints the retired guest-call rate per
   second next to each snapshot. Same symbols plus a huge, steady call rate means a hot loop in one place; different
   symbols means the engine is working and just not presenting; a near-zero rate means a real wait.
   `DispatchImport` also names a thread that calls one import two million times without any other import in between
   (a guest poll loop).
2. `protodroid.ini`: `sync_futex=0` routes every event and semaphore wait/signal through the generic
   condition-variable path instead of the futex primitives. Already answered: the stall survives the switch, so the
   primitives are not the cause. The primitives are still covered by `src/native/tests/sync_futex_test.cpp` in CI.
3. The dispatcher records the last 64 computed calls per guest thread plus the register file, and the watchdog dumps
   them with every stall sample (`dispatch_trace=0` turns it off). The repeating cycle of targets names the loop, and
   the registers show the counters it compares - map an address with `re/functions.csv` (guest VA = address - the
   engine base).

## Rejected or excluded

* Bink pacing: `WaitForFrame` sleeps at most 100 ms and returns; it cannot stall for 10 s.
* Worker-pool semaphore: the workers are idle on purpose; woken work is not what the main thread is waiting for.
* The `f_7c9dde30+0x10c40` offset in the log is a return-address attribution, not an offset inside that function: the
  function is 0x4b75 bytes and the resolved frame belongs to `GFxFontManagerStates` (0x109f4bb0).
