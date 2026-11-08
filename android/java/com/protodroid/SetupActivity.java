package com.protodroid;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.system.ErrnoException;
import android.system.Os;
import android.view.KeyEvent;
import android.widget.Toast;

import java.io.File;

/**
 * Launcher: finds the game files, provides Play and Settings, or explains where to put them. The game can live in the
 * app's own folder (Android/data/com.protodroid/files/game, which file managers often cannot open on Android 11+)
 * or in /sdcard/Prototype, which needs "all files access" (storage permission before Android 11).
 * Also supports /sdcard/Prototype/game subfolder.
 */
public class SetupActivity extends Activity {
    private static final int REQUEST_STORAGE = 1;

    private SetupView setupView;
    private final DiagnosticTourCode tourCode = new DiagnosticTourCode();
    private boolean tourArmed;
    private DriverView driverView;
    private UpdateManager updateManager;
    private DriverManager driverManager;
    private File foundGame;
    private boolean foundInApp;
    private boolean missingUv;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ImmersiveMode.apply(this);
        updateManager = new UpdateManager(this, new UpdateManager.Callback() {
            @Override
            public void onStateChanged(UpdateManager.State state) {
                if (setupView != null) setupView.setUpdateState(state);
            }

            @Override
            public void onUninstallRequired(String versionName) {
                if (setupView != null) setupView.showUpdateWarning(versionName);
            }
        });
        driverManager = new DriverManager(this);
    }

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
                public void onDrivers() {
                    showDrivers();
                }

                @Override
                public void onMods() {
                    startActivity(new Intent(SetupActivity.this, ModsActivity.class));
                    LauncherMotion.transition(SetupActivity.this);
                }

                @Override
                public void onTouchLayout() {
                    startActivity(new Intent(SetupActivity.this, TouchLayoutActivity.class));
                    LauncherMotion.transition(SetupActivity.this);
                }

                @Override
                public void onDiagnostics() {
                    startActivity(new Intent(SetupActivity.this, DiagnosticsActivity.class));
                    LauncherMotion.transition(SetupActivity.this);
                }

                @Override
                public void onExit() {
                    finish();
                }

                @Override
                public void onUpdate() {
                    updateManager.onUpdatePressed();
                }

                @Override
                public void onUpdateConfirm() {
                    setupView.hideUpdateWarning();
                    updateManager.confirmUninstall();
                }

                @Override
                public void onUpdateCancel() {
                    setupView.hideUpdateWarning();
                }

                @Override
                public void onUpdateReleases() {
                    updateManager.onReleasesPressed();
                }

                @Override
                public void onDiagnosticTourToggle() {
                    toggleDiagnosticTour();
                }
            });
            setContentView(setupView);
        }
        setupView.setUpdateState(updateManager.currentState());
        File sharedSub = new File(shared, "game");
        boolean access = hasSharedAccess();
        String appPath = app != null ? app.getAbsolutePath()
                : "/storage/emulated/0/Android/data/" + getPackageName() + "/files/game";
        String status = foundGame != null ? "Ready to play"
                : access ? "Game files not found. Tap Play to check again" : "Waiting for file access";
        if (missingUv) {
            status = "Missing proto_uv.dat; copy it from your PC game";
        } else if (tourArmed) {
            status = foundGame != null ? "Tour: automatic tests, then app closes" : "Tour armed; game files required";
        }
        setupView.setTourArmed(tourArmed);
        setupView.setState(new SetupView.State(
                access,
                foundGame != null,
                false,
                status,
                shared.getAbsolutePath(),
                appPath,
                sharedSub.getAbsolutePath()));
    }

    private void showDrivers() {
        if (driverView != null || setupView == null) return;
        driverView = new DriverView(this, setupView.gameSettings(), driverManager, this::closeDrivers);
        setContentView(driverView);
        if (LauncherMotion.enabled()) {
            driverView.setAlpha(0f);
            driverView.animate().alpha(1f).setDuration(220).start();
        }
    }

    private void closeDrivers() {
        if (driverView == null) return;
        driverManager.setObserver(null);
        driverView = null;
        setContentView(setupView);
        if (LauncherMotion.enabled()) {
            setupView.setAlpha(0f);
            setupView.animate().alpha(1f).setDuration(220).start();
        }
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

    @Override
    protected void onResume() {
        super.onResume();
        ImmersiveMode.apply(this);
        check();  // also after coming back from the permission screen
        updateManager.onResume();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) ImmersiveMode.apply(this);
    }

    @Override
    protected void onPause() {
        updateManager.onPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        driverManager.close();
        updateManager.close();
        super.onDestroy();
    }

    private void check() {
        File app = appGame();
        if (app != null) app.mkdirs();
        File shared = sharedGame();
        File sharedSub = sharedGameSub();
        boolean sharedAccess = hasSharedAccess();
        if (sharedAccess) {
            shared.mkdirs();
            // Also ensure subfolder exists check doesn't create it yet, but ensure parent exists
        }
        // Detection is automatic; launching is deliberate so Settings remains available before every game start.
        File[] candidates = sharedAccess ? new File[]{app, shared, sharedSub} : new File[]{app};
        foundGame = GameFiles.firstComplete(candidates);
        foundInApp = foundGame != null && foundGame.equals(app);
        missingUv = foundGame == null && GameFiles.anyMissingUv(candidates);
        showSetup(app, shared);
    }

    private void play() {
        check();  // also refreshes after files were copied while this screen remained open
        if (foundGame == null) return;
        if (tourArmed) {
            launch(foundInApp ? null : foundGame, true, null);
            return;
        }
        if (needsShaderPrecompile()) {
            offerShaderPrecompile();
            return;
        }
        launch(foundInApp ? null : foundGame, false, null);
    }

    /**
     * The one-time shader precompile: the automated tour builds every graphics pipeline in its own isolated run
     * (and its own process), then this screen relaunches normal Play, which seeds its cache from the tour's.
     * Offered only while no populated cache exists anywhere; the native side writes shader-precompile.done once
     * seeding has happened. A tour cache as small as a few hundred bytes is real work (a populated DXVK state
     * cache header plus entries — the fixed-scenario tour legitimately produces only ~19 pipelines), and the
     * shared /Prototype backup is what normal play restores automatically, so its presence also skips the offer.
     */
    private boolean needsShaderPrecompile() {
        File app = getExternalFilesDir(null);
        if (app == null) return false;
        if (new File(app, "shader-precompile.done").exists()) return false;
        if (fileAtLeast(new File(app, "diagnostic-tour-cache/prototypeenginef.dll.dxvk-cache"), 128)) return false;
        if (fileAtLeast(new File("/storage/emulated/0/Prototype/prototypeenginef.dll.dxvk-cache"), 4096)) return false;
        return !fileAtLeast(new File(app, "prototypeenginef.dll.dxvk-cache"), 4096);
    }

    private static boolean fileAtLeast(File file, long bytes) {
        return file.isFile() && file.length() >= bytes;
    }

    private void offerShaderPrecompile() {
        PrecompileDialog.show(this, new PrecompileDialog.Actions() {
            @Override
            public void loadAllShadersNow() {
                launchPrecompile(foundInApp ? null : foundGame);
            }

            @Override
            public void playWithoutOptimizing() {
                launch(foundInApp ? null : foundGame, false, null);
            }

            @Override
            public void dontAskAgain() {
                try {
                    //noinspection ResultOfMethodCallIgnored
                    new File(getExternalFilesDir(null), "shader-precompile.done").createNewFile();
                } catch (java.io.IOException ignored) {
                }
                launch(foundInApp ? null : foundGame, false, null);
            }
        });
    }

    /**
     * Starts the precompile tour in its own process and waits for its result. The tour's native side ends the
     * run with an immediate process exit, so the relaunch into normal Play must be owned from here — this
     * activity stays alive behind the tour screen and starts the game as soon as result.txt says "complete".
     */
    private void launchPrecompile(File gameDir) {
        if (!applyLaunchEnvironment(gameDir, true, "precompile")) return;
        startActivity(new Intent(this, TourActivity.class));
        final File resultFile = new File("/storage/emulated/0/Prototype/diagnostic-tour/logs/result.txt");
        final android.os.Handler handler = new android.os.Handler(getMainLooper());
        final Runnable[] poll = new Runnable[1];
        poll[0] = new Runnable() {
            private int ticks;

            @Override
            public void run() {
                String verdict = firstResultLine(resultFile);
                if (verdict == null && ticks++ < 20 * 60) {  // the tour can legitimately run for minutes
                    handler.postDelayed(this, 1000);
                    return;
                }
                if ("complete".equals(verdict)) {
                    Toast.makeText(SetupActivity.this, "Shaders optimized — starting game", Toast.LENGTH_LONG).show();
                    launch(gameDir, false, null);
                } else if (verdict != null) {
                    Toast.makeText(SetupActivity.this,
                            "Shader optimization did not finish; see Diagnostics.", Toast.LENGTH_LONG).show();
                }
            }
        };
        new Thread(() -> handler.post(poll[0]), "precompile-watch").start();
    }

    private static String firstResultLine(File resultFile) {
        try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader(resultFile))) {
            for (String line; (line = reader.readLine()) != null;) {
                line = line.trim();
                if (line.startsWith("result=")) return line.substring("result=".length()).trim();
            }
        } catch (java.io.IOException ignored) {
            // not written yet
        }
        return null;
    }

    private void launch(File gameDir, boolean tour) {
        launch(gameDir, tour, null);
    }

    private boolean applyLaunchEnvironment(File gameDir, boolean tour, String purpose) {
        try {
            if (tour) Os.setenv("PROTODROID_DIAGNOSTIC_TOUR", "1", true);
            else Os.unsetenv("PROTODROID_DIAGNOSTIC_TOUR");
            if (tour && purpose != null) Os.setenv("PROTODROID_TOUR_PURPOSE", purpose, true);
            else Os.unsetenv("PROTODROID_TOUR_PURPOSE");
        } catch (ErrnoException e) {
            Toast.makeText(this, tour
                            ? "Could not arm the diagnostic tour; normal Play was not substituted."
                            : "Could not clear diagnostic-tour mode; Play was cancelled to protect your saves.",
                    Toast.LENGTH_LONG).show();
            return false;
        }
        try {
            if (gameDir == null) Os.unsetenv("PROTODROID_GAME");
            else Os.setenv("PROTODROID_GAME", gameDir.getAbsolutePath(), true);
        } catch (ErrnoException ignored) {
            // the native side falls back to the app folder
        }
        File driver = driverManager.driverForLaunch();
        DriverRepository.Entry selected = driverManager.selected();
        try {
            if (driver == null) {
                Os.unsetenv("PROTODROID_CUSTOM_DRIVER");
                Os.unsetenv("PROTODROID_CUSTOM_DRIVER_BACKEND");
            } else {
                Os.setenv("PROTODROID_CUSTOM_DRIVER", driver.getAbsolutePath(), true);
                Os.setenv("PROTODROID_CUSTOM_DRIVER_BACKEND",
                        selected == null ? DriverCatalog.TURNIP : selected.installed.backend, true);
            }
        } catch (ErrnoException ignored) {
            // the bundled drivers are used
        }
        return true;
    }

    private void launch(File gameDir, boolean tour, String purpose) {
        if (!applyLaunchEnvironment(gameDir, tour, purpose)) return;
        startActivity(new Intent(this, ProtoActivity.class));
        finish();
    }

    private void toggleDiagnosticTour() {
        if (driverView != null || setupView == null) return;
        tourArmed = !tourArmed;
        setupView.setTourArmed(tourArmed);
        check();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (driverView == null && event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
            int unicode = event.getUnicodeChar();
            if (unicode > 0 && tourCode.onLetter((char) unicode)) {
                toggleDiagnosticTour();
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
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
        super.onRequestPermissionsResult(code, permissions, results);
        updateManager.onRequestPermissionsResult(code, results);
        check();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (driverManager.onActivityResult(requestCode, resultCode, data)) return;
        updateManager.onActivityResult(requestCode, resultCode);
    }

    @Override
    public void onBackPressed() {
        if (driverView != null) {
            closeDrivers();
            return;
        }
        if (setupView != null && setupView.closeUpdateWarning()) return;
        if (setupView != null && setupView.closeSettings()) return;
        super.onBackPressed();
    }

}
