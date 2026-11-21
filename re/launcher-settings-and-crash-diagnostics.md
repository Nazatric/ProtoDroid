# Launcher settings that did nothing, and what the next crash report will say

Status: settings root cause identified and fixed; crash root cause narrowed, not yet proven. Nothing here is a
measurement of frame rate or stability.

## 1. Almost every launcher engine option was inert on this device (fixed)

The device log (`proto-native.log`, build `Nov 14 2026 07:09:25`, source `7be996af`) contains exactly one loaded
setting:

```
config: loaded /storage/emulated/0/Prototype/protodroid.ini
config: sync_futex = 0
```

That file is on shared storage. The launcher's settings page writes a *different* file, the app's own external files
folder (`Android/data/com.protodroid/files/protodroid.ini`, see `GameSettings.writeEngineSettings`). `config.cpp`
stopped at the first candidate that existed, so the launcher's file was never opened: resolution, textures, shadows,
quality, FOV, shadow map, tick limit, developer menu, VRAM report, texture dumping — every `engine=true` row — was
written and then ignored. Only launcher-local options (`ui_*`, stored in SharedPreferences) could take effect, which is
why the failure looked like "some settings don't work".

`config.cpp` now loads **every** candidate file, in priority order, and the first file that defines a key wins:

1. `<app external files>/protodroid.ini`, and `./protodroid.ini` (the same folder on Android) — the launcher's file
2. `/storage/emulated/0/Android/data/com.protodroid/files/protodroid.ini`
3. `/storage/emulated/0/Prototype/protodroid.ini`, `.../Prototype/game/protodroid.ini` — hand-written keys
4. `<game_dir>/protodroid.ini` and its parent

Hand-written keys that the launcher does not own (`sync_futex`, `detailed_diagnostics`, ...) still apply, a key
shadowed by a higher-priority file is logged as such, and each file logs `N key(s) used, M repeated line(s)`. The next
device log therefore states which file the engine obeyed and which keys it used.

Tests: `src/native/tests/config_test.cpp` (`run_config_test.sh`) pins the precedence rule (launcher file wins, shared
file still contributes its own keys, case/comment/section handling). `tools/test_launcher_settings.py` fails the build
if a launcher row marked `engine=true` names a key that appears nowhere in the native port — the class of bug that
makes a row do nothing at all. Both run in the `bink-tests` job before the engine is generated.

The in-game overlay also re-reads the launcher's UI settings when it attaches, not only in its constructor, so a
change made while a session exists is picked up.

## 2. What the crash address actually is

```
signal 11 (Segmentation fault) code 2 addr 0x103eecdc7b on tid 11190
pc f_7c6a7750+0x1f0   lr f_7c663780+0x398
```

`SIGSEGV` code 2 is `SEGV_ACCERR`: the page is **mapped and not writable**, not unmapped. The guest window lives at
`RECOMP_GUEST_BASE = 0x1000000000` (`src/native/Makefile`), so the faulting guest address is
`0x103eecdc7b - 0x1000000000 = 0x3eecdc7b` (the previous run: `0x3f853b9b`) — a plain 32-bit guest address inside the
arena, in a range the allocator had handed back (`UnmapGuest` maps freed ranges `PROT_NONE`). The high half of the
address is the guest window base, not a truncated 64-bit value: the earlier "48-bit pointer" reading was an artifact
of not subtracting the base.

`f_7c6a7750` is a dirty-bit-handle flush: it takes a 16-bit biased handle, computes a byte index and bit
(`shr cx,3`, `and ecx,7`, `shl dl,cl`), sets the bit in the dirty bitset `[esi+0x2080]`, and appends the handle to the
queue at `[esi+0x2084]` — `[queue+8]` base, `[queue+0xc]` cursor, capacity `0x1000`, store `mov word [edx], di`. The
faulting store is the append. The cursor it used was a stale arena address, so either the queue object was already
destroyed (use after free) or the queue buffer had been released while the queue still pointed into it.

