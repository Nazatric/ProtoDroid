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

## Experiments this build supports

1. Default run. The watchdog now repeats its snapshot while a stall lasts and prints the retired guest-call rate per
   second next to each snapshot. Same symbols plus a huge, steady call rate means a hot loop in one place; different
   symbols means the engine is working and just not presenting; a near-zero rate means a real wait.
   `DispatchImport` also names a thread that calls one import two million times without any other import in between
   (a guest poll loop).
2. `protodroid.ini`: `sync_futex=0` routes every event and semaphore wait/signal through the generic
   condition-variable path instead of the futex primitives. If the stall disappears with that switch, the lost wakeup
   is in `sync_futex.h`; if it survives, the engine code is the suspect. The primitives themselves are now covered by
   `src/native/tests/sync_futex_test.cpp` in CI, so a known-bad handoff would have to be in how `kernel32.cpp` uses
   them, not in the primitives.

## Rejected or excluded

* Bink pacing: `WaitForFrame` sleeps at most 100 ms and returns; it cannot stall for 10 s.
* Worker-pool semaphore: the workers are idle on purpose; woken work is not what the main thread is waiting for.
* The `f_7c9dde30+0x10c40` offset in the log is a return-address attribution, not an offset inside that function: the
  function is 0x4b75 bytes and the resolved frame belongs to `GFxFontManagerStates` (0x109f4bb0).
