# Phase 13 — integrated correctness/performance validation

Status: **complete within the available source/host/native-CI scope**. Phase 13 adopted one narrow Bink audio-output recovery correction in production commit `2dabecdf3b15b14da56803863bb13b67c80070d6`; it did not adopt an FPS optimization or establish a fix for any supplied phone report. Phase 14 final evidence consolidation is next. No merge, tag, release, or 0.16 performance claim is authorized.

## H1 — integrated Bink/XAudio2 ownership and failure controls

The exact production `xaudio2.cpp`, `bink.cpp`, `bink_media.cpp`, and `bink_blit.cpp` were linked with pinned FAudio 25.10, SDL 2.30.9, FFmpeg n7.1.1, and zlib 1.3.1. The combined fixture uses guest vtables/thunks and a real 15,563,652-byte `valve.bik` sample (SHA-256 `e4cc59c1242615fc33159537e21d019599481676f1de62818a5cc4aa9d2f2dd5`). Fresh-process ASan/UBSan controls pass:

- **Bink-first:** 40 checks. Bink opens its stream; FAudio mastering creation returns `0x88960004` with a null output on the host dummy backend. Closing the movie retains Bink's process-lifetime stream, so the second attempt also fails explicitly; there is no crash or master overwrite.
- **FAudio-first:** 49 checks. Master/source creation and destruction succeed, then Bink open/decode/close and engine release complete. Sanitizer diagnostics contain no ASan/UBSan error.

This is host dummy-audio behavior, not Android physical-device evidence. SDL's high-level single-default-output restriction is not extrapolated to Android/AAudio. The integrated fixture's earlier fixed-mapping, static-initialization, invalid-source, and stale-binary failures were harness/setup issues and are not counted as product results.

## H2 — transient Bink audio-open recovery

### Source/reachability finding

`EnsureAudioOutput()` previously used `std::call_once`. If SDL initialization or `SDL_OpenAudioDevice()` failed, every later movie inherited `g_audio.device == 0` permanently, even if the output later became available. Video continued through the existing monotonic-timing fallback, but later movie audio could not recover. The original engine DLL imports and directly calls `BinkSetSoundTrack` before opening movies; a representative failed-first-movie/later-movie sequence reproduced the lost retry. This is a source-reachable robustness defect, not proof of the trigger for #35 or any other issue.

### Candidate and controls

The candidate replaces the one-shot flag with a small initialization mutex and an early return only when a valid device already exists. Failed initialization can be retried by a later existing audio-output request; a healthy stream is never closed, replaced, or reopened. No frame, audio-rate, A/V synchronization, fallback, or quality behavior is changed.

| Control | Unmodified production | Candidate |
| --- | --- | --- |
| Real pinned SDL fault injection, first open fails then later movie | Second movie opens/decodes but only **1** SDL open attempt; recovery assertion fails | **2** opens, second succeeds; no unexpected closes; pass |
| Deterministic Bink regression | Fails at `failed audio initialization was not retried exactly once` | Passes at 32,000 and 44,100 Hz; healthy third movie reuses the same stream |
| Concurrent first-use fixture, six threads with delayed stub and one injected failure | Fails retry/serialization assertion | **94 checks**, exactly 2 opens, 0 closes, all movies open/decode/close; later movie reuses stream |
| Exact XAudio2 four-mode regression | Baseline established in Phase 12 | **47 checks × 4**, zero master overwrites, empty sanitizer diagnostics |

The new permanent Bink test injects one failed open before the first successful initialization, checks monotonic video fallback and exact recovery, and checks that a subsequent healthy movie does not reopen the stream. The test-only SDL stub adds counters/failure injection and an optional delay; it does not change production SDL behavior.

## Phase 13 completion report

### 1. Actual time and work

Phase 13 started at `2026-11-04T20:32:10.615107Z` and completed at `2026-11-04T21:32:21.772245Z` (**3611.157138 seconds / 1h00m11.157138s**) of substantive integrated source auditing, original-engine import/call-site inspection, fixture repair, real SDL/FAudio/FFmpeg controls, candidate implementation, sanitizer/concurrency testing, broad regression, exact CI/package verification, and documentation. No idle/timer padding or physical-device measurement is included.

### 2. Files and commits

Production/test commit **`2dabecdf3b15b14da56803863bb13b67c80070d6` (`fix: retry Bink audio after transient open failure`)** changed exactly:

- `src/native/bink.cpp`: retry only failed Bink audio initialization; retain a successful stream and serialize initialization.
- `src/native/tests/bink_test.cpp`: permanent first-failure/recovery/healthy-reuse regression.
- `src/native/tests/sdl_stub.h`: deterministic test counters and failure injection.

This completion record updates research documentation only. External fixtures, raw logs, disassembly, binaries, and CI JSON remain under `/home/user/protodroid-016-evidence/phase-13/`; large artifacts are not added to Git. The branch was pushed only as `arena/5eb1c06c-protodroid`; no other branch, merge, tag, or release was used.

### 3. Candidate dispositions

