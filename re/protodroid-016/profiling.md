# ProtoDroid profiling record

Phase 3 has not started. No bottleneck ranking is claimed yet.

Existing instrumentation available for future measurements:

- Present-to-Present wall intervals with one-second FPS/average/P99/max windows.
- Detailed timing samples with p50/p95/p99/max, fixed long-frame thresholds, and four worst events.
- Per-tour-phase exact Present average/max and rolling-window metrics.
- Process CPU time, available per-core frequency/busy data, thermal status/headroom APIs, and explicit unsupported states.
- Optional guest/import profiling during the isolated diagnostic tour.
- DXVK/native monotonic logs and pipeline tracing; Present wall time is explicitly not GPU time.

Measurement cautions:

- The tour uses a 4x guest clock and is not normal-play performance evidence.
- Rolling one-second windows and tour-phase windows must not be combined as though they were independent raw samples.
- Historical logs from another source revision cannot establish a 0.15 A/B result.
- Host timing and ARM64 CI timing do not establish physical phone FPS or GPU time.
