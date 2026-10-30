package com.protodroid;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.system.ErrnoException;
import android.system.Os;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Launcher: finds the game files, provides Play and Settings, or explains where to put them. The game can live in the
 * app's own folder (Android/data/com.protodroid/files/game, which file managers often cannot open on Android 11+)
 * or in /sdcard/Prototype, which needs "all files access" (storage permission before Android 11).
 * Also supports /sdcard/Prototype/game subfolder.
 */
public class SetupActivity extends Activity {
    private static final String DLL = "prototypeenginef.dll";
    private static final int REQUEST_STORAGE = 1;

    private SetupView setupView;
    private File foundGame;
    private boolean foundInApp;

    private void showSetup(File app, File shared) {
        if (setupView == null) {
            // Keep the automatic found-game launch path as light as before: artwork is decoded only when help is needed.
            setupView = new SetupView(this, new SetupView.Listener() {
                @Override
                public void onAllowFileAccess() {
                    requestAccess();
                }

                @Override
                public void onCheckAgain() {
                    check();
                }

                @Override
                public void onPlay() {
                    play();
                }

                @Override
                public void onExit() {
                    finish();
                }
            });
            setContentView(setupView);
        }
        File sharedSub = new File(shared, "game");
        boolean access = hasSharedAccess();
        String appPath = app != null ? app.getAbsolutePath()
                : "/storage/emulated/0/Android/data/" + getPackageName() + "/files/game";
        String status = foundGame != null ? "Ready to play"
                : access ? "Game files not found. Tap Play to check again" : "Waiting for file access";
        setupView.setState(new SetupView.State(
                access,
                foundGame != null,
                false,
                status,
                shared.getAbsolutePath(),
                appPath,
                sharedSub.getAbsolutePath()));
    }

    private File appGame() {
        File base = getExternalFilesDir(null);  // also creates Android/data/<package>/files
        return base == null ? null : new File(base, "game");
    }

    private static File sharedGame() {
        return new File(Environment.getExternalStorageDirectory(), "Prototype");
    }

    private static File sharedGameSub() {
        return new File(sharedGame(), "game");
    }

    private boolean hasSharedAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private static boolean hasGame(File dir) {
        return dir != null && new File(dir, DLL).canRead();
    }

    @Override
    protected void onResume() {
        super.onResume();
        check();  // also after coming back from the permission screen
    }

    private void check() {
        File app = appGame();
        if (app != null) app.mkdirs();
        File shared = sharedGame();
        File sharedSub = sharedGameSub();
        if (hasSharedAccess()) {
            shared.mkdirs();
            // Also ensure subfolder exists check doesn't create it yet, but ensure parent exists
        }
        // Detection is automatic; launching is deliberate so Settings remains available before every game start.
        foundGame = null;
        foundInApp = false;
        if (hasGame(app)) {
            foundGame = app;
            foundInApp = true;
        } else if (hasSharedAccess() && hasGame(shared)) {
            foundGame = shared;
        } else if (hasSharedAccess() && hasGame(sharedSub)) {
            foundGame = sharedSub;
        }
        showSetup(app, shared);
    }

    private void play() {
        check();  // also refreshes after files were copied while this screen remained open
        if (foundGame != null) launch(foundInApp ? null : foundGame);
    }

    private void launch(File gameDir) {
        try {
            if (gameDir == null) Os.unsetenv("PROTODROID_GAME");
            else Os.setenv("PROTODROID_GAME", gameDir.getAbsolutePath(), true);
        } catch (ErrnoException ignored) {
            // the native side falls back to the app folder
        }
        File driver = installCustomDriver();
        try {
            if (driver == null) Os.unsetenv("PROTODROID_CUSTOM_DRIVER");
            else Os.setenv("PROTODROID_CUSTOM_DRIVER", driver.getAbsolutePath(), true);
        } catch (ErrnoException ignored) {
            // the bundled drivers are used
        }
        startActivity(new Intent(this, ProtoActivity.class));
        finish();
    }

    /**
     * A user-supplied Vulkan driver (an Adreno driver .so, e.g. a Turnip build made for emulators) in
     * Android/data/com.protodroid/files/driver/: copied into the app's private storage, where the linker may load
     * it from (not from shared storage). Returns the private copy, or null when there is none (the copy is removed
     * when the user removes the driver).
     */
    private File installCustomDriver() {
        File base = getExternalFilesDir(null);
        File[] found = base == null ? null : new File(base, "driver").listFiles((d, name) -> name.endsWith(".so"));
        File dest = new File(new File(getFilesDir(), "driver"), "libvulkan_custom.so");
        if (found == null || found.length == 0) {
            dest.delete();
            return null;
        }
        File src = found[0];
        if (dest.exists() && dest.length() == src.length() && dest.lastModified() >= src.lastModified()) return dest;
        dest.getParentFile().mkdirs();
        try (InputStream in = new FileInputStream(src); OutputStream out = new FileOutputStream(dest)) {
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0;) out.write(buf, 0, n);
        } catch (IOException e) {
            dest.delete();
            return null;
        }
        return dest;
    }

    private void requestAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE}, REQUEST_STORAGE);
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        check();
    }

    @Override
    public void onBackPressed() {
        if (setupView != null && setupView.closeSettings()) return;
        super.onBackPressed();
    }

}
