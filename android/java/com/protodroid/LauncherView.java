package com.protodroid;

import android.annotation.SuppressLint;
import android.app.ActivityManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Looper;
import android.os.SystemClock;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.util.Locale;

/**
 * Offline WebView presentation of the supplied launcher. Android remains authoritative for detection, preferences,
 * INI writes, updates, drivers, mods, the full touch editor, and the native diagnostic tour.
 */
final class LauncherView extends WebView {
    interface Listener {
        void onAllowFileAccess();
        void onCheckAgain();
        void onPlay();
        void onDrivers();
        void onMods();
        void onTouchLayout();
        void onExit();
        void onUpdateCheck();
        void onUpdate();
        void onUpdateConfirm();
        void onUpdateCancel();
        void onUpdateReleases();
        void onDiagnosticTourToggle();
    }

    static final class State {
        final boolean fileAccess, gameFound, checking, dll, uv;
        final String status, primaryPath, secondaryPath, alternateSharedPath, gamePath;

        State(boolean fileAccess, boolean gameFound, boolean checking, String status, String primaryPath,
              String secondaryPath, String alternateSharedPath, String gamePath, boolean dll, boolean uv) {
            this.fileAccess = fileAccess;
            this.gameFound = gameFound;
            this.checking = checking;
            this.status = status;
            this.primaryPath = primaryPath;
            this.secondaryPath = secondaryPath;
            this.alternateSharedPath = alternateSharedPath;
            this.gamePath = gamePath;
            this.dll = dll;
            this.uv = uv;
        }
    }

    private static final String[] CATEGORY_IDS = {"graphics", "video", "controls", "interface", "fixes"};
    private static final String[] CATEGORY_NAMES = {"Graphics", "Game video", "Controls", "Interface", "Engine fixes"};

    private final Listener listener;
    private final GameSettings gameSettings;
    private final DiagnosticTourCode logoTourCode = new DiagnosticTourCode();
    private State state = new State(false, false, true, "Checking game files", "", "", "", "", false, false);
    private UpdateManager.State updateState = UpdateManager.State.hidden();
    private String updateWarningVersion = "";
    private boolean tourArmed;
    private boolean pageReady;
    private boolean destroyed;
    private volatile String snapshotJson = "{}";

