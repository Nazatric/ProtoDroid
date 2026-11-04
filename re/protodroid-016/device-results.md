# Physical-device results

No physical Android device or `adb` connection is available in the current environment. No new device benchmark has been run for 0.16. Phase 7 is complete with host memory/I/O fixtures only. Phase 8 is complete with host D3D9 bridge fixtures only. Phase 9 is active with source/host/native-Linux-ARM64 cache fixtures only. The tracked real Prototype cache supplies entry counts and bytes, not a current physical-device timing run; no result fills the physical table or establishes startup, streaming, FPS, or frame-pacing improvement.

| Device/GPU/driver | Source/APK | Settings/scene/cache | Runs | Result | Limitations |
| --- | --- | --- | --- | --- | --- |
| None attached | N/A | N/A | 0 | Unmeasured | Host and CI evidence cannot fill this row |

Historical logs may guide scene selection and instrumentation, but are entered only with their original source revision and are never relabeled as released-0.15 or candidate-0.16 results.

Phase 6 host synchronization/Present measurements and sanitizer/regression runs are not physical Play or diagnostic-tour results. Device FPS, frame-time distributions, audio continuity, shutdown behavior, thermals, and sustained pacing remain unmeasured.
