package com.protodroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.List;
import java.util.Locale;

/** Installed local mods, import, compatibility errors and next-Play activation. */
public final class ModsActivity extends Activity {
    private static final int REQUEST_IMPORT = 71;
    private static final int RED = 0xffc9141a;
    private static final int BONE = 0xffece8e1;
    private static final int DIM = 0xffaaa49d;
    private static final int OK = 0xff8fe07a;

    private LinearLayout list;
    private TextView summary;
    private TextView status;
    private Button importButton;
    private boolean busy;

    @SuppressWarnings("deprecation")  // API 26–35 fullscreen launcher styling; still supported on API 36.
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window window = getWindow();
        window.setStatusBarColor(Color.BLACK);
        window.setNavigationBarColor(Color.BLACK);

        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(20), dp(14), dp(20), dp(12));
        page.setBackgroundColor(0xff08090c);

        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        Button back = button("‹  BACK", false);
        back.setOnClickListener(v -> finish());
        header.addView(back, new LinearLayout.LayoutParams(dp(104), dp(46)));
        TextView title = text("PROTOTYPE  /  MODS", 23, BONE, true);
        title.setPadding(dp(16), 0, 0, 0);
        header.addView(title, new LinearLayout.LayoutParams(0, dp(48), 1f));
        importButton = button("IMPORT", true);
        importButton.setOnClickListener(v -> openImport());
        header.addView(importButton, new LinearLayout.LayoutParams(dp(126), dp(46)));
        page.addView(header, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        summary = text("Installed mods", 13, DIM, false);
        summary.setPadding(0, dp(7), 0, dp(7));
        page.addView(summary);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list, new ScrollView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        page.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        status = text("Imports start disabled. ARM64 plugins require translation; x86 ASI/DLL files do not run.",
                12, DIM, false);
        status.setPadding(0, dp(9), 0, 0);
        status.setMaxLines(3);
        page.addView(status);
        setContentView(page);
        reload();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!busy && list != null) reload();
    }

    private void reload() {
        List<ModManifest> mods = ModManager.list(this);
        list.removeAllViews();
        summary.setText(String.format(Locale.ROOT, "%d INSTALLED  /  %d ACTIVE NEXT PLAY  /  %s",
                mods.size(), ModManager.activeCount(mods), ModManager.modsDir(this).getAbsolutePath()));
        if (mods.isEmpty()) {
            TextView empty = text("No mods installed.\n\nImport a ProtoDroid ZIP, a texture pack with textures.ini, "
                    + "a loose game-data file, or a translated ARM64 plugin. Nothing runs until you enable it and press Play.",
                    16, DIM, false);
            empty.setGravity(Gravity.CENTER);
            empty.setPadding(dp(24), dp(44), dp(24), dp(44));
            list.addView(empty, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT));
            return;
        }
        for (ModManifest mod : mods) addMod(mod);
    }

    private void addMod(ModManifest mod) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(12), dp(10), dp(12), dp(10));
        row.setBackground(panel(mod.enabled ? 0x402b090c : 0x0015181f));
        LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        rowParams.bottomMargin = dp(8);
        list.addView(row, rowParams);

        FrameLayout coverFrame = new FrameLayout(this);
        coverFrame.setBackgroundColor(0xff17191f);
        TextView placeholder = text(mod.plugin.isEmpty() ? "P3D" : "ARM64", 17, RED, true);
        placeholder.setGravity(Gravity.CENTER);
        coverFrame.addView(placeholder, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        ImageView cover = new ImageView(this);
        cover.setScaleType(ImageView.ScaleType.CENTER_CROP);
        cover.setContentDescription(mod.name + " cover");
        Bitmap bitmap = decodeCover(mod.coverFile());
        if (bitmap != null) cover.setImageBitmap(bitmap);
        coverFrame.addView(cover, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        row.addView(coverFrame, new LinearLayout.LayoutParams(dp(128), dp(94)));

        LinearLayout details = new LinearLayout(this);
        details.setOrientation(LinearLayout.VERTICAL);
        details.setPadding(dp(14), 0, dp(10), 0);
        row.addView(details, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView name = text(mod.name, 19, BONE, true);
        name.setMaxLines(1);
        details.addView(name);
        StringBuilder meta = new StringBuilder();
        if (!mod.version.isEmpty()) meta.append(mod.version);
        if (!mod.author.isEmpty()) {
            if (meta.length() > 0) meta.append("  ·  ");
            meta.append(mod.author);
        }
        if (meta.length() > 0) meta.append("  ·  ");
        meta.append(mod.contentLabel().isEmpty() ? "Incompatible package" : mod.contentLabel());
        TextView metadata = text(meta.toString(), 12, DIM, false);
        metadata.setMaxLines(2);
        details.addView(metadata);
        if (!mod.description.isEmpty()) {
            TextView description = text(mod.description, 13, BONE, false);
            description.setMaxLines(3);
            details.addView(description);
        }
        String diagnostic = !mod.error.isEmpty() ? mod.error : mod.launchError;
        if (!diagnostic.isEmpty()) {
            TextView error = text(diagnostic, 13, 0xffff8c83, false);
            error.setMaxLines(4);
            details.addView(error);
        }
        String stateText = !mod.error.isEmpty() ? "INCOMPATIBLE PACKAGE"
                : !mod.launchError.isEmpty() ? (mod.enabled ? "LAST LOAD FAILED / RETRY ON PLAY" : "LAST LOAD FAILED / DISABLED")
                : mod.enabled ? "ACTIVE ON NEXT PLAY" : "INSTALLED / DISABLED";
        TextView state = text(stateText, 12, !diagnostic.isEmpty() ? 0xffff8c83 : mod.enabled ? OK : DIM, true);
        details.addView(state);

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.VERTICAL);
        row.addView(actions, new LinearLayout.LayoutParams(dp(116), ViewGroup.LayoutParams.WRAP_CONTENT));
        Button toggle = button(mod.enabled ? "DISABLE" : "ENABLE", !mod.enabled && mod.error.isEmpty());
        toggle.setEnabled(mod.error.isEmpty() || mod.enabled);
        toggle.setOnClickListener(v -> toggle(mod));
        actions.addView(toggle, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(42)));
        Button remove = button("REMOVE", false);
        LinearLayout.LayoutParams removeParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(42));
        removeParams.topMargin = dp(7);
        actions.addView(remove, removeParams);
        remove.setOnClickListener(v -> confirmRemove(mod));
    }

    private void toggle(ModManifest mod) {
        if (!mod.enabled && !mod.plugin.isEmpty()) {
            new AlertDialog.Builder(this)
                    .setTitle("Enable native ARM64 plugin?")
                    .setMessage("Only enable plugins you trust. Native code can crash the game. It loads on the next Play; "
                            + "Windows x86 ASI/DLL compatibility is not provided.")
                    .setNegativeButton("Cancel", null)
                    .setPositiveButton("Enable", (dialog, which) -> setEnabled(mod, true))
                    .show();
            return;
        }
        setEnabled(mod, !mod.enabled);
    }

    private void setEnabled(ModManifest mod, boolean enabled) {
        try {
            ModManager.setEnabled(this, mod, enabled);
            status.setText((enabled ? "Enabled " : "Disabled ") + mod.name + ". The change applies on the next Play.");
            reload();
        } catch (Exception e) {
            showError(e);
        }
    }

    private void confirmRemove(ModManifest mod) {
        new AlertDialog.Builder(this)
                .setTitle("Remove " + mod.name + "?")
                .setMessage("This removes only the mod's own folder. Original game files and shared saves are not touched.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Remove", (dialog, which) -> {
                    try {
                        ModManager.remove(this, mod);
                        status.setText("Removed " + mod.name + ".");
                        reload();
                    } catch (Exception e) {
                        showError(e);
                    }
                })
                .show();
    }

    private void openImport() {
        if (busy) return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_IMPORT);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_IMPORT || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri source = data.getData();
        busy = true;
        importButton.setEnabled(false);
        status.setText("Importing and validating…");
        new Thread(() -> {
            String result;
            try {
                ModManifest imported = ModManager.importMod(this, source);
                result = imported.error.isEmpty()
                        ? "Installed '" + imported.name + "' disabled. Review it, then enable for the next Play."
                        : "Installed with a compatibility error: " + imported.error;
            } catch (Exception e) {
                result = "Import failed: " + (e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage());
            }
            String message = result;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                busy = false;
                importButton.setEnabled(true);
                status.setText(message);
                reload();
            });
        }, "mod-import").start();
    }

    private void showError(Exception e) {
        String message = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
        status.setText(message);
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }

    private Bitmap decodeCover(File file) {
        if (file == null || !file.isFile() || file.length() > 24 * 1024 * 1024) return null;
        try {
            BitmapFactory.Options bounds = new BitmapFactory.Options();
            bounds.inJustDecodeBounds = true;
            BitmapFactory.decodeFile(file.getAbsolutePath(), bounds);
            if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null;
            int sample = 1;
            while (bounds.outWidth / sample > 768 || bounds.outHeight / sample > 768) sample *= 2;
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inSampleSize = sample;
            options.inPreferredConfig = Bitmap.Config.RGB_565;
            return BitmapFactory.decodeFile(file.getAbsolutePath(), options);
        } catch (RuntimeException | OutOfMemoryError e) {
            return null;
        }
    }

    private TextView text(String value, int sp, int color, boolean strong) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setGravity(Gravity.CENTER_VERTICAL);
        view.setTypeface(Typeface.create("sans-serif-condensed", strong ? Typeface.BOLD : Typeface.NORMAL));
        return view;
    }

    private Button button(String value, boolean primary) {
        Button button = new Button(this);
        button.setText(value);
        button.setTextSize(12);
        button.setTextColor(primary ? Color.WHITE : BONE);
        button.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        button.setBackground(panel(primary ? RED : 0xff202229));
        button.setAllCaps(false);
        return button;
    }

    private GradientDrawable panel(int color) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(3));
        drawable.setStroke(dp(1), 0x35ffffff);
        return drawable;
    }

    private int dp(int value) {
        return Math.max(1, Math.round(value * getResources().getDisplayMetrics().density));
    }
}
