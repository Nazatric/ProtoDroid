# ProtoDroid 0.16 final evidence report

Status: **program evidence consolidated; not release-ready and not a final performance claim**. The 14-phase sequence is closed within available source/host/native-CI scopes, with Phase 1 retained as an explicit 8-minute-2-second available-environment checkpoint rather than a completed one-hour phase. Released 0.15 remains `18ba66bd0945e405aaded0de5ff8848a8cd07932` (`protodroid-v0.15`). The Phase 13 production/CI source head is `2dabecdf3b15b14da56803863bb13b67c80070d6`; later commits on `arena/5eb1c06c-protodroid` are documentation/evidence only. No 0.16 merge, tag, release, version bump, or physical-device performance claim is authorized.

## Executive result

The program retained six net native production changes: four quality-preserving host/native micro-optimizations and two host-reproducible correctness fixes. It also added extensive regression, measurement, CI, and research coverage. No physical Android device was available, so the required 0.15-versus-0.16 median FPS, frame-time percentiles, long stalls, cold/warm shader loading, GPU time, sustained/thermal behavior, audible quality, and affected-device visual/media outcomes remain **unmeasured**. Host/native-CI results cannot fill those gaps.

The final readiness disposition is therefore:

- **Source/host/native-CI evidence:** complete for the recorded scope.
- **Exact source-built CI APK:** verified on the Phase 13 source head with a disposable CI signing identity; not release-eligible.
- **Physical-device performance and correctness gate:** open.
- **Supplied issues #22, #29, #32, #33, and #35:** open, not fixed by this program.
- **Release disposition:** do not merge, tag, release, or advertise 0.16 performance without explicit authorization and comparable real-device evidence.

## Adopted production changes and honest measured scope

| Change | Exact net source/result | Evidence and disposition |
| --- | --- | --- |
| DXT3/5 direct BGRA decode and palette-alpha handling | `src/native/d3d9_dxt.cpp`, SHA-256 `40c092c82ad0da0d…` | Adopted after exact output differentials and repeated host A/B. Same-process 512×512 medians include DXT3 BGRA 474.059580→338.085153 µs (one slot) and 496.642108→357.127720 µs (16 slots); DXT5 BGRA 567.194520→419.574920 µs and 577.801132→438.012223 µs. These are synthetic host decoder batches, not frame times or FPS. Packed16 behavior is preserved; no hidden format/quality reduction. |
| Small multi-object wait inline storage | `src/native/kernel32.cpp`, SHA-256 `5a358ee29cc36351…` | Adopted for up to eight handles, with vector storage retained for larger counts. Historical actual-handler evidence covers 403.2 million calls; allocation and sanitizer stress passed. No phone scheduling/FPS benefit is claimed. |
| One-time anisotropy initialization | `src/native/d3d9.cpp`, SHA-256 `ea41d9e18e65af60…` | Adopted to resolve the existing clamped setting before guest sampler calls rather than paying a local-static guard per call. Defaults and AA/preset independence remain unchanged. Host/native ARM64 micro-evidence is not physical GPU/FPS evidence. |
| Byte-validated unchanged state-cache backup skip | `src/native/state_cache.cpp` / `main.cpp`, state-cache SHA-256 `96cde02bcd7e7334…` | Adopted only after stable identities **and complete byte equality** prove the backup unchanged. Changed/uncertain caches still copy atomically. Final native ARM64 real-cache unchanged ratio 0.07396384637 with 443.623 µs median saving; changed case 1.002738 and inconclusive. Linux `/tmp` is not Android shared-storage FUSE and this is not cold/warm shader-loading evidence. |
| FAudio live-master preservation | `src/native/xaudio2.cpp`, SHA-256 `5b1bc983ef1fa078…` | Adopted in `c8709cb5a553b470f8724dce15faae0779a85528`. Uses SafeEXT, preserves the real master on dependent-send refusal, serializes replacement, and retries only without logical users. Unmodified real-backend controls fail at check 23; patched real backends pass 43 checks in each of four modes, fake backends pass 47 checks in each of four modes (47×4), and 32-cycle lifecycle fixtures pass 502/470 checks. Host dummy/fake audio only; no audible/FPS/issue diagnosis. |
| Bink failed-output retry | `src/native/bink.cpp`, SHA-256 `6ca52f74c6f35d5c…` | Adopted in `2dabecdf3b15b14da56803863bb13b67c80070d6`. Failed SDL output initialization can be retried; healthy streams remain process-lifetime retained. Representative baseline records one open and fails recovery; candidate records two opens/zero closes and passes. Both audio rates, 94 concurrent checks, and 40/49 integrated controls pass. No Android output-coexistence, audible, cutscene, FPS, or affected-device claim. |

