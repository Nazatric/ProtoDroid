package com.protodroid;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Native About/device-information and bounded read-only log views for the original Android launcher. */
public final class DiagnosticsActivity extends Activity {
    private static final int BONE = 0xffece8e1;
    private static final int DIM = 0xffa9a49b;
    private static final int RED = 0xffff2b2b;
    private static final String[] LOG_IDS = {"native", "prev", "dxvk", "tour", "journal", "result"};
    private static final String[] LOG_LABELS = {"NATIVE", "PREVIOUS", "DXVK", "TOUR", "JOURNAL", "RESULT"};

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final List<Button> logButtons = new ArrayList<>();
    private Typeface regular;
    private Typeface bold;
    private View aboutPage;
    private View logsPage;
    private Button aboutTab;
    private Button logsTab;
    private TextView logPath;
    private TextView logMeta;
    private TextView logContent;
    private String selectedLog = "native";
    private LauncherLog.Entry currentLog;
    private int loadGeneration;
    private boolean logsVisible;
    private boolean closed;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window window = getWindow();
        ImmersiveMode.apply(this);
        window.setStatusBarColor(Color.BLACK);
        window.setNavigationBarColor(Color.BLACK);
        regular = Typeface.createFromAsset(getAssets(), "setup/fonts/barlow_condensed_medium.ttf");
        bold = Typeface.createFromAsset(getAssets(), "setup/fonts/barlow_condensed_bold.ttf");

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff080708);
        root.setPadding(dp(18), dp(12), dp(18), dp(12));

        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text("PROTODROID  /  DIAGNOSTICS", 22, BONE, true);
        header.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        Button back = button("BACK", this::finish);
        header.addView(back, new LinearLayout.LayoutParams(dp(104), dp(44)));
        root.addView(header, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(52)));

        LinearLayout pages = new LinearLayout(this);
        aboutTab = button("ABOUT / DEVICE", () -> showLogs(false));
        logsTab = button("LOGS", () -> showLogs(true));
        pages.addView(aboutTab, new LinearLayout.LayoutParams(0, dp(44), 1f));
        pages.addView(logsTab, new LinearLayout.LayoutParams(0, dp(44), 1f));
        root.addView(pages, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(50)));

        FrameLayout content = new FrameLayout(this);
        aboutPage = createAboutPage();
        logsPage = createLogsPage();
        content.addView(aboutPage);
        content.addView(logsPage);
        root.addView(content, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);
        showLogs("logs".equals(getIntent().getStringExtra("page")));
        LauncherMotion.transition(this);
    }

    private View createAboutPage() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout rows = new LinearLayout(this);
        rows.setOrientation(LinearLayout.VERTICAL);
        rows.setPadding(dp(8), dp(14), dp(8), dp(20));
        scroll.addView(rows);

        rows.addView(text("ABOUT", 28, BONE, true));
        addRow(rows, "APP VERSION", installedVersion());
        addRow(rows, "PACKAGE", getPackageName());
        addRow(rows, "DEVICE", safe(Build.MANUFACTURER) + " " + safe(Build.MODEL));
        addRow(rows, "ANDROID", "Android " + safe(Build.VERSION.RELEASE) + "  /  API " + Build.VERSION.SDK_INT);
        addRow(rows, "HARDWARE", safe(Build.HARDWARE));
        boolean mali = new File("/dev/mali0").exists() || new File("/dev/mali").exists()
                || new File("/dev/mali_kbase").exists();
        boolean adreno = !mali && new File("/dev/kgsl-3d0").exists();
        addRow(rows, "GPU FAMILY HINT", DriverScreenPolicy.deviceFamily(mali, adreno));
        addRow(rows, "SUPPORTED ABIS", String.join(", ", Build.SUPPORTED_ABIS));
        addRow(rows, "TOTAL MEMORY", totalMemory());
        addRow(rows, "WINDOW PIXELS", getResources().getDisplayMetrics().widthPixels + " × "
                + getResources().getDisplayMetrics().heightPixels);
        addRow(rows, "ANDROID BUILD", safe(Build.FINGERPRINT));
        File app = getExternalFilesDir(null);
        addRow(rows, "APP-SPECIFIC DATA", app == null ? "Unavailable" : app.getAbsolutePath());
        addRow(rows, "SHARED SAVES", sharedPrototype().getAbsolutePath());

        TextView note = text("Device fields are reported by Android and device nodes. They do not enable graphics features; "
                + "native Vulkan and driver probes make capability decisions. Logs are read-only and updates remain on the main launcher.",
                15, DIM, false);
        note.setPadding(0, dp(18), 0, dp(12));
        rows.addView(note);
        Button openLogs = button("OPEN LOGS", () -> showLogs(true));
        rows.addView(openLogs, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48)));
        return scroll;
    }

    private View createLogsPage() {
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(4), dp(8), dp(4), 0);

        HorizontalScrollView tabScroll = new HorizontalScrollView(this);
        tabScroll.setHorizontalScrollBarEnabled(false);
        LinearLayout tabs = new LinearLayout(this);
        for (int i = 0; i < LOG_IDS.length; ++i) {
            final String id = LOG_IDS[i];
            Button tab = button(LOG_LABELS[i], () -> selectLog(id));
            logButtons.add(tab);
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(112), dp(42));
            params.setMargins(0, 0, dp(6), 0);
            tabs.addView(tab, params);
        }
        tabScroll.addView(tabs);
        page.addView(tabScroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48)));

        logPath = text("", 13, DIM, false);
        logPath.setPadding(dp(4), dp(4), dp(4), 0);
        page.addView(logPath);
        logMeta = text("", 13, DIM, false);
        logMeta.setPadding(dp(4), 0, dp(4), dp(6));
        page.addView(logMeta);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        logContent = text("Select a log.", 14, BONE, false);
        logContent.setTypeface(Typeface.MONOSPACE);
        logContent.setTextIsSelectable(true);
        logContent.setPadding(dp(12), dp(12), dp(12), dp(12));
        logContent.setBackgroundColor(0xff111013);
        scroll.addView(logContent, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        page.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout actions = new LinearLayout(this);
        Button refresh = button("REFRESH", this::loadLog);
        Button copy = button("COPY LOG TAIL", this::copyLog);
        actions.addView(refresh, new LinearLayout.LayoutParams(0, dp(46), 1f));
        actions.addView(copy, new LinearLayout.LayoutParams(0, dp(46), 1f));
        page.addView(actions, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(52)));
        return page;
    }

    private void showLogs(boolean show) {
        logsVisible = show;
        aboutPage.setVisibility(show ? View.GONE : View.VISIBLE);
        logsPage.setVisibility(show ? View.VISIBLE : View.GONE);
        styleButton(aboutTab, !show);
        styleButton(logsTab, show);
        if (show && currentLog == null) loadLog();
    }

    private void selectLog(String id) {
        if (!id.equals(selectedLog)) {
            selectedLog = id;
            currentLog = null;
        }
        updateLogTabs();
        loadLog();
    }

    private void updateLogTabs() {
        for (int i = 0; i < logButtons.size(); ++i) styleButton(logButtons.get(i), LOG_IDS[i].equals(selectedLog));
    }

    private void loadLog() {
        if (closed) return;
        final int generation = ++loadGeneration;
        final String id = selectedLog;
        final File app = getExternalFilesDir(null);
        final File shared = sharedPrototype();
        currentLog = null;
        logContent.setText("Loading " + id + " log…");
        logPath.setText("");
        logMeta.setText("Reading at most the last " + (LauncherLog.MAX_BYTES / 1024) + " KiB; no file is changed.");
        updateLogTabs();
        worker.execute(() -> {
            LauncherLog.Entry entry;
            try {
                entry = LauncherLog.read(app, shared, id);
            } catch (RuntimeException error) {
                entry = new LauncherLog.Entry(id, "", "", false, message(error));
            }
            LauncherLog.Entry result = entry;
            runOnUiThread(() -> {
                if (closed || generation != loadGeneration || !id.equals(selectedLog)) return;
                currentLog = result;
                logPath.setText(result.path.isEmpty() ? "No readable log file selected" : result.path);
                logMeta.setText(result.truncated
                        ? "Large log: showing only the last " + (LauncherLog.MAX_BYTES / 1024)
                        + " KiB. The original file was not changed."
                        : "Read-only log tail. Refresh to read the latest file contents.");
                if (!result.error.isEmpty()) logContent.setText(result.error);
                else logContent.setText(result.content.isEmpty() ? "Log is empty." : result.content);
            });
        });
    }

    private void copyLog() {
        LauncherLog.Entry entry = currentLog;
        if (entry == null || entry.content.isEmpty() || !entry.error.isEmpty()) {
            Toast.makeText(this, "There is no log text to copy yet.", Toast.LENGTH_SHORT).show();
            return;
        }
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard == null) {
            Toast.makeText(this, "Android clipboard is unavailable.", Toast.LENGTH_SHORT).show();
            return;
        }
        clipboard.setPrimaryClip(ClipData.newPlainText("ProtoDroid " + entry.name, entry.content));
        Toast.makeText(this, entry.truncated ? "Copied the displayed log tail." : "Log copied.", Toast.LENGTH_SHORT).show();
    }

    private void addRow(LinearLayout rows, String label, String value) {
        TextView heading = text(label, 13, RED, true);
        heading.setPadding(0, dp(12), 0, 0);
        rows.addView(heading);
        TextView content = text(value, 18, BONE, false);
        content.setTextIsSelectable(true);
        content.setPadding(0, 0, 0, dp(4));
        rows.addView(content);
    }

    private TextView text(String value, int size, int color, boolean heavy) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setTypeface(heavy ? bold : regular);
        view.setLineSpacing(0f, 1.08f);
        return view;
    }

    private Button button(String label, Runnable action) {
        Button view = new Button(this);
        view.setText(label);
        view.setTextSize(13);
        view.setAllCaps(false);
        view.setGravity(Gravity.CENTER);
        view.setMinWidth(0);
        view.setMinimumWidth(0);
        view.setPadding(dp(10), 0, dp(10), 0);
        view.setOnClickListener(ignored -> action.run());
        styleButton(view, false);
        return view;
    }

    private void styleButton(Button view, boolean selected) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(selected ? 0xff241417 : 0xff111013);
        background.setCornerRadius(dp(2));
        background.setStroke(dp(1), selected ? RED : 0xff4a4545);
        view.setBackground(background);
        view.setTextColor(selected ? BONE : DIM);
        view.setTypeface(selected ? bold : regular);
    }

    private String installedVersion() {
        try {
            String version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
            return version == null || version.isEmpty() ? "Unknown" : version;
        } catch (Exception error) {
            return "Unknown";
        }
    }

    private String totalMemory() {
        try {
            ActivityManager manager = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
            ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
            if (manager != null) {
                manager.getMemoryInfo(info);
                return String.format(Locale.ROOT, "%.1f GiB", info.totalMem / (1024.0 * 1024.0 * 1024.0));
            }
        } catch (RuntimeException ignored) {}
        return "Unknown";
    }

    private File sharedPrototype() {
        return new File(Environment.getExternalStorageDirectory(), "Prototype");
    }

    private static String safe(String value) {
        return value == null || value.isEmpty() ? "Unknown" : value;
    }

    private static String message(Exception error) {
        String value = error.getMessage();
        return value == null || value.isEmpty() ? error.getClass().getSimpleName() : value;
    }

    private int dp(int value) {
        return Math.max(1, Math.round(value * getResources().getDisplayMetrics().density));
    }

    @Override
    protected void onResume() {
        super.onResume();
        ImmersiveMode.apply(this);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) ImmersiveMode.apply(this);
    }

    @Override
    public void onBackPressed() {
        if (logsVisible) showLogs(false);
        else super.onBackPressed();
    }

    @Override
    public void finish() {
        super.finish();
        LauncherMotion.finishTransition(this);
    }

    @Override
    protected void onDestroy() {
        closed = true;
        ++loadGeneration;
        worker.shutdownNow();
        super.onDestroy();
    }
}