- **Adopted:** the narrow Bink failed-output retry correction, supported by unmodified-source negative controls and exact-production candidate controls.
- **Rejected/not selected:** changing Bink's intentional process-lifetime stream ownership to satisfy host dummy second-output expectations; adding an unconditional close/reopen; altering audio rates, mixer/quantum/callback policy, video pacing, or frame skipping; and any speculative device-specific patch.
- **Preserved:** DXT work, Bink/cutscenes and monotonic fallback, dynamic shadows, AA/texture quality, mods/metadata, texture modding, A/V synchronization, both audio-rate behaviors, safe defaults/drivers, native launcher/controls, ARM64 ABI, and prior runtime dispositions.
- **Issue boundary:** #22, #29, #32, #33, and #35 remain open and unfixed by this phase. The host recovery defect is not promoted to a shared phone root cause.

### 4. Raw before/after measurements

This is a correctness/recovery change, so there is **no honest FPS before/after number**. The raw functional before/after results are:

- Real SDL transient-open fixture: baseline `audio_open_calls=1`, recovery check fails; candidate `audio_open_calls=2`, `audio_close_calls=0`, pass.
- Permanent Bink suite: unmodified negative control exits 1 at the new retry assertion; candidate passes at both 32,000 and 44,100 Hz. Final sanitizer runs have empty stderr.
- Concurrent fixture: baseline fails the retry/serialization assertion; candidate reports `PASS concurrent-bink-audio checks=94 open_calls=2 close_calls=0`.
- Integrated real-backend controls: Bink-first **40 checks**, FAudio-first **49 checks**, both pass with no sanitizer error. Bink-first retains the explicit host-only `0x88960004` second-output failure; it is not rewritten into a false success.
- Exact XAudio2 regression: **47 checks in each of four modes**, `master_overwrites=0`; reuse modes report master create/destroy **2/1**, non-reuse **2/2**.

### 5. Correctness and regression evidence

- Fresh Phase 13 broad regression: **62/62 commands, 11 groups**, final resumed-run elapsed **88.844822 seconds**. It includes a fresh current-source Bink build and both audio rates, exact XAudio2 fake/real controls, native core, mods/scripts, diagnostics/settings, patched DXVK policy, real-cache inventory, real-DLL differential, and reproducibility gates.
- The independent manifest check validates all 62 output byte counts and SHA-256 hashes, with **25 records explicitly marked resumed** from the preserved partial run. The first partial run stopped after 26 records due to the foreground shell timeout; its failed `issue-templates` command was only missing Python `yaml`, rerun successfully with the pinned dependency path. No partial run is counted as a complete pass.
- The first manifest verifier used an incorrect expected resumed count (26); the corrected invariant is 25 resumed records, and all hashes/bytes pass. Earlier fixture mapping, stale-binary, unused-helper, f-string, and invalid-source setup failures are preserved outside Git and excluded from product claims.
- Exact CI run **41911558127** on **`2dabecdf3b15b14da56803863bb13b67c80070d6`** is **completed/success**. Required `bink-tests`, dependency, driver, native ARM64 graphics, Turnip, and `build-apk` jobs succeed; optional ARM64 evidence jobs are neutral; release is skipped. CI Bink production harness and XAudio2 regression steps pass.
- CI source-built ARM64 `libmain.so`, Android Java compilation, source ARM64 plugin/Lua examples, APK packaging/signing, and APK/embedded-DXVK verification pass. The prebuilt-symbol fallback is not used for the source-built result.

### 6. Limitations

No ADB, affected physical device, Android/Bionic execution, real audio output, GPU profiler, thermal trace, or local Android NDK is available. Host fake/dummy audio and native ARM64 CI do not establish audible quality, callback latency on a phone, Android stream coexistence, FPS, frame-time percentiles, GPU time, thermal throttling, or sustained playback.

Artifact **13364468289** (`prototype-apk`) is server-described as **57,950,912 bytes**, with archive digest **`sha256:0a980739e8fb05c5dabd8ff48cffb4abff3ab33e66b8cb7cf1d5d060edc2f1e1`**. This is exact-run artifact metadata, not an inner-APK hash or installation/visual/audio validation. The artifact API query initially used an unsupported `gh run view` field and was corrected through the Actions API; no payload claim is inferred from that setup error.

The transient recovery defect does not diagnose Dana's Safe House/cutscene black screens, the Motorola Play-to-home crash, the Itel setup report, #32's missing feature, or #22's visual glitch. No unconditional cutscene skipping, feature spoofing, hidden quality reduction, or universal driver workaround is justified.

### 7. Next priority

Begin **Phase 14** with final whole-program evidence consolidation and an explicit release-readiness boundary. Recompute the final source/CI/artifact provenance, preserve the adopted Bink and XAudio2 corrections under regression, and state exactly which physical FPS/pacing, shader-loading, sustained, rendering, and affected-issue outcomes remain unmeasured. Do not merge, tag, release, or advertise 0.16 without explicit authorization and comparable real-device evidence.
