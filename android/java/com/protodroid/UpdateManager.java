package com.protodroid;

import android.Manifest;
import android.app.Activity;
import android.app.DownloadManager;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.Settings;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

/**
 * Launcher-only updater. The system DownloadManager owns the bytes and their completion notification, so the APK
 * remains available in public Downloads even when Android kills this activity during a signature-changing uninstall.
 */
final class UpdateManager {
    private static final String TAG = "ProtoDroidUpdate";
    private static final String API_URL = "https://api.github.com/repos/Nazatric/ProtoDroid/releases/latest";
    private static final String PREFS = "launcher_updates";
    private static final String KEY_DOWNLOAD_ID = "download_id";
    private static final String KEY_VERSION_NAME = "version_name";
    private static final String KEY_VERSION_CODE = "version_code";
    private static final String KEY_URL = "url";
    private static final String KEY_SIZE = "size";
    private static final String KEY_APK_SHA256 = "apk_sha256";
    private static final String KEY_CERTIFICATE_SHA256 = "certificate_sha256";
    private static final String KEY_LAST_CHECK = "last_check";
    private static final long CHECK_INTERVAL_MS = 6L * 60L * 60L * 1000L;
    private static final int MAX_METADATA_BYTES = 2 * 1024 * 1024;

    static final int REQUEST_INSTALL = 40;
    static final int REQUEST_UNINSTALL = 41;
    static final int REQUEST_UNKNOWN_SOURCES = 42;
    static final int REQUEST_DOWNLOAD_STORAGE = 43;

    static final class State {
        static final int HIDDEN = 0;
        static final int CHECKING = 1;
        static final int AVAILABLE = 2;
        static final int DOWNLOADING = 3;
        static final int VERIFYING = 4;
        static final int READY = 5;
        static final int ERROR = 6;

        final int status;
        final String versionName;
        final int progress;
        final long downloadedBytes;
        final long totalBytes;
        final String detail;
        final boolean requiresUninstall;

        private State(int status, UpdateRelease release, int progress, long downloadedBytes, long totalBytes,
                      String detail, boolean requiresUninstall) {
            this.status = status;
            this.versionName = release == null ? "" : release.versionName;
            this.progress = progress;
            this.downloadedBytes = downloadedBytes;
            this.totalBytes = totalBytes;
            this.detail = detail == null ? "" : detail;
            this.requiresUninstall = requiresUninstall;
        }

        static State hidden() {
            return new State(HIDDEN, null, -1, 0L, 0L, "", false);
        }

        boolean visible() {
            return status != HIDDEN;
        }

        boolean canPress() {
            return status == AVAILABLE || status == READY || status == ERROR;
        }
    }

    interface Callback {
        void onStateChanged(State state);
        void onUninstallRequired(String versionName);
    }

    private static final class DownloadInfo {
        int status;
        int reason;
        long downloaded;
        long total;
        String path;
    }

    private final Activity activity;
    private final Context context;
    private final Callback callback;
    private final DownloadManager downloads;
    private final SharedPreferences prefs;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final long installedVersionCode;
    private State state = State.hidden();
    private UpdateRelease release;
    private long downloadId = -1L;
    private boolean resumed;
    private boolean closed;
    private boolean checking;
    private boolean verifying;
    private boolean pendingUninstall;

    UpdateManager(Activity activity, Callback callback) {
        this.activity = activity;
        this.context = activity.getApplicationContext();
        this.callback = callback;
        downloads = (DownloadManager) context.getSystemService(Context.DOWNLOAD_SERVICE);
        prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        installedVersionCode = installedVersionCode();
    }

    State currentState() {
        return state;
    }

    void onResume() {
        if (closed) return;
        resumed = true;
        if (pendingUninstall && !isPackageInstalled()) {
            pendingUninstall = false;
            launchInstaller();
            return;
        }
        if (downloadId < 0L) restoreDownload();
        if (state.status == State.HIDDEN || (state.status == State.ERROR && release == null)) {
            checkForUpdates(false);
        } else if (isDownloadActive(state.status)) {
            refreshDownload();
        }
        schedulePoll();
    }