    @SuppressLint("SetJavaScriptEnabled")
    LauncherView(Context context, Listener listener) {
        super(context);
        this.listener = listener;
        gameSettings = new GameSettings(context);
        setBackgroundColor(Color.BLACK);
        setFocusable(true);
        setClickable(true);
        WebSettings settings = getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowContentAccess(false);
        settings.setAllowFileAccess(true);  // only the packaged file:///android_asset document is navigable
        settings.setAllowFileAccessFromFileURLs(false);
        settings.setAllowUniversalAccessFromFileURLs(false);
        settings.setBlockNetworkLoads(true);
        settings.setBuiltInZoomControls(false);
        settings.setDisplayZoomControls(false);
        settings.setSupportZoom(false);
        settings.setMediaPlaybackRequiresUserGesture(true);
        settings.setOffscreenPreRaster(true);
        WebView.setWebContentsDebuggingEnabled(false);
        setWebViewClient(new WebViewClient() {
            @Override public void onPageFinished(WebView view, String url) {
                pageReady = true;
                publish();
            }

            @Override public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                Uri uri = request.getUrl();
                return !("file".equals(uri.getScheme()) && "android_asset".equals(uri.getHost()));
            }
        });
        addJavascriptInterface(new HostBridge(), "ProtoDroidHost");
        snapshotJson = buildSnapshot();
        loadUrl("file:///android_asset/launcher/index.html");
    }

    GameSettings gameSettings() {
        return gameSettings;
    }

    void setState(State next) {
        state = next;
        publish();
    }

    void setUpdateState(UpdateManager.State next) {
        updateState = next == null ? UpdateManager.State.hidden() : next;
        publish();
    }

    void showUpdateWarning(String versionName) {
        updateWarningVersion = versionName == null ? "" : versionName;
        publish();
    }

    void hideUpdateWarning() {
        updateWarningVersion = "";
        publish();
    }

    void setTourArmed(boolean armed) {
        tourArmed = armed;
        if (!armed) logoTourCode.reset();
        publish();
    }

    void navigateBack() {
        if (!pageReady || destroyed) {
            listener.onExit();
            return;
        }
        evaluateJavascript("window.protodroidBack&&window.protodroidBack()", null);
    }

    void performFocusedAction() {
        if (pageReady && !destroyed) {
            evaluateJavascript("window.protodroidActivate&&window.protodroidActivate()", null);
        }
    }

    void notifyResume() {
        publish();
        if (pageReady && !destroyed) {
            evaluateJavascript("window.protodroidHostResumed&&window.protodroidHostResumed()", null);
        }
    }

    private void publish() {
        if (destroyed) return;
        if (Looper.myLooper() != Looper.getMainLooper()) {
            post(this::publish);
            return;
        }
        snapshotJson = buildSnapshot();
        if (pageReady) {
            evaluateJavascript("window.protodroidHostChanged&&window.protodroidHostChanged(" + snapshotJson + ")", null);
        }
    }

    private String buildSnapshot() {
        try {
            JSONObject root = new JSONObject();
            root.put("versionName", installedVersionName());
            root.put("tourArmed", tourArmed);
            root.put("motion", LauncherMotion.enabled() ? "full" : "off");

            JSONObject game = new JSONObject();
            game.put("access", state.fileAccess);
            game.put("gameFound", state.gameFound);
            game.put("checking", state.checking);
            game.put("status", state.status);
            game.put("gamePath", state.gamePath);
            game.put("sharedPath", state.primaryPath);
            game.put("appPath", state.secondaryPath);
            game.put("alternatePath", state.alternateSharedPath);
            game.put("dll", state.dll);
            game.put("uv", state.uv);
            root.put("game", game);

            JSONObject settings = new JSONObject();
            settings.put("preset", gameSettings.graphicsPreset());
            JSONArray categories = new JSONArray();
            for (int category = 0; category < GameSettings.CATEGORY_COUNT; ++category) {
                JSONObject group = new JSONObject();
                group.put("id", CATEGORY_IDS[category]);
                group.put("name", CATEGORY_NAMES[category]);
                JSONArray rows = new JSONArray();
                for (GameSettings.Setting setting : gameSettings.settings) {
                    if (GameSettings.categoryOf(setting) != category) continue;
                    JSONObject row = new JSONObject();
                    row.put("key", setting.key);
                    row.put("label", setting.label);
                    row.put("help", GameSettings.helpFor(setting));
                    row.put("index", setting.index);
                    JSONArray options = new JSONArray();
                    for (String option : setting.names) options.put(option);
                    row.put("options", options);
                    rows.put(row);
                }
                group.put("settings", rows);
                categories.put(group);
            }
            settings.put("categories", categories);
            root.put("settings", settings);

            JSONObject update = new JSONObject();
            update.put("status", updateStatus(updateState.status));
            update.put("visible", updateState.visible());
            update.put("canPress", updateState.canPress());
            update.put("versionName", updateState.versionName);
            update.put("progress", updateState.progress);
            update.put("downloadedBytes", updateState.downloadedBytes);
            update.put("totalBytes", updateState.totalBytes);
            update.put("detail", updateState.detail);
            update.put("requiresUninstall", updateState.requiresUninstall);
            update.put("warningVersion", updateWarningVersion);
            root.put("update", update);
            root.put("device", deviceSnapshot());
            return root.toString();
        } catch (JSONException e) {
            throw new IllegalStateException("Could not serialize launcher state", e);
        }
    }

    private JSONObject deviceSnapshot() throws JSONException {
        JSONObject device = new JSONObject();
        device.put("model", Build.MANUFACTURER + " " + Build.MODEL);
        device.put("os", "Android " + Build.VERSION.RELEASE + " · API " + Build.VERSION.SDK_INT);
        device.put("cpu", Build.HARDWARE == null || Build.HARDWARE.isEmpty() ? "Unknown" : Build.HARDWARE);
        boolean mali = new File("/dev/mali0").exists() || new File("/dev/mali").exists()
                || new File("/dev/mali_kbase").exists();
        boolean adreno = !mali && new File("/dev/kgsl-3d0").exists();
        device.put("gpu", DriverScreenPolicy.deviceFamily(mali, adreno));
        device.put("abi", String.join(", ", Build.SUPPORTED_ABIS));
        device.put("ram", totalMemory());
        device.put("screen", getResources().getDisplayMetrics().widthPixels + "×"
                + getResources().getDisplayMetrics().heightPixels);
        device.put("nativeBuild", Build.FINGERPRINT == null ? "Unknown" : Build.FINGERPRINT);
        return device;
    }

    private String totalMemory() {
        try {
            ActivityManager manager = (ActivityManager) getContext().getSystemService(Context.ACTIVITY_SERVICE);
            ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
            if (manager != null) {
                manager.getMemoryInfo(info);
                return String.format(Locale.ROOT, "%.1f GiB", info.totalMem / (1024.0 * 1024.0 * 1024.0));
            }
        } catch (RuntimeException ignored) {}
        return "Unknown";
    }

    private String installedVersionName() {
        try {
            PackageInfo info = getContext().getPackageManager().getPackageInfo(getContext().getPackageName(), 0);
            return info.versionName == null ? "Unknown" : info.versionName;
        } catch (Exception e) {
            return "Unknown";
        }
    }

    private static String updateStatus(int status) {
        switch (status) {
            case UpdateManager.State.CHECKING: return "checking";
            case UpdateManager.State.AVAILABLE: return "available";
            case UpdateManager.State.DOWNLOADING: return "downloading";
            case UpdateManager.State.VERIFYING: return "verifying";
            case UpdateManager.State.READY: return "ready";
            case UpdateManager.State.ERROR: return "error";
            default: return "hidden";
        }
    }

    private void onUi(Runnable action) {
        if (destroyed) return;
        if (Looper.myLooper() == Looper.getMainLooper()) action.run();
        else post(action);
    }

    private JSONObject readLog(String id) throws JSONException {
        File app = getContext().getExternalFilesDir(null);
        File shared = new File(Environment.getExternalStorageDirectory(), "Prototype");
        LauncherLog.Entry entry = LauncherLog.read(app, shared, id);
        JSONObject result = new JSONObject();
        result.put("name", entry.name);
        result.put("path", entry.path);
        result.put("content", entry.content);
        result.put("truncated", entry.truncated);
        result.put("error", entry.error);
        return result;
    }

    @Override public void destroy() {
        destroyed = true;
        removeJavascriptInterface("ProtoDroidHost");
        super.destroy();
    }

    private final class HostBridge {
        @JavascriptInterface public String getSnapshot() {
            return snapshotJson;
        }

        @JavascriptInterface public void play() {
            onUi(listener::onPlay);
        }

        @JavascriptInterface public void exit() {
            onUi(listener::onExit);
        }

        @JavascriptInterface public void requestFileAccess() {
            onUi(listener::onAllowFileAccess);
        }

        @JavascriptInterface public void checkGameData() {
            onUi(listener::onCheckAgain);
        }

        @JavascriptInterface public void openDrivers() {
            onUi(listener::onDrivers);
        }

        @JavascriptInterface public void openMods() {
            onUi(listener::onMods);
        }

        @JavascriptInterface public void openTouchLayout() {
            onUi(listener::onTouchLayout);
        }

        @JavascriptInterface public void setSetting(final String key, final int index) {
            onUi(() -> {
                for (int i = 0; i < gameSettings.settings.length; ++i) {
                    GameSettings.Setting setting = gameSettings.settings[i];
                    if (!setting.key.equals(key)) continue;
                    if (setting.key.equals("touch_layout")) listener.onTouchLayout();
                    else if (index >= 0 && index < setting.names.length) gameSettings.setIndex(i, index);
                    publish();
                    return;
                }
            });
        }

        @JavascriptInterface public void applyPreset(final int preset) {
            onUi(() -> {
                gameSettings.applyPreset(preset);
                publish();
            });
        }

        @JavascriptInterface public void resetSettings() {
            onUi(() -> {
                gameSettings.resetToDefaults();
                publish();
            });
        }

        @JavascriptInterface public void checkUpdates() {
            onUi(listener::onUpdateCheck);
        }

        @JavascriptInterface public void updateNow() {
            onUi(listener::onUpdate);
        }

        @JavascriptInterface public void openReleases() {
            onUi(listener::onUpdateReleases);
        }

        @JavascriptInterface public void confirmUpdate() {
            onUi(listener::onUpdateConfirm);
        }

        @JavascriptInterface public void dismissUpdateWarning() {
            onUi(listener::onUpdateCancel);
        }

        @JavascriptInterface public void logoTap() {
            onUi(() -> {
                if (logoTourCode.onLogoTap(SystemClock.uptimeMillis())) listener.onDiagnosticTourToggle();
            });
        }

        @JavascriptInterface public void cancelDiagnosticTour() {
            onUi(() -> {
                if (tourArmed) listener.onDiagnosticTourToggle();
            });
        }

        @JavascriptInterface public String readLog(String id) {
            try {
                return LauncherView.this.readLog(id).toString();
            } catch (JSONException e) {
                return "{}";
            }
        }

        @JavascriptInterface public void copyText(final String text) {
            onUi(() -> {
                ClipboardManager clipboard = (ClipboardManager) getContext().getSystemService(Context.CLIPBOARD_SERVICE);
                if (clipboard != null) clipboard.setPrimaryClip(ClipData.newPlainText("ProtoDroid log", text));
            });
        }
    }
}
