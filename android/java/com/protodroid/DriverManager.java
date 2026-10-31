package com.protodroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
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
import java.util.Locale;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Launcher UI and lifecycle for validated custom Vulkan drivers. No global/system driver is modified. */
final class DriverManager {
    interface Listener {
        void onDriverSelectionChanged(boolean custom);
    }

    static final int REQUEST_IMPORT = 410;
    private static final String LEGACY_IGNORED = "legacy_driver_ignored";

    private final Activity activity;
    private final Listener listener;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final SharedPreferences preferences;
    private volatile DriverCatalog.Download activeDownload;
    private ProgressDialog progress;
    private boolean busy;
    private boolean closed;

    DriverManager(Activity activity, Listener listener) {
        this.activity = activity;
        this.listener = listener;
        preferences = activity.getSharedPreferences("controls", Activity.MODE_PRIVATE);
    }

    private File root() {
        return new File(activity.getFilesDir(), "driver");
    }

    /** Uses the managed package first, then migrates the previous external/private single-.so path once. */
    File driverForLaunch() {
        DriverPackage.Installed installed = DriverPackage.read(root());
        if (installed != null) return installed.libraryFile();

        File oldPrivate = new File(root(), "libvulkan_custom.so");
        if (oldPrivate.isFile()) {
            File migrated = migrateLegacy(oldPrivate);
            if (migrated != null) return migrated;
        }
        if (!preferences.getBoolean(LEGACY_IGNORED, false)) {
            File external = activity.getExternalFilesDir(null);
            File[] found = external == null ? null : new File(external, "driver").listFiles(
                    (dir, name) -> name.endsWith(".so"));
            if (found != null && found.length > 0) {
                Arrays.sort(found);
                File migrated = migrateLegacy(found[0]);
                if (migrated != null) return migrated;
            }
        }
        return noDriverForLaunch();
    }

    private File noDriverForLaunch() {
        if (preferences.getInt("vulkan_driver", 0) == 3) {
            listener.onDriverSelectionChanged(false);
            Toast.makeText(activity, "No valid custom driver is installed; Auto was restored.",
                    Toast.LENGTH_LONG).show();
        }
        return null;
    }

    private File migrateLegacy(File source) {
        try (InputStream input = new FileInputStream(source)) {
            DriverPackage.Installed installed = DriverPackage.install(input, source.getName(), root(), Build.VERSION.SDK_INT);
            // Builds before the driver picker had no vulkan_driver preference: preserve their old custom-driver behavior.
            if (!preferences.contains("vulkan_driver")) listener.onDriverSelectionChanged(true);
            return installed.libraryFile();
        } catch (Exception e) {
            android.util.Log.w("ProtoDroid", "Could not migrate the previous custom driver", e);
            return null;
        }
    }

    void show() {
        if (busy || closed) return;
        DriverPackage.Installed installed = DriverPackage.read(root());
        String status = installed == null
                ? "No custom driver is installed. Auto uses ProtoDroid's bundled selection; System uses the phone's driver."
                : "Installed:\n" + installed.title() + "\n\nCustom is tried in a separate probe at the next Play. "
                        + "If it is incompatible, ProtoDroid logs the reason and uses the system driver.";
        AlertDialog.Builder dialog = new AlertDialog.Builder(activity)
                .setTitle("GPU drivers")
                .setMessage(status + "\n\nDrivers are native code. Install only packages you trust; "
                        + "this never replaces the phone's global driver.")
                .setPositiveButton("Import ZIP / .so", (d, which) -> confirmImport())
                .setNeutralButton("Download", (d, which) -> showDownloads());
        if (installed != null) dialog.setNegativeButton("Remove custom", (d, which) -> confirmRemove());
        else dialog.setNegativeButton("Close", null);
        dialog.show();
    }

    private void confirmImport() {
        if (Build.VERSION.SDK_INT < 29) {
            showMessage("Android version too old", "ProtoDroid's isolated custom-driver loader needs Android 10 "
                    + "(API 29) or newer. Auto and System remain available.");
            return;
        }
        new AlertDialog.Builder(activity)
                .setTitle("Import a custom driver")
                .setMessage("Choose an AdrenoTools-style ZIP or a raw Android/Bionic ARM64 .so. "
                        + "Linux/glibc packages, x86 libraries, damaged archives, and wrong-ABI drivers are rejected. "
                        + "The previous installation is kept if validation fails.")
                .setPositiveButton("Choose file", (d, which) -> openPicker())
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void openPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/octet-stream"});
        activity.startActivityForResult(intent, REQUEST_IMPORT);
    }