    void onPause() {
        resumed = false;
        main.removeCallbacks(poll);
    }

    void close() {
        closed = true;
        resumed = false;
        main.removeCallbacks(poll);
        worker.shutdownNow();
    }

    void onUpdatePressed() {
        if (closed) return;
        switch (state.status) {
            case State.AVAILABLE:
                beginDownload();
                break;
            case State.READY:
                if (state.requiresUninstall) callback.onUninstallRequired(state.versionName);
                else installWithPermission(false);
                break;
            case State.ERROR:
                if (release == null) checkForUpdates(true);
                else retryDownload();
                break;
            default:
                break;
        }
    }

    /** Called only after the user accepts the launcher's signature-change warning. */
    void confirmUninstall() {
        if (state.status != State.READY || release == null) return;
        installWithPermission(true);
    }

    void onActivityResult(int requestCode, int resultCode) {
        if (closed) return;
        if (requestCode == REQUEST_UNKNOWN_SOURCES) {
            if (canRequestPackageInstalls()) {
                if (pendingUninstall) startUninstall();
                else launchInstaller();
            } else {
                pendingUninstall = false;
                publishReady("Allow app installation to continue; the verified APK remains in Downloads");
            }
        } else if (requestCode == REQUEST_UNINSTALL) {
            if (resultCode == Activity.RESULT_OK || !isPackageInstalled()) {
                pendingUninstall = false;
                launchInstaller();
            } else {
                pendingUninstall = false;
                publishReady("Uninstall cancelled; nothing was removed");
            }
        } else if (requestCode == REQUEST_INSTALL) {
            pendingUninstall = false;
            if (resultCode == Activity.RESULT_OK) {
                clearDownloadPrefs();
                downloadId = -1L;
                release = null;
                publish(State.hidden());
            } else if (release != null) {
                publishReady("Installation was not completed; the verified APK remains in Downloads");
            }
        }
    }