## 3. Allocator behaviour that could produce a stale pointer (fixed where it was a deviation)

* `VirtualFree(MEM_DECOMMIT)` used to leave the pages mapped, so a later `VirtualAlloc(MEM_COMMIT)` over the same
  reservation returned the **old bytes**. Windows guarantees zeroed memory for a commit. A structure that is
  decommitted and re-committed came back with a stale pointer where the game expects `NULL`. `PageDecommit` now drops
  the backing (and `PageAlloc` re-backs exactly the committed range, which is zeroed), tracked in `g_decommitted` per
  reservation.
* `HeapAlloc`/`HeapRealloc`/`PageAlloc` now reject a size that cannot fit the 2 GiB arena. `size + sizeof(Header)`
  used to wrap, turning a huge request into a small size class — a caller then wrote far past a tiny block and
  corrupted whatever followed it in the heap.
* `HeapRealloc` of a block whose header is not ours now returns `0` instead of reinterpreting foreign memory.

Tests: `src/native/tests/memory_test.cpp` covers decommit → commit zeroing, interior commits, impossible sizes and the
address descriptions.

## 4. What the next crash report will contain

`OnCrash` (main.cpp) now classifies a fault that falls inside the guest window and writes the answer to the log,
followed by the last 24 allocator operations (`memory.cpp` ring, 512 entries, one relaxed atomic; each event records
kind, address, size, the **guest return address of the import that asked**, and the thread id):

* `committed allocation [..): the mapping is fine, the write itself is wrong`, plus `that heap block was freed by
  guest <pc> on tid <n> and not allocated again (use after free)` when the newest event covering the address is a
  free;
* `decommitted page inside the allocation ...`;
* `unbacked page of the free range [..): released, decommitted or never allocated`;
* `outside the arena (engine image, guest stack or host pointer)`.

With `crash: mem[-NN] ...` lines next to `crash: guest <addr>: ...`, the next log names the call site that freed (or
never allocated) the memory the flush routine wrote to, instead of only the faulting frame.

The report also carries `crash: guest calls <addr> ...`, the crashing thread's last 24 guest call targets, read from
its own dispatch ring without the global trace lock (`CurrentDispatchTrace`, persisted by the ring test in
`runtime_bridge_test.cpp`). The `pc` in a crash report is a host address inside a recompiled function, and the two
device crashes reported `f_7c6a7750+0x1f0`, which is not an instruction boundary in this build, so guest addresses are
the only reliable frame identity.

Where the flush context comes from, for whoever reads the next report: `f_7c6a7750`'s `esi` is its second argument,
which `f_7c663780` passes as a pointer into the *caller's* frame (`lea ecx,[esp+0x1c]`). The bitset at `[esi+0x2080]`
and the queue at `[esi+0x2084]` are fields of that ~0x2088-byte context, so a garbage cursor comes from a context that
was filled by an outer function - the guest call chain in the report is what identifies it.

## 5. What to check in the next device log

1. `config: loaded ...` lines now name **every** file that existed, and each is followed by `N key(s) used, M repeated
   line(s)`. The launcher's file must appear first; if the only file is `/storage/emulated/0/Prototype/protodroid.ini`,
   the settings page has not been opened since installing this build, which is expected until a toggle is changed.
2. `config: <key> = <value>` lines repeat the values the engine will use. A launcher toggle that "does nothing" is now
   visible either here (the value the engine uses) or absent (the launcher did not write it).
3. After a crash, `crash: guest <addr>: ...`, `crash: mem[-NN] ...` and `crash: guest calls ...` describe what the
   faulting address was, which guest call freed or allocated it, and the call chain that led there.

## 6. Open

The crash itself is not proven fixed. The evidence points at a guest-side lifetime error around the dirty-handle queue
during level load (the crash follows `textures: 1024 created ... 218 MiB`, after the front-end movies), and the new
report is what makes the responsible allocation/free visible. The logged runs are from build `source 7be996af`, which
is not a commit in this repository, so they describe the guest path rather than any particular branch build.
