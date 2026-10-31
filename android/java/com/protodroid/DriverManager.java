package com.protodroid;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.provider.OpenableColumns;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Android lifecycle and background operations for the private, validated driver library. */
final class DriverManager {
    interface Observer {
        void onDriverStateChanged();
    }

    static final int REQUEST_IMPORT = 410;
    private static final String LEGACY_IGNORED = "legacy_driver_ignored";
    private static final String ACTIVE_ID = "custom_driver_id";

    private final Activity activity;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final SharedPreferences preferences;
    private volatile DriverCatalog.Download activeDownload;
    private volatile Observer observer;
    private volatile boolean busy;
    private volatile boolean closed;
    private volatile String operationTitle = "";
    private volatile String statusTitle = "";
    private volatile String statusMessage = "";
    private volatile long progressDone;
    private volatile long progressTotal;

    DriverManager(Activity activity) {
        this.activity = activity;
        preferences = activity.getSharedPreferences("controls", Activity.MODE_PRIVATE);
    }

    void setObserver(Observer next) {
        observer = next;
    }

    private File files() {
        return activity.getFilesDir();
    }

    List<DriverRepository.Entry> installed() {
        return DriverRepository.list(files());
    }

    /** A missing selection is never silently replaced with a different custom package. */
    DriverRepository.Entry selected() {
        return selected(activity);
    }

    static DriverRepository.Entry selected(android.content.Context context) {
        SharedPreferences preferences = context.getSharedPreferences("controls", Activity.MODE_PRIVATE);
        if (preferences.getInt("vulkan_driver", 0) != 3) return null;
        String id = preferences.getString(ACTIVE_ID, "");
        DriverRepository.Entry selected = DriverRepository.find(context.getFilesDir(), id);
        if (selected != null) return selected;
        // The original driver manager had one slot and no id preference. Preserve that installation on upgrade.
        if (id.isEmpty()) return DriverRepository.find(context.getFilesDir(), DriverRepository.LEGACY_ID);
        return null;
    }

    void select(String id) {
        if (closed || busy) return;
        DriverRepository.Entry entry = DriverRepository.find(files(), id);
        if (entry == null) {
            setStatus("Driver unavailable", "That package is no longer valid. Choose another installed driver or Auto.");
            return;
        }
        preferences.edit().putString(ACTIVE_ID, id).apply();
        clearFailureMarker();
        setStatus("Custom driver selected", entry.installed.name
                + " will be tried in ProtoDroid's isolated probe on the next Play. "
                + "If it cannot initialize safely, the system driver is used and the reason is logged.");
    }

    /** Returns the selected file only for Custom; callers still decide whether Custom is the requested mode. */
    File driverForLaunch() {
        migrateLegacyIfNeeded();
        DriverRepository.Entry selected = selected();
        if (selected != null) return selected.installed.libraryFile();
        return noDriverForLaunch();
    }

    private File noDriverForLaunch() {
        if (preferences.getInt("vulkan_driver", 0) == 3) {
            preferences.edit().remove(ACTIVE_ID).apply();
            new GameSettings(activity).setCustomDriverSelected(false);
            Toast.makeText(activity, "No valid custom driver is selected; Auto was restored.", Toast.LENGTH_LONG).show();
        }
        return null;
    }

    private void migrateLegacyIfNeeded() {
        if (selected() != null) return;
        File oldPrivate = new File(DriverRepository.legacyRoot(files()), "libvulkan_custom.so");
        if (oldPrivate.isFile()) {
            migrateLegacy(oldPrivate);
            return;
        }
        if (preferences.getBoolean(LEGACY_IGNORED, false)) return;
        File external = activity.getExternalFilesDir(null);
        File[] found = external == null ? null : new File(external, "driver").listFiles(
                (dir, name) -> name.endsWith(".so"));
        if (found != null && found.length > 0) {
            Arrays.sort(found);
            migrateLegacy(found[0]);
        }
    }

    private void migrateLegacy(File source) {
        try (InputStream input = new FileInputStream(source)) {
            DriverRepository.Entry installed = DriverRepository.install(files(), "", input, source.getName(),
                    Build.VERSION.SDK_INT);
            preferences.edit().putString(ACTIVE_ID, installed.id).putBoolean(LEGACY_IGNORED, true).apply();
            // Builds before the picker had no mode preference: retain their old custom-driver behavior.
            if (!preferences.contains("vulkan_driver")) new GameSettings(activity).setCustomDriverSelected(true);
        } catch (Exception e) {
            android.util.Log.w("ProtoDroid", "Could not migrate the previous custom driver", e);
        }
    }