    void onRequestPermissionsResult(int requestCode, int[] grantResults) {
        if (requestCode != REQUEST_DOWNLOAD_STORAGE || closed) return;
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) beginDownload();
        else publishError("Storage permission is needed to keep the APK in Downloads", true);
    }

    private void checkForUpdates(boolean manual) {
        if (checking || verifying || closed || downloads == null) return;
        if (!manual && (state.visible() || System.currentTimeMillis() - prefs.getLong(KEY_LAST_CHECK, 0L)
                < CHECK_INTERVAL_MS)) return;
        checking = true;
        // Back off failed/rate-limited automatic checks too; a visible Retry remains an explicit user action.
        prefs.edit().putLong(KEY_LAST_CHECK, System.currentTimeMillis()).apply();
        publish(new State(State.CHECKING, null, -1, 0L, 0L, "Checking GitHub releases", false));
        try {
            worker.execute(() -> {
                try {
                    UpdateRelease latest = fetchLatest();
                    main.post(() -> {
                        checking = false;
                        if (closed) return;
                        prefs.edit().putLong(KEY_LAST_CHECK, System.currentTimeMillis()).apply();
                        if (latest == null || !latest.isNewerThan(installedVersionCode)) {
                            release = null;
                            publish(State.hidden());
                        } else {
                            release = latest;
                            restoreMatchingDownload();
                            if (downloadId < 0L) publishAvailable();
                        }
                    });
                } catch (Exception e) {
                    Log.w(TAG, "Update check failed", e);
                    main.post(() -> {
                        checking = false;
                        if (closed) return;
                        if (manual) publishError("Could not check GitHub; tap retry", false);
                        else publish(State.hidden());
                    });
                }
            });
        } catch (RejectedExecutionException e) {
            checking = false;
            if (!closed) publish(State.hidden());
        }
    }

    private UpdateRelease fetchLatest() throws Exception {
        HttpURLConnection connection = (HttpURLConnection) new URL(API_URL).openConnection();
        try {
            connection.setConnectTimeout(8000);
            connection.setReadTimeout(12000);
            connection.setUseCaches(false);
            connection.setInstanceFollowRedirects(true);
            connection.setRequestProperty("Accept", "application/vnd.github+json");
            connection.setRequestProperty("X-GitHub-Api-Version", "2022-11-28");
            connection.setRequestProperty("User-Agent", "ProtoDroid-Android-Updater");
            if (connection.getResponseCode() != HttpURLConnection.HTTP_OK) {
                throw new IOException("GitHub returned HTTP " + connection.getResponseCode());
            }
            String json;
            try (InputStream input = connection.getInputStream()) {
                json = readLimited(input, MAX_METADATA_BYTES);
            }
            return UpdateRelease.parse(json);
        } finally {
            connection.disconnect();
        }
    }

    private static String readLimited(InputStream input, int limit) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int total = 0;
        for (int count; (count = input.read(buffer)) != -1;) {
            total += count;
            if (total > limit) throw new IOException("release metadata is too large");
            output.write(buffer, 0, count);
        }
        return new String(output.toByteArray(), StandardCharsets.UTF_8);
    }

    private void beginDownload() {
        if (release == null || closed || downloads == null || verifying) return;
        if (Build.VERSION.SDK_INT <= 28
                && context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            activity.requestPermissions(new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    REQUEST_DOWNLOAD_STORAGE);
            return;
        }
        if (downloadId >= 0L) {
            DownloadInfo existing = queryDownload(downloadId);
            if (existing != null && existing.status != DownloadManager.STATUS_FAILED) {
                refreshDownload();
                return;
            }
            removeDownload();
        }
        try {
            String filename = "protodroid-" + release.versionName + "-" + release.versionCode + "-"
                    + System.currentTimeMillis() + ".apk";
            DownloadManager.Request request = new DownloadManager.Request(Uri.parse(release.downloadUrl));
            request.setTitle("ProtoDroid " + release.versionName);
            request.setDescription("Verified update; shared game files remain in Prototype");
            request.setMimeType(UpdateRelease.APK_MIME);
            request.setAllowedOverMetered(true);
            request.setAllowedOverRoaming(false);
            request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            request.setVisibleInDownloadsUi(true);
            request.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, filename);
            downloadId = downloads.enqueue(request);
            saveDownloadPrefs();
            publishDownloading(0L, release.sizeBytes, "Starting download");
            schedulePoll();
        } catch (Exception e) {
            Log.w(TAG, "Unable to enqueue update", e);
            publishError("Could not start the download; tap retry", true);
        }
    }

    private void retryDownload() {
        if (release == null) {
            checkForUpdates(true);
            return;
        }
        if (downloadId >= 0L) removeDownload();
        beginDownload();
    }

    private void restoreDownload() {
        long savedId = prefs.getLong(KEY_DOWNLOAD_ID, -1L);
        UpdateRelease saved = readSavedRelease();
        if (savedId < 0L || saved == null || !saved.isNewerThan(installedVersionCode)) {
            clearDownloadPrefs();
            return;
        }
        DownloadInfo info = queryDownload(savedId);
        if (info == null) {
            clearDownloadPrefs();
            return;
        }
        release = saved;
        downloadId = savedId;
        applyDownloadInfo(info);
    }

    private void restoreMatchingDownload() {
        long savedId = prefs.getLong(KEY_DOWNLOAD_ID, -1L);
        UpdateRelease saved = readSavedRelease();
        if (savedId < 0L || saved == null || release == null || saved.versionCode != release.versionCode
                || !saved.downloadUrl.equals(release.downloadUrl)) {
            clearDownloadPrefs();
            downloadId = -1L;
            return;
        }
        downloadId = savedId;
        release = saved;
        DownloadInfo info = queryDownload(savedId);
        if (info == null) {
            clearDownloadPrefs();
            downloadId = -1L;
        } else {
            applyDownloadInfo(info);
        }
    }

    private void applyDownloadInfo(DownloadInfo info) {
        if (info.status == DownloadManager.STATUS_SUCCESSFUL) {
            verifyDownload();
        } else if (info.status == DownloadManager.STATUS_FAILED) {
            publishError("Download failed (" + info.reason + "); tap retry", true);
        } else {
            String detail = info.status == DownloadManager.STATUS_PAUSED
                    ? "Paused; waiting for network" : "Downloading official APK";
            publishDownloading(info.downloaded, info.total > 0L ? info.total : release.sizeBytes, detail);
        }
    }

    private void refreshDownload() {
        if (downloadId < 0L || release == null || verifying || closed) return;
        DownloadInfo info = queryDownload(downloadId);
        if (info == null) {
            publishError("Download is no longer available; tap retry", true);
            return;
        }
        applyDownloadInfo(info);
    }

    private void verifyDownload() {
        if (verifying || downloadId < 0L || release == null || closed) return;
        verifying = true;
        final long id = downloadId;
        final UpdateRelease expected = release;
        publish(new State(State.VERIFYING, expected, 100, expected.sizeBytes, expected.sizeBytes,
                "Verifying APK and package identity", false));
        try {
            worker.execute(() -> {
                String error = null;
                try {
                    verifyBytes(id, expected);
                    verifyPackage(id, expected);
                } catch (Exception e) {
                    Log.w(TAG, "Downloaded update failed verification", e);
                    error = e.getMessage() == null ? "verification failed" : e.getMessage();
                }
                final String failure = error;
                main.post(() -> {
                    if (closed || id != downloadId || release == null
                            || release.versionCode != expected.versionCode) return;
                    verifying = false;
                    if (failure == null) {
                        publishReady("Verified official APK; ready to install");
                    } else {
                        removeDownload();
                        publishError("APK verification failed; tap retry", true);
                    }
                });
            });
        } catch (RejectedExecutionException e) {
            verifying = false;
        }
    }

    private void verifyBytes(long id, UpdateRelease expected) throws Exception {
        DownloadInfo info = queryDownload(id);
        if (info == null || info.status != DownloadManager.STATUS_SUCCESSFUL) {
            throw new IOException("download is incomplete");
        }
        if (info.total > 0L && info.total != expected.sizeBytes) throw new IOException("APK size differs");
        ParcelFileDescriptor descriptor = downloads.openDownloadedFile(id);
        if (descriptor == null) throw new IOException("downloaded APK is unavailable");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long size = 0L;
        try (InputStream input = new FileInputStream(descriptor.getFileDescriptor())) {
            byte[] buffer = new byte[64 * 1024];
            for (int count; (count = input.read(buffer)) != -1;) {
                digest.update(buffer, 0, count);
                size += count;
            }
        } finally {
            descriptor.close();
        }
        if (size != expected.sizeBytes) throw new IOException("APK size differs");
        String actual = hex(digest.digest());
        if (!expected.apkSha256.equalsIgnoreCase(actual)) throw new IOException("APK SHA-256 differs");
    }

    private void verifyPackage(long id, UpdateRelease expected) throws Exception {
        DownloadInfo info = queryDownload(id);
        if (info == null || info.path == null || info.path.isEmpty()) {
            throw new IOException("APK path is unavailable for package verification");
        }
        int flags = Build.VERSION.SDK_INT >= 28
                ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
        PackageInfo apk = context.getPackageManager().getPackageArchiveInfo(info.path, flags);
        if (apk == null) throw new IOException("Android cannot read the APK");
        if (!context.getPackageName().equals(apk.packageName)) throw new IOException("wrong Android package");
        long code = Build.VERSION.SDK_INT >= 28 ? apk.getLongVersionCode() : apk.versionCode;
        if (code != expected.versionCode || !expected.versionName.equals(apk.versionName)) {
            throw new IOException("APK version differs from release metadata");
        }
        String certificate = certificate(apk);
        if (certificate == null || !expected.certificateSha256.equalsIgnoreCase(certificate)) {
            throw new IOException("APK signing certificate differs");
        }
    }

    private DownloadInfo queryDownload(long id) {
        if (downloads == null) return null;
        Cursor cursor = null;
        try {
            cursor = downloads.query(new DownloadManager.Query().setFilterById(id));
            if (cursor == null || !cursor.moveToFirst()) return null;
            DownloadInfo info = new DownloadInfo();
            info.status = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS));
            info.reason = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON));
            info.downloaded = cursor.getLong(cursor.getColumnIndexOrThrow(
                    DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR));
            info.total = cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES));
            int pathColumn = cursor.getColumnIndex(DownloadManager.COLUMN_LOCAL_FILENAME);
            if (pathColumn >= 0 && !cursor.isNull(pathColumn)) info.path = cursor.getString(pathColumn);
            return info;
        } catch (Exception e) {
            Log.w(TAG, "Unable to query download " + id, e);
            return null;
        } finally {
            if (cursor != null) cursor.close();
        }
    }

    private void installWithPermission(boolean uninstallAfter) {
        if (release == null || downloadId < 0L || closed) return;
        pendingUninstall = uninstallAfter;
        if (!canRequestPackageInstalls()) {
            try {
                Intent settings = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + context.getPackageName()));
                activity.startActivityForResult(settings, REQUEST_UNKNOWN_SOURCES);
            } catch (Exception e) {
                pendingUninstall = false;
                publishError("Android could not open the app-install permission screen", true);
            }
            return;
        }
        if (uninstallAfter) startUninstall();
        else launchInstaller();
    }

    private boolean canRequestPackageInstalls() {
        return Build.VERSION.SDK_INT < 26 || context.getPackageManager().canRequestPackageInstalls();
    }

    private void startUninstall() {
        if (release == null || downloadId < 0L || closed) return;
        pendingUninstall = true;
        try {
            Intent uninstall = new Intent(Intent.ACTION_UNINSTALL_PACKAGE,
                    Uri.parse("package:" + context.getPackageName()));
            uninstall.putExtra(Intent.EXTRA_RETURN_RESULT, true);
            activity.startActivityForResult(uninstall, REQUEST_UNINSTALL);
        } catch (Exception e) {
            pendingUninstall = false;
            publishError("Android could not open the uninstall prompt", true);
        }
    }

    private void launchInstaller() {
        if (release == null || downloadId < 0L || closed || downloads == null) return;
        Uri uri = downloads.getUriForDownloadedFile(downloadId);
        if (uri == null) {
            publishError("The downloaded APK is unavailable; tap retry", true);
            return;
        }
        pendingUninstall = false;
        Intent install = new Intent(Intent.ACTION_INSTALL_PACKAGE);
        install.setDataAndType(uri, UpdateRelease.APK_MIME);
        install.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        install.putExtra(Intent.EXTRA_RETURN_RESULT, true);
        install.putExtra(Intent.EXTRA_NOT_UNKNOWN_SOURCE, true);
        install.putExtra(Intent.EXTRA_ALLOW_REPLACE, true);
        install.putExtra(Intent.EXTRA_INSTALLER_PACKAGE_NAME, context.getPackageName());
        try {
            activity.startActivityForResult(install, REQUEST_INSTALL);
        } catch (ActivityNotFoundException e) {
            install.setAction(Intent.ACTION_VIEW);
            try {
                activity.startActivityForResult(install, REQUEST_INSTALL);
            } catch (Exception failure) {
                publishError("No Android package installer is available; open Downloads", true);
            }
        } catch (Exception e) {
            publishError("Could not open the installer; open Downloads", true);
        }
    }

    private boolean isPackageInstalled() {
        try {
            context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }

    private void publishAvailable() {
        if (release == null) return;
        publish(new State(State.AVAILABLE, release, -1, 0L, release.sizeBytes,
                formatSize(release.sizeBytes) + " • official GitHub release", false));
    }

    private void publishDownloading(long downloaded, long total, String detail) {
        if (release == null) return;
        int progress = total > 0L && downloaded >= 0L
                ? (int) Math.max(0L, Math.min(100L, downloaded * 100L / total)) : -1;
        publish(new State(State.DOWNLOADING, release, progress, downloaded, total, detail, false));
    }

    private void publishReady(String detail) {
        if (release == null) return;
        boolean requiresUninstall = !release.certificateSha256.equalsIgnoreCase(installedCertificate());
        publish(new State(State.READY, release, 100, release.sizeBytes, release.sizeBytes,
                detail + (requiresUninstall ? " • uninstall first" : " • in-place update"), requiresUninstall));
    }

    private void publishError(String detail, boolean keepRelease) {
        if (!keepRelease) release = null;
        publish(new State(State.ERROR, keepRelease ? release : null, -1, 0L,
                release == null ? 0L : release.sizeBytes, detail, false));
    }

    private void publish(State next) {
        if (closed) return;
        state = next;
        callback.onStateChanged(next);
    }

    private boolean isDownloadActive(int status) {
        return status == State.DOWNLOADING || status == State.VERIFYING;
    }

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            if (!resumed || closed) return;
            refreshDownload();
            schedulePoll();
        }
    };

    private void schedulePoll() {
        main.removeCallbacks(poll);
        if (resumed && isDownloadActive(state.status)) main.postDelayed(poll, 1000L);
    }

    private void saveDownloadPrefs() {
        prefs.edit()
                .putLong(KEY_DOWNLOAD_ID, downloadId)
                .putString(KEY_VERSION_NAME, release.versionName)
                .putLong(KEY_VERSION_CODE, release.versionCode)
                .putString(KEY_URL, release.downloadUrl)
                .putLong(KEY_SIZE, release.sizeBytes)
                .putString(KEY_APK_SHA256, release.apkSha256)
                .putString(KEY_CERTIFICATE_SHA256, release.certificateSha256)
                .apply();
    }

    private UpdateRelease readSavedRelease() {
        String name = prefs.getString(KEY_VERSION_NAME, null);
        String url = prefs.getString(KEY_URL, null);
        String apkSha256 = prefs.getString(KEY_APK_SHA256, null);
        String certificate = prefs.getString(KEY_CERTIFICATE_SHA256, null);
        long code = prefs.getLong(KEY_VERSION_CODE, -1L);
        long size = prefs.getLong(KEY_SIZE, -1L);
        if (name == null || url == null || apkSha256 == null || certificate == null || code <= 0L || size <= 0L) {
            return null;
        }
        return new UpdateRelease(name, code, url, size, apkSha256, certificate);
    }

    private void removeDownload() {
        if (downloadId >= 0L && downloads != null) {
            try {
                downloads.remove(downloadId);
            } catch (Exception e) {
                Log.w(TAG, "Unable to remove owned download", e);
            }
        }
        downloadId = -1L;
        clearDownloadPrefs();
    }

    private void clearDownloadPrefs() {
        prefs.edit().remove(KEY_DOWNLOAD_ID).remove(KEY_VERSION_NAME).remove(KEY_VERSION_CODE).remove(KEY_URL)
                .remove(KEY_SIZE).remove(KEY_APK_SHA256).remove(KEY_CERTIFICATE_SHA256).apply();
    }

    private long installedVersionCode() {
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            return Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
        } catch (PackageManager.NameNotFoundException e) {
            return 0L;
        }
    }

    private String installedCertificate() {
        try {
            int flags = Build.VERSION.SDK_INT >= 28
                    ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), flags);
            return certificate(info);
        } catch (Exception e) {
            return "";
        }
    }

    @SuppressWarnings("deprecation")
    private static String certificate(PackageInfo info) throws NoSuchAlgorithmException {
        Signature[] signatures;
        if (Build.VERSION.SDK_INT >= 28) {
            if (info.signingInfo == null || info.signingInfo.hasMultipleSigners()) return null;
            signatures = info.signingInfo.getApkContentsSigners();
        } else {
            signatures = info.signatures;
        }
        if (signatures == null || signatures.length != 1) return null;
        return hex(MessageDigest.getInstance("SHA-256").digest(signatures[0].toByteArray()));
    }

    private static String hex(byte[] bytes) {
        final char[] digits = "0123456789abcdef".toCharArray();
        char[] result = new char[bytes.length * 2];
        for (int i = 0; i < bytes.length; i++) {
            int value = bytes[i] & 0xff;
            result[i * 2] = digits[value >>> 4];
            result[i * 2 + 1] = digits[value & 0x0f];
        }
        return new String(result);
    }

    private static String formatSize(long bytes) {
        return String.format(Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024.0));
    }
}
