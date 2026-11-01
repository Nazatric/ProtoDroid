#!/usr/bin/env python3
"""Host tests for release version and signing approval boundaries."""

from release_policy import verify_release

OLD_CERT = "a" * 64
NEW_CERT = "b" * 64
RELEASES = [{
    "tag_name": "protodroid-v0.14",
    "body": f"Version 0.14 (versionCode 14)\nSigning certificate SHA-256: `{OLD_CERT}`",
}]


def expect_error(message, *args, **kwargs):
    try:
        verify_release(*args, **kwargs)
    except ValueError as error:
        assert message in str(error), (message, str(error))
    else:
        raise AssertionError(f"expected release rejection: {message}")


def main():
    assert not verify_release("0.15", 15, OLD_CERT, RELEASES, "persistent", False, False)
    assert not verify_release("0.15", 15, OLD_CERT, RELEASES, "ephemeral", True, False)
    expect_error("allow_ephemeral_release", "0.15", 15, OLD_CERT, RELEASES, "ephemeral", False, False)
    expect_error("allow_certificate_change", "0.15", 15, NEW_CERT, RELEASES, "persistent", False, False)
    assert verify_release("0.15", 15, NEW_CERT, RELEASES, "persistent", False, True), (
        "a persistent certificate transition must not require disposable-release approval"
    )
    expect_error("allow_certificate_change", "0.15", 15, NEW_CERT, RELEASES, "ephemeral", True, False)
    assert verify_release("0.15", 15, NEW_CERT, RELEASES, "ephemeral", True, True)
    expect_error("not newer", "0.14", 15, OLD_CERT, RELEASES, "persistent", False, False)
    expect_error("versionCode is not newer", "0.15", 14, OLD_CERT, RELEASES, "persistent", False, False)
    expect_error("unknown APK signing identity", "0.15", 15, OLD_CERT, RELEASES, "local", False, False)
    print("Release version, persistent-certificate transition, and disposable-release tests passed")


if __name__ == "__main__":
    main()