    void openPicker() {
        if (closed || busy) return;
        if (Build.VERSION.SDK_INT < 29) {
            setStatus("Android version too old", "Custom drivers need Android 10 (API 29) or newer. Auto and System remain available.");
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // Document providers often use vendor-specific MIME types for .so and ZIP files; validate content instead.
        intent.setType("*/*");
        activity.startActivityForResult(intent, REQUEST_IMPORT);
    }

    boolean onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_IMPORT) return false;
        if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) startImport(data.getData());
        return true;
    }

    void download(DriverCatalog.Entry entry) {
        if (closed || busy) return;
        if (Build.VERSION.SDK_INT < entry.minApi) {
            setStatus("Android version too old", entry.title + " needs Android API " + entry.minApi + " or newer.");
            return;
        }
        startDownload(entry);
    }

    private void startImport(Uri uri) {
        if (!beginOperation("Importing driver")) return;
        String name = displayName(uri);
        worker.execute(() -> {
            try {
                ContentResolver resolver = activity.getContentResolver();
                try (InputStream input = resolver.openInputStream(uri)) {
                    if (input == null) throw new IOException("The selected file could not be opened");
                    DriverRepository.Entry installed = DriverRepository.install(files(), "", input, name,
                            Build.VERSION.SDK_INT);
                    installSucceeded(installed);
                }
            } catch (Exception e) {
                operationFailed("Import failed", e);
            } finally {
                endOperation();
            }
        });
    }

    private void startDownload(DriverCatalog.Entry entry) {
        if (!beginOperation("Downloading " + entry.title)) return;
        File partial = new File(activity.getCacheDir(), "driver-download.part");
        // Only our own temporary file is removed; an installed package is never a download destination.
        partial.delete();
        DriverCatalog.Download download = new DriverCatalog.Download();
        activeDownload = download;
        worker.execute(() -> {
            try {
                download.run(entry, partial, (done, total) -> {
                    progressDone = done;
                    progressTotal = total;
                    notifyObserver();
                });
                if (download.isCancelled()) throw new CancellationException();
                activeDownload = null;
                setStatus("Verifying and installing", entry.title + " passed its pinned SHA-256 check. Validating the Android package…");
                DriverRepository.Entry installed;
                try (InputStream input = new FileInputStream(partial)) {
                    installed = DriverRepository.install(files(), entry.id, input, entry.title + ".zip",
                            Build.VERSION.SDK_INT);
                }
                installSucceeded(installed);
            } catch (Exception e) {
                operationFailed("Download failed", e);
            } finally {
                partial.delete();
                activeDownload = null;
                endOperation();
            }
        });
    }

    private boolean beginOperation(String title) {
        if (busy || closed) return false;
        busy = true;
        operationTitle = title;
        progressDone = 0;
        progressTotal = 0;
        statusTitle = title;
        statusMessage = "Keep ProtoDroid open. Your installed drivers and current selection are not changed until validation succeeds.";
        notifyObserver();
        return true;
    }

    void cancel() {
        DriverCatalog.Download download = activeDownload;
        if (download != null) download.cancel();
    }

    private void installSucceeded(DriverRepository.Entry installed) {
        preferences.edit().putBoolean(LEGACY_IGNORED, true).apply();
        clearFailureMarker();
        String name = installed.installed.name;
        setStatus("Driver installed", name + " is ready in your library. Auto remains unchanged; "
                + "choose Custom, then Select on this card when you want to use it. No system driver was modified.");
    }

    private void operationFailed(String title, Exception error) {
        if (error instanceof CancellationException) {
            setStatus("Download cancelled", "The partial file was discarded. Your installed drivers and selection were not changed.");
            return;
        }
        String detail = error.getMessage();
        if (detail == null || detail.trim().isEmpty()) detail = error.getClass().getSimpleName();
        setStatus(title, detail + " The previous installation and current selection were not changed.");
    }

    private void endOperation() {
        activity.runOnUiThread(() -> {
            if (closed) return;
            busy = false;
            operationTitle = "";
            progressDone = 0;
            progressTotal = 0;
            notifyObserver();
        });
    }

    /** Synchronous because removal is local and does not touch the game, saves, or Android's global driver. */
    boolean remove(String id) {
        if (closed || busy) return false;
        try {
            DriverRepository.Entry selected = selected();
            boolean removed = DriverRepository.remove(files(), id);
            if (!removed) {
                setStatus("Driver not found", "The package may already have been removed. Your other drivers were not changed.");
                return false;
            }
            String activeId = preferences.getString(ACTIVE_ID, "");
            if (activeId.equals(id) || (selected != null && selected.id.equals(id))) {
                preferences.edit().remove(ACTIVE_ID).apply();
                if (selected != null && selected.id.equals(id)) new GameSettings(activity).setCustomDriverSelected(false);
            }
            clearFailureMarker();
            setStatus("Driver removed", "Only ProtoDroid's private copy was removed. Auto is available, and your original ZIP/.so, saves, and system driver are unchanged.");
            return true;
        } catch (IOException e) {
            setStatus("Could not remove driver", e.getMessage() == null ? "The private package could not be deleted." : e.getMessage());
            return false;
        }
    }

    void clearStatus() {
        if (busy) return;
        statusTitle = "";
        statusMessage = "";
        notifyObserver();
    }

    private void clearFailureMarker() {
        new File(files(), "turnip_failed").delete();
    }

    private String displayName(Uri uri) {
        try (Cursor cursor = activity.getContentResolver().query(
                uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) return cursor.getString(0);
        } catch (Exception ignored) {}
        String segment = uri.getLastPathSegment();
        return segment == null ? "Imported driver" : segment;
    }

    private void setStatus(String title, String message) {
        activity.runOnUiThread(() -> {
            if (closed) return;
            statusTitle = title;
            statusMessage = message;
            notifyObserver();
        });
    }

    private void notifyObserver() {
        Observer current = observer;
        if (current == null || closed) return;
        if (android.os.Looper.myLooper() == android.os.Looper.getMainLooper()) {
            current.onDriverStateChanged();
        } else {
            activity.runOnUiThread(() -> {
                Observer latest = observer;
                if (!closed && latest != null) latest.onDriverStateChanged();
            });
        }
    }

    boolean isBusy() { return busy; }
    String statusTitle() { return statusTitle; }
    String statusMessage() { return statusMessage; }
    String operationTitle() { return operationTitle; }
    long progressDone() { return progressDone; }
    long progressTotal() { return progressTotal; }

    void close() {
        closed = true;
        observer = null;
        DriverCatalog.Download download = activeDownload;
        if (download != null) download.cancel();
        worker.shutdownNow();
    }
}