Full source hashes and net-diff inventory are preserved in `/home/user/protodroid-016-evidence/phase-14/adopted-production-inventory.json`. The original `hot_audio.cpp`, Bink media/blit paths, Present statistics, and Android-patched DXVK source have no net production delta from 0.15. The rejected HostTable acquire relaxation, Bink plane/audio-ring/mixer rewrites, Present-policy changes, and speculative device patches are not present as adopted runtime changes. Earlier DXT and other pre-recreation raw files are historical committed measurements, not files claimed to remain reopenable in the current external store; the final exact-head ARM64/cache Check data is separately reopened and recomputed.

## Rejected and inconclusive work

The phase ledgers preserve negative results rather than converting them into successes. Major non-adoptions include:

- dispatch/function-layout changes without robust material benefit;
- RCF exact-hit deferral and cold-index rewrites;
- DXT packed16 and plane-copy shortcuts without a safe material gain;
- audio-ring contiguous reads below the preregistered absolute CPU materiality threshold, conditional-index ring changes that were slower, and speculative mixer rewrites;
- metadata-only cache-backup skipping, direct SHA-1 padding below materiality, and unsafe/text-only cache or pipeline deduplication;
- Present diagnostic gating worth only 0.000102643 ms/Present on native ARM64, and unsupported FIFO/queue/frame-latency/presentation-policy changes;
- allocator, barrier, upload, queue, or second-present-pipeline rewrites without device attribution;
- unconditional Bink stream close/reopen, cutscene skipping, audio-rate changes, feature spoofing, universal driver workarounds, hidden quality reductions, generated-frame inflation, and speculative issue-specific patches.

ADPF/Present-thread attribution, some historical semantic duplicate-like pipeline groups, changed-cache timing, and several device-specific failure mechanisms remain inconclusive. They must not be reopened without new workload evidence.

## Whole-program correctness and regression evidence

The final fresh Phase 14 host regression at documentation head `12e3ebc11e7940b1af076ee6ba39974a2b5b883c` passed **62/62 commands across 11 groups in 117.145156 seconds**, with zero resumed records. Independent verification passed **316 checks** over the manifest, all 62 return codes/timeouts, and every stdout/stderr byte count and SHA-256. A separate final-freeze rerun also passed **62/62 commands across 11 groups in 112.078860 seconds**, with zero resumed records; its independent manifest/raw-stream/runner audit passed **378 checks**. Focused final-audit reruns additionally pass Bink at both rates, XAudio2 fake 47 checks × 4 and real 43 checks × 4, plus 32-cycle real normal-lifecycle 502/470 checks, with empty sanitizer diagnostics. Coverage includes launcher/policy, release/version controls, native core/DXT, exact fake/real XAudio2, mods/plugins/scripts, diagnostics/settings, patched-DXVK policy, real-cache inventory, freshly compiled Bink at 32,000 and 44,100 Hz, real-DLL differential, and reproducibility gates.

