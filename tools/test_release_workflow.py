#!/usr/bin/env python3
"""Source checks for complementary CI signing selection and dispatch release parity."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8")


def step(name):
    body = WORKFLOW.split(f"      - name: {name}\n", 1)[1]
    return body.split("\n      - name:", 1)[0]


protected = step("Setup protected release signing identity")
disposable = step("Setup disposable CI signing identity")
assert "env.PUBLISH_RELEASE == 'true'" in protected
assert "env.ALLOW_EPHEMERAL_RELEASE != 'true'" in protected
assert "env.PUBLISH_RELEASE != 'true'" in disposable
assert "env.ALLOW_EPHEMERAL_RELEASE == 'true'" in disposable
assert "github.event_name == 'workflow_dispatch'" not in protected + disposable, (
    "signing selection must use the common dispatch policy, not one event type"
)
for expression in ("PUBLISH_RELEASE:", "ALLOW_EPHEMERAL_RELEASE:", "ALLOW_CERTIFICATE_CHANGE:"):
    assert expression in WORKFLOW
assert WORKFLOW.count("github.event.client_payload.publish_release == true") >= 2
assert "allow_certificate_change:" in WORKFLOW.split("repository_dispatch:", 1)[0]

release_if = WORKFLOW.split("  release:\n", 1)[1].split("    steps:", 1)[0]
assert "allow_ephemeral_release" not in release_if, (
    "a protected repository-dispatch release must not require disposable-release approval"
)
assert "publish_release == true" in release_if
assert 'python3 tools/release_policy.py "$versionName" "$versionCode" "$certificateSha256"' in WORKFLOW
version_guard = step("Reject Android package-version downgrades against PR base")
assert "if: github.event_name == 'pull_request'" in version_guard
assert 'git fetch --no-tags --depth=1 origin "refs/heads/$BASE_REF"' in version_guard
assert 'python3 tools/check_android_version.py android/AndroidManifest.xml' in version_guard
assert "python3 tools/test_android_version.py" in step("Test Android version policy")
assert 'android/java/com/protodroid/ModConfig.java' in WORKFLOW
assert 'android/java/com/protodroid/ImmersiveMode.java' in WORKFLOW
assert 'tools/test_immersive_mode.py' in WORKFLOW
verified_package = step("Verify APK and embedded Android DXVK")
assert "source build/apk/package.txt" in verified_package
assert 'test "$apkBytes" = "$(stat -c%s "$APK")"' in verified_package
assert 'test "$apkSha256" = "$(sha256sum "$APK" | awk \'{print $1}\')"' in verified_package
upload = step("Upload APK artifact")
assert "build/apk/package.txt" in upload
assert "if-no-files-found: error" in upload
print("Workflow signing, Android version-floor, release policy, and exact APK metadata checks passed")
