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
assert 'android/java/com/protodroid/ModConfig.java' in WORKFLOW
print("Workflow signing selection, dispatch parity, and separate certificate approval checks passed")
