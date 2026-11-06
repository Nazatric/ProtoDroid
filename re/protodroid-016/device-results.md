# Physical-device results

No physical Android device or `adb` connection is available in the current environment. No new device benchmark has been run for 0.16. Phase 1 is an explicit 8-minute-2-second available-environment checkpoint, Phases 2–13 are complete within their recorded scopes, and Phase 14 is complete only within the source/host/native-CI scope. Phase 12 adds exact XAudio2/FAudio lifecycle controls, sustained historical-log classification, broad regression, and issue-log classification; none supplies current physical-device FPS, frame-time, rendering, audio-device, thermal, or sustained-playback evidence.

| Device/GPU/driver | Source/APK | Settings/scene/cache | Runs | Result | Limitations |
| --- | --- | --- | --- | --- | --- |
| None attached | N/A | N/A | 0 | Unmeasured | Host and CI evidence cannot fill this row |

Historical logs may guide scene selection and instrumentation, but are entered only with their original source revision and are never relabeled as released-0.15 or candidate-0.16 results.

## Supplied issue evidence is not new device validation

The Phase 11 read-only audit leaves **#22, #29, #32, #33, and #35 open**. It inspected current issue bodies/comments, all supplied native/D3D9 logs, 44 images, and the remaining crash/tour attachments; a 72-check verifier binds the classifications to exact files. No affected device was rerun and no issue fix is claimed.

- **#35:** the failing `fa8b6e55` direct-PanVK/540p/two-mip/512-shadow/dumping run stops before mission Bink open and later shows watchdog/`LockRect`/device-loss correlations. A previous run with the same revision and settings reached frame 65,536 and opened mission Bink videos. This is a comparative historical control, not a current A/B or root-cause result.
- **#33:** old `ea2ba6c9` logs show explicit system-Vulkan-1.1 rejection. The latest recurrence comment supplies no current logs, so its failure remains undiagnosed.
- **#32:** old PurpleVK/Adreno 610 evidence ends at `VK_ERROR_FEATURE_NOT_PRESENT`, but requested feature state does not identify the unsupported feature.
- **#22:** old Mali-G57 logs contain successful reset/present operations and no established visual root cause. Existing full-color DXT1 and compatibility diagnostics are not validated issue fixes.
- **#29:** generic launcher/setup text and an image supply no actionable native/D3D9 failure evidence.

CI run `41861964235` verifies the exact-revision source-built APK and skips release, but the signed artifact payload could not be reopened after redirect EOF. That packaging result does not populate the physical-device table or establish installation, rendering, audio, or issue resolution.

## Phase 12 host lifecycle evidence is not a device result

Exact CI run `41900484812` on `c8709cb5a553b470f8724dce15faae0779a85528` succeeds, including the source-built APK and embedded-DXVK verification; release is skipped. The fresh host XAudio2 controls pass 43 checks in each of four modes, and 32-cycle normal lifecycle fixtures pass 502/470 checks in reuse/non-reuse modes with sanitizer-clean logs. These tests use fake or host dummy audio and do not populate the physical-device table.

Artifact `13359379211` (`prototype-apk`) is server-described as 57,955,156 bytes with archive digest `sha256:f3863a3f1a946fc72d0dfeaae128913231d600b537b3d948f02974862255a6bd`; the signed payload returned EOF, so no local APK/library hash or installation claim is made. The XAudio2 lifetime repair is not relabeled as a fix for #22, #29, #32, #33, or #35. No new FPS, frame-time, thermal, audio-device, rendering, or sustained-play measurement exists.

## Phase 13 host recovery evidence is not a device result

Exact CI run `41911558127` on `2dabecdf3b15b14da56803863bb13b67c80070d6` succeeds, including source-built ARM64 `libmain.so`, APK packaging/signing, and APK/embedded-DXVK verification; release is skipped. The Bink transient-open candidate passes real host SDL/stub controls, both audio rates, 94 concurrent checks, and 62/62 broad commands. These do not establish Android output coexistence, audible recovery, installation, FPS, rendering, thermal, or sustained-play results.

Artifact `13364468289` is server-described as 57,950,912 bytes with archive digest `sha256:0a980739e8fb05c5dabd8ff48cffb4abff3ab33e66b8cb7cf1d5d060edc2f1e1`; this is not an inner-APK hash or device validation. **#22, #29, #32, #33, and #35 remain open and unfixed by this phase.**

## Phase 14 final physical-device gate

The final fresh host regression passes 62/62 commands with 316 independent raw-evidence checks, and exact source CI `41911558127` passes required ARM64/APK jobs. These do not add a device row. The new-run count remains **zero**; no current median FPS, frame-time p50/p95/p99, long-stall, cold/warm shader-loading, GPU-time, thermal, sustained-play, audible-output, installation, or visual/media result exists.

The open PR manifest now identifies the candidate as `com.protodroid` **0.16/code 16**, changed at the user's explicit request on 2026-11-05; released 0.15/code 15 is unchanged. This metadata correction does not close any device or release gate. The CI artifact uses a disposable signing identity and is not release-eligible. **#22, #29, #32, #33, and #35 remain open.** A future authorized candidate requires exact-APK 0.15/candidate device A/B evidence before any performance or issue-resolution claim.
