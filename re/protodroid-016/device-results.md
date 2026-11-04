# Physical-device results

No physical Android device or `adb` connection is available in the current environment. No new device benchmark has been run for 0.16. Phases 1–12 are complete only within their recorded source/host/native-CI scopes. Phase 12 adds exact XAudio2/FAudio lifecycle controls, sustained historical-log classification, broad regression, and issue-log classification; none supplies current physical-device FPS, frame-time, rendering, audio-device, thermal, or sustained-playback evidence.

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
