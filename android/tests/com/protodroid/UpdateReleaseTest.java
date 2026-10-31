package com.protodroid;

import org.json.JSONException;

/** Host-side contract tests; run with the small JSON-java test dependency, not Android stub classes. */
public final class UpdateReleaseTest {
    private static final String VALID = "{"
            + "\"tag_name\":\"protodroid-v0.10\",\"draft\":false,\"prerelease\":false,"
            + "\"body\":\"Version 0.10 (versionCode 11)\\n"
            + "APK SHA-256: `0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef`\\n"
            + "Signing certificate SHA-256: `abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789`\","
            + "\"assets\":[{\"name\":\"prototype.apk\",\"size\":47672216,"
            + "\"browser_download_url\":\"https://github.com/Nazatric/ProtoDroid/releases/download/protodroid-v0.10/prototype.apk\"}]"
            + "}";

    public static void main(String[] args) throws Exception {
        UpdateRelease release = UpdateRelease.parse(VALID);
        check("0.10".equals(release.versionName), "visible version");
        check(release.versionCode == 11L, "integer versionCode");
        check(release.isNewerThan(10L) && !release.isNewerThan(11L), "version comparison");
        check(release.sizeBytes == 47672216L, "asset size");
        check(release.apkSha256.startsWith("0123456789abcdef"), "APK digest");
        check(release.certificateSha256.startsWith("abcdef0123456789"), "certificate digest");
        UpdateRelease withHistoricalNote = UpdateRelease.parse(VALID.replace(
                "Version 0.10 (versionCode 11)", "Older builds used versionCode 4.\\nVersion 0.10 (versionCode 11)"));
        check(withHistoricalNote.versionCode == 11L, "verification line wins over changelog text");

        reject(VALID.replace("\"draft\":false", "\"draft\":true"), "draft");
        reject(VALID.replace("\"prerelease\":false", "\"prerelease\":true"), "prerelease");
        reject(VALID.replace("prototype.apk\",", "other.apk\","), "asset name");
        reject(VALID.replace("https://github.com/Nazatric/ProtoDroid", "https://example.com/Nazatric/ProtoDroid"),
                "download host");
        reject(VALID.replace("versionCode 11", "versionCode eleven"), "version code");
        reject(VALID.replace("Version 0.10", "Version 0.9"), "tag/body mismatch");
        reject(VALID.replace("APK SHA-256:", "APK digest:"), "APK hash");
        reject(VALID.replace("Signing certificate SHA-256:", "Certificate:"), "certificate hash");
        reject(VALID.replace("\"size\":47672216", "\"size\":0"), "empty asset");
        System.out.println("UpdateRelease tests passed");
    }

    private static void reject(String json, String label) throws Exception {
        try {
            UpdateRelease.parse(json);
            throw new AssertionError("accepted invalid " + label);
        } catch (JSONException expected) {
            // Expected: malformed or unverifiable metadata must never reach the installer.
        }
    }

    private static void check(boolean condition, String label) {
        if (!condition) throw new AssertionError(label);
    }
}
