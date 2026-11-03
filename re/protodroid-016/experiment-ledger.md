# Experiment ledger

Every experiment records hypothesis, exact baseline/candidate revisions, changed files, environment, settings, scene, cache state, raw result, correctness result, and `adopt`, `reject`, or `inconclusive`.

## B0 — 0.15 baseline capture

- Hypothesis: existing fixtures can establish a reproducible host reference without making a device-performance claim.
- Baseline: `18ba66bd0945e405aaded0de5ff8848a8cd07932`
- Candidate: none
- Changed runtime files: none
- Environment: x86_64 Linux, two logical CPUs; no attached Android device
- Scene/cache: deterministic host fixtures; physical scenes not executed
- Raw results: historical `/tmp/protodroid-016/baseline` artifacts were lost during sandbox recreation; summarized historical values remain in `baseline.md` and are not represented as currently available raw logs
- Correctness: all host differential/UBSan fixtures passed; exact-0.15 ARM64 CI correctness was already green
- Decision: host/CI reference established; physical-device baseline remains unmeasured. Baseline only, not an optimization adoption
- Additional evidence decision: mixed-source `TestingFiles` logs were rejected as one run; see `rejected-changes.md`