The four final real XAudio2 bridge modes and both 32-cycle normal-lifecycle modes were freshly rebuilt from preserved harnesses that directly include current `xaudio2.cpp`, using pinned FAudio/SDL static libraries and ASan/UBSan; their build/run logs and hashes are external. These real lifecycle fixtures are sequential: they do not directly prove concurrent guest master create/destroy or indirect callback-reentry behavior, and no final TSan result exists. Their original policy sets `detect_leaks=0` because production intentionally retains guest vtables/import blocks for process lifetime. A separate final `detect_leaks=1` run reports 1,583 bytes in 76 fixed one-time `BuildVtable` name allocations; the same allocation pattern exists in released 0.15. The stricter report is preserved and is not a new 0.16 lifecycle assertion or memory-safety failure. Ten repeated original-policy lifecycle runs pass independently (five 502-check reuse and five 470-check non-reuse runs), without removing that limitation. An earlier focused Bink rerun used a preserved committed-source binary, with DWARF and source/test/stub/binary hashes recorded. A later independent final Bink rebuild compiled all four current production/test translation units with `-Werror`/UBSan and passed at both audio rates with empty stderr; separate build/run hashes preserve that source-built chain. Fresh UBSan graphics/DXT, runtime-bridge/guest-context, and state-cache retry suites also pass. Ten additional fresh-process Bink runs (five at each rate) and ten original-policy XAudio2 lifecycle runs (five 502-check reuse and five 470-check non-reuse) pass with independently verified output hashes and empty stderr. One standalone state-cache run initially failed its 5 ms ctime-isolation fixture assertion; both that failure and the unchanged-test retry pass are preserved, with no source change.

Exact-head native ARM64 Check summaries were independently recomputed in **371 checks**, including all 360 state-cache raw timing rows. The rejected HostTable ratio is 1.0013933305455902; adopted anisotropy is 0.3756525292674999; unchanged-cache results remain materially faster in their synthetic workloads, while changed-content controls are mixed and are not claimed as gains. These remain native Linux microfixtures, not physical Android FPS, GPU-time, shader-loading, or sustained-play evidence. A structured exact-CI checklist also passes 14 preservation/package requirements covering the native launcher, AA/preset independence, controls/layout, diagnostic tour, shared saves, driver recovery, Vulkan feature policy, performance-evidence guards, release policy, source ARM64 compilation, APK verification, and disposable signing; protected signing and the prebuilt-native fallback were skipped.

Phase 13's preceding broad run also passed 62/62 commands; its independent finalizer passed 354 checks and its 25 explicitly resumed records were verified rather than misrepresented as fresh. Both Phase 14 regressions are fully fresh; neither reuses Phase 13 records. Preservation-focused compatibility, touch-control, and SDL-control tests pass separately. All 13 earlier phase reports contain their required seven-part completion sections; Phase 14 adds the fourteenth.

No actual merge conflict markers or whitespace errors were found. A separate clean archive of `12e3ebc` plus only the pending research patches was reconstructed and checked: all 615 non-research files match the exact production/CI source, and all 42 current tracked/pending research files match the live worktree, including all eight pending documents and 34 other tracked research files. Fresh source-built state-cache, runtime-bridge/guest-context, XAudio2 47×4, and Bink two-rate suites pass from that clean tree with empty sanitizer stderr. The tracked original DLL and existing generated recompiler sources remain part of the repository's established layout; no evidence directory, APK, Bink sample, ZIP, or new large dataset was added to Git.

## Exact CI, APK, version, and release boundary

Exact CI run **41911558127** on **`2dabecdf3b15b14da56803863bb13b67c80070d6`** completed successfully. Later documentation-only commits do not trigger this workflow because `paths-ignore` excludes `**.md`; the final audit verified that every non-research path is identical to this tested source head. Required dependency, Bink/host, driver-package, native ARM64 graphics, Turnip, and `build-apk` jobs passed; optional ARM64 evidence jobs were neutral; `release` was skipped. The APK job compiled Android Java and source ARM64 `libmain.so`, built source ARM64 plugin/Lua examples, packaged and signed the APK, and verified the APK plus embedded Android DXVK. The prebuilt-symbol fallback was not used for the source-built result.

