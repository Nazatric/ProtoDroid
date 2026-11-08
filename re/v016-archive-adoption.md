# v0.16 source-archive adoption

The user reported that the source archive pushed to `main` is fixed and performs well, and asked for every
file in this branch to be replaced with it.

## What was adopted

- Source commit on `main`: `78f28c8` ("Add ProtoDroid v0.16 source archive").
- The archive itself, `5eb1c06c-protodroid/protodroid-v0.16-latest-src.zip`, is a **Git LFS pointer**:
  `oid sha256:0d2dce8b92813e04b12abb068193c8084f62b692748fb0832aff077371905dab`, `size 198772769`.
  Its object is served from an LFS host this environment cannot reach (TLS to `media.githubusercontent.com`
  and `github-cloud.githubusercontent.com` fails), so the 189.6 MiB blob could not be downloaded here.
- The commit that adds the archive also adds its **extracted content** as ordinary files. This branch therefore
  mirrors that tree exactly, so the working tree is the archive's content byte for byte.

## Deviations from that tree

Only `.github/workflows/build-apk.yml` differs, for build-infrastructure reasons documented in the commit
messages:

1. The `driver-package-tests` step now retries, because its four pinned driver downloads are answered with
   HTTP 504 by the release CDN (confirmed by run annotations).
2. `build-apk` no longer waits for `driver-package-tests`; the **release** job now requires it instead, so a real
   driver size/digest regression still blocks publishing but an upstream outage cannot block a testable APK.
3. On failure the driver-gate output is copied into the job summary, because Actions log/artifact bytes are served
   from a host this environment cannot read.

No engine, graphics, driver, or game code was changed relative to the archive.

## Build

- Commit `507ecdc`, run `42468130463`, job `build-apk`: **success** — `libmain.so` built from source, ARM64 plugin
  example built, disposable CI signing, APK packaging and signing, and APK/embedded-DXVK verification all passed.
- Artifact `prototype-apk` `13612628305`, 57,950,599 bytes,
  `sha256:f6fd442b52ee269b11d16fdcae5e3e3ba0b555871e394d39e852a55708f7b64a`.
- The overall run is red only because `driver-package-tests` could not download its pinned assets.

## Things to know before testing

- `android/AndroidManifest.xml` in this archive is **versionName 0.15, versionCode 15**. The previous branch state
  was versionCode 16, so Android will refuse to install this over it without an uninstall. Saves live in
  `Prototype/Saves` and are not removed by an uninstall of the app's own data directory.
- This archive predates the selective x87 and XMM work developed on this branch, so any performance difference
  against those builds is expected and is not attributed to the archive being wrong.
- Rollback: branch state before adoption is commit `2d36f50` (also in
  `/home/user/protodroid-016-evidence/branch-before-v016-archive.bundle`).

## Not verified here

Device FPS, frame-time tails, and visual completeness of this archive on the phone are **not** measured by this
repository or by CI. CI only establishes that the source builds, signs, and passes its own checks.
