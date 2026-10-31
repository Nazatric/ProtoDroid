package com.protodroid;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** The small, authenticated release contract used by the launcher updater. */
final class UpdateRelease {
    static final String APK_MIME = "application/vnd.android.package-archive";
    private static final long MAX_APK_BYTES = 512L * 1024L * 1024L;
    private static final Pattern TAG = Pattern.compile("protodroid-v([0-9]+\\.[0-9]+)");
    private static final Pattern RELEASE_VERSION = Pattern.compile(
            "(?i)\\bVersion\\s+([0-9]+\\.[0-9]+)\\s*\\(versionCode\\s+([0-9]+)\\)");
    private static final Pattern APK_SHA256 = Pattern.compile(
            "(?i)APK SHA-256:\\s*`?([0-9a-f]{64})`?");
    private static final Pattern CERTIFICATE_SHA256 = Pattern.compile(
            "(?i)Signing certificate SHA-256:\\s*`?([0-9a-f]{64})`?");

    final String versionName;
    final long versionCode;
    final String downloadUrl;
    final long sizeBytes;
    final String apkSha256;
    final String certificateSha256;

    UpdateRelease(String versionName, long versionCode, String downloadUrl, long sizeBytes,
                  String apkSha256, String certificateSha256) {
        this.versionName = versionName;
        this.versionCode = versionCode;
        this.downloadUrl = downloadUrl;
        this.sizeBytes = sizeBytes;
        this.apkSha256 = apkSha256;
        this.certificateSha256 = certificateSha256;
    }

    boolean isNewerThan(long installedVersionCode) {
        return versionCode > installedVersionCode;
    }

    static UpdateRelease parse(String json) throws JSONException {
        JSONObject root = new JSONObject(json);
        if (root.optBoolean("draft", false) || root.optBoolean("prerelease", false)) {
            throw new JSONException("draft or prerelease is not an update channel");
        }

        Matcher tag = TAG.matcher(root.optString("tag_name", ""));
        if (!tag.matches()) throw new JSONException("unexpected release tag");
        String versionName = tag.group(1);
        String body = root.optString("body", "");
        Matcher statedVersion = RELEASE_VERSION.matcher(body);
        if (!statedVersion.find()) throw new JSONException("release has no verified versionCode");
        if (!versionName.equals(statedVersion.group(1))) {
            throw new JSONException("release tag and verification version differ");
        }
        long versionCode = positiveNumber(statedVersion.group(2));
        String apkSha256 = hash(APK_SHA256, body, "APK SHA-256");
        String certificateSha256 = hash(CERTIFICATE_SHA256, body, "signing certificate SHA-256");

        JSONArray assets = root.optJSONArray("assets");
        if (assets == null) throw new JSONException("release has no assets");
        JSONObject apk = null;
        for (int i = 0; i < assets.length(); i++) {
            JSONObject candidate = assets.optJSONObject(i);
            if (candidate != null && "prototype.apk".equals(candidate.optString("name", ""))) {
                apk = candidate;
                break;
            }
        }
        if (apk == null) throw new JSONException("release has no prototype.apk");
        String downloadUrl = apk.optString("browser_download_url", "");
        validateDownloadUrl(downloadUrl);
        long sizeBytes = apk.optLong("size", -1L);
        if (sizeBytes <= 0L || sizeBytes > MAX_APK_BYTES) {
            throw new JSONException("invalid APK size");
        }
        return new UpdateRelease(versionName, versionCode, downloadUrl, sizeBytes, apkSha256, certificateSha256);
    }

    private static long positiveNumber(String text) throws JSONException {
        try {
            long value = Long.parseLong(text);
            if (value <= 0L) throw new JSONException("invalid versionCode");
            return value;
        } catch (NumberFormatException e) {
            throw new JSONException("invalid versionCode");
        }
    }

    private static String hash(Pattern pattern, String text, String label) throws JSONException {
        Matcher matcher = pattern.matcher(text);
        if (!matcher.find()) throw new JSONException("release has no " + label);
        return matcher.group(1).toLowerCase(Locale.ROOT);
    }

    private static void validateDownloadUrl(String value) throws JSONException {
        try {
            URI uri = new URI(value);
            String host = uri.getHost();
            if (!"https".equalsIgnoreCase(uri.getScheme()) || host == null
                    || !("github.com".equalsIgnoreCase(host) || "www.github.com".equalsIgnoreCase(host))
                    || uri.getUserInfo() != null || uri.getPath() == null
                    || !uri.getPath().startsWith("/Nazatric/ProtoDroid/releases/download/")) {
                throw new JSONException("APK URL is not the official ProtoDroid release");
            }
        } catch (URISyntaxException e) {
            throw new JSONException("invalid APK URL");
        }
    }
}