Artifact **13364468289** (`prototype-apk`) is server-described as **57,950,912 bytes**, with archive digest **`sha256:0a980739e8fb05c5dabd8ff48cffb4abff3ab33e66b8cb7cf1d5d060edc2f1e1`**. This is the GitHub ZIP artifact digest, not an inner-APK hash. Earlier signed-payload retrieval returned EOF, so no inner-APK/library hash or installation claim is invented. The job used the **disposable CI signing identity**; protected release signing was skipped, and the artifact is explicitly not release-eligible or update-stable.

`android/AndroidManifest.xml` remains **`com.protodroid`, versionName `0.15`, versionCode `15`**, unchanged from released 0.15. Release-policy, release-workflow, version-increment, performance-evidence, and runtime-performance host guards pass. Local signing tools (`apksigner`, `aapt2`, platform JAR) are unavailable; that local tooling failure is not hidden and is separately covered by exact CI.

At the final audit reading, `main` remains `00976d939e0cba01af908a8aecd4a02db6d41066`, no PR exists for this branch, the latest release remains `protodroid-v0.15` targeting the exact released baseline, and no 0.16 tag/release exists. The workflow permits publication only through an explicit approved dispatch on `main` after exact-commit build/test success; this session did not invoke it.

## Supplied correctness reports

**#22, #29, #32, #33, and #35 remain OPEN and unfixed.** Current issue metadata was refreshed during Phase 14. The established boundaries remain:

- **#35 / Infinix Hot 20S black screen/no audio:** a same-revision/settings previous run reached frame 65,536 and opened mission Bink videos, while the failing run stopped before mission Bink and later showed watchdog/`LockRect`/device-loss correlations. The host Bink transient-open retry is not proof of the intermittent phone trigger or a black-screen fix.
- **#33 / Motorola Play-to-home:** older logs are tied to `ea2ba6c9`; the latest recurrence lacks current logs. No current crash path is established.
- **#32 / Adreno feature failure:** requested feature state and `VK_ERROR_FEATURE_NOT_PRESENT` do not identify the exact missing feature. No feature spoofing or blanket disable is justified.
- **#22 / Mali-G57 visual glitch:** old logs contain successful resets/presents but no visual root cause. Existing compatibility diagnostics are not an issue fix.
- **#29 / Itel launcher/setup:** generic text/image evidence lacks actionable native/D3D9 logs.

Closing any of these requires current exact-version reproduction or a device-relevant source-provable defect with focused validation. Host lifecycle/recovery fixes must not be relabeled as phone issue resolutions.

## Physical-device results and remaining measurements

The physical-device table remains **zero new runs**. ADB, affected devices, Android/Bionic execution, simpleperf, Perfetto, GPU profiler, and local Android SDK/NDK/signing toolchains are unavailable. Historical logs retain their original revisions/settings and are not current A/B measurements.

A future authorized release candidate still needs, on the same exact APK/source and controlled settings/scenes/caches:

1. Released 0.15 versus candidate median FPS and frame-time p50/p95/p99 plus long-stall counts, with raw per-frame data and repeated runs.
2. Cold and warm shader/pipeline loading and first-encounter stalls, including cache state and loading-scene definitions.
3. CPU/GPU attribution where supported, ordinary-play and sustained/thermal runs long enough to expose pacing and memory behavior, with no diagnostic-only workload substituted for gameplay.
4. Visual/output equivalence for DXT, dynamic shadows, AA/texture quality, mods, Bink/cutscenes, A/V synchronization, both audio rates, controls, saves, and safe fallbacks.
5. Current affected-version reproduction and validation for #22/#29/#32/#33/#35, including exact device/GPU/driver/settings/log provenance.
6. An explicitly authorized version bump, protected/update-compatible signing decision, exact-main CI, artifact/inner-APK verification, and only then any merge/tag/release decision.

Until those gates are satisfied, the defensible conclusion is **native/host improvements and correctness fixes with strong regression evidence, but no validated physical-device FPS/pacing or issue-resolution claim**.