    private void showDownloads() {
        String[] names = new String[DriverCatalog.ENTRIES.length];
        for (int i = 0; i < names.length; ++i) names[i] = DriverCatalog.ENTRIES[i].title;
        new AlertDialog.Builder(activity)
                .setTitle("Pinned driver downloads")
                .setItems(names, (dialog, which) -> confirmDownload(DriverCatalog.ENTRIES[which]))
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void confirmDownload(DriverCatalog.Entry entry) {
        if (Build.VERSION.SDK_INT < entry.minApi) {
            showMessage("Android version too old", entry.title + " needs Android API " + entry.minApi + " or newer.");
            return;
        }
        new AlertDialog.Builder(activity)
                .setTitle(entry.title)
                .setMessage(entry.description + "\n\nPinned package: "
                        + String.format(Locale.ROOT, "%.1f MiB", entry.size / 1048576.0)
                        + "\nThe download installs only after its exact SHA-256 matches. No website is opened.")
                .setPositiveButton("Download & install", (d, which) -> startDownload(entry))
                .setNegativeButton("Cancel", null)
                .show();
    }

    boolean onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_IMPORT) return false;
        if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
            startImport(data.getData());
        }
        return true;
    }

    private void startImport(Uri uri) {
        if (!beginOperation("Importing driver", false)) return;
        String name = displayName(uri);
        worker.execute(() -> {
            try {
                ContentResolver resolver = activity.getContentResolver();
                try (InputStream input = resolver.openInputStream(uri)) {
                    if (input == null) throw new IOException("The selected file could not be opened");
                    DriverPackage.Installed installed = DriverPackage.install(input, name, root(), Build.VERSION.SDK_INT);
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
        if (!beginOperation("Downloading driver", true)) return;
        File partial = new File(activity.getCacheDir(), "driver-download.part");
        partial.delete();
        DriverCatalog.Download download = new DriverCatalog.Download();
        activeDownload = download;
        worker.execute(() -> {
            try {
                download.run(entry, partial, (done, total) -> activity.runOnUiThread(() -> {
                    if (progress != null) progress.setProgress((int) (done * 1000 / total));
                }));
                if (download.isCancelled()) throw new CancellationException();
                activeDownload = null;
                activity.runOnUiThread(() -> {
                    if (progress != null) {
                        progress.setMessage("Verifying and installing…");
                        progress.getButton(ProgressDialog.BUTTON_NEGATIVE).setEnabled(false);
                    }
                });
                DriverPackage.Installed installed;
                try (InputStream input = new FileInputStream(partial)) {
                    installed = DriverPackage.install(input, entry.title + ".zip", root(), Build.VERSION.SDK_INT);
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

    private boolean beginOperation(String title, boolean determinate) {
        if (busy || closed) return false;
        busy = true;
        progress = new ProgressDialog(activity);
        progress.setTitle(title);
        progress.setMessage(determinate ? "Starting…" : "Checking the package. Keep ProtoDroid open.");
        progress.setIndeterminate(!determinate);
        if (determinate) {
            progress.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
            progress.setMax(1000);
            progress.setProgress(0);
        }
        progress.setCancelable(false);
        if (determinate) {
            progress.setButton(ProgressDialog.BUTTON_NEGATIVE, "Cancel", (dialog, which) -> cancelOperation());
        }
        progress.show();
        return true;
    }

    private void cancelOperation() {
        DriverCatalog.Download download = activeDownload;
        if (download != null) download.cancel();
        // ZIP import is deliberately quick and bounded; it finishes or fails without replacing the old driver.
    }

    private void installSucceeded(DriverPackage.Installed installed) {
        preferences.edit().putBoolean(LEGACY_IGNORED, false).apply();
        clearFailureMarker();
        activity.runOnUiThread(() -> {
            if (closed) return;
            listener.onDriverSelectionChanged(true);
            showMessage("Driver installed", installed.title()
                    + "\n\nCustom is now selected. Tap Play to run the isolated driver probe.");
        });
    }

    private void operationFailed(String title, Exception error) {
        activity.runOnUiThread(() -> {
            if (closed) return;
            if (error instanceof CancellationException) {
                showMessage("Download cancelled", "The previous driver installation was not changed.");
                return;
            }
            String detail = error.getMessage();
            if (detail == null || detail.trim().isEmpty()) detail = error.getClass().getSimpleName();
            showMessage(title, detail + "\n\nThe previous driver installation was not changed.");
        });
    }

    private void endOperation() {
        activity.runOnUiThread(() -> {
            busy = false;
            if (progress != null) {
                progress.dismiss();
                progress = null;
            }
        });
    }

    private void confirmRemove() {
        new AlertDialog.Builder(activity)
                .setTitle("Remove custom driver?")
                .setMessage("This removes only ProtoDroid's private imported copy and returns the setting to Auto. "
                        + "Your original ZIP/.so, game files, saves, and the phone's system driver are not changed.")
                .setPositiveButton("Remove", (dialog, which) -> {
                    try {
                        DriverPackage.deleteTree(root());
                        preferences.edit().putBoolean(LEGACY_IGNORED, true).apply();
                        clearFailureMarker();
                        listener.onDriverSelectionChanged(false);
                        showMessage("Custom driver removed", "Auto will be used on the next Play.");
                    } catch (IOException e) {
                        showMessage("Could not remove driver", e.getMessage());
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void clearFailureMarker() {
        new File(activity.getFilesDir(), "turnip_failed").delete();
    }

    private String displayName(Uri uri) {
        try (Cursor cursor = activity.getContentResolver().query(
                uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) return cursor.getString(0);
        } catch (Exception ignored) {}
        String segment = uri.getLastPathSegment();
        return segment == null ? "Imported driver" : segment;
    }

    private void showMessage(String title, String message) {
        if (closed) return;
        new AlertDialog.Builder(activity).setTitle(title).setMessage(message).setPositiveButton("OK", null).show();
    }

    void close() {
        closed = true;
        DriverCatalog.Download download = activeDownload;
        if (download != null) download.cancel();
        worker.shutdownNow();
        if (progress != null) {
            progress.dismiss();
            progress = null;
        }
    }
}
