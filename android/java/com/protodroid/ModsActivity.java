package com.protodroid;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.Window;

/** Hosts the Prototype-style mod library and performs validated imports/lifecycle writes off the UI thread. */
public final class ModsActivity extends Activity implements ModsView.Listener {
    private static final int REQUEST_IMPORT = 71;

    private interface BackgroundAction {
        String run() throws Exception;
    }

    private ModsView view;
    private boolean busy;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window window = getWindow();
        window.setStatusBarColor(Color.BLACK);
        window.setNavigationBarColor(Color.BLACK);
        view = new ModsView(this, new GameSettings(this), this);
        setContentView(view);
        LauncherMotion.transition(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (view != null && !busy) view.reload();
    }

    @Override
    protected void onDestroy() {
        if (view != null) view.close();
        super.onDestroy();
    }

    @Override
    public void onClose() {
        finish();
    }

    @Override
    public void finish() {
        super.finish();
        LauncherMotion.finishTransition(this);
    }

    @Override
    public void onBackPressed() {
        if (view != null && view.handleBack()) return;
        super.onBackPressed();
    }

    @Override
    public void onImport() {
        if (busy) return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_IMPORT);
    }

    @Override
    public void onSetEnabled(ModManifest mod, boolean enabled) {
        if (busy) return;
        runBackground("Updating the mod for the next Play.", "Could not change the mod", () -> {
            ModManager.setEnabled(this, mod, enabled);
            return (enabled ? "Enabled '" : "Disabled '") + mod.name
                    + "'. The change applies on the next Play.";
        });
    }

    @Override
    public void onRemove(ModManifest mod) {
        if (busy) return;
        runBackground("Removing the private mod copy. Original game data stays untouched.",
                "Could not remove the mod", () -> {
                    ModManager.remove(this, mod);
                    return "Removed '" + mod.name + "'. Original game files and shared saves were not touched.";
                });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_IMPORT || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri source = data.getData();
        runBackground("Importing and validating the local package. Keep ProtoDroid open.",
                "Import failed", () -> {
                    ModManifest imported = ModManager.importMod(this, source);
                    return imported.error.isEmpty()
                            ? "Installed '" + imported.name + "' disabled. Review it, then enable it for the next Play."
                            : "Installed with a compatibility error: " + imported.error;
                });
    }

    private void runBackground(String progress, String failurePrefix, BackgroundAction action) {
        if (busy) return;
        busy = true;
        view.setBusy(true, progress);
        new Thread(() -> {
            String result;
            try {
                result = action.run();
            } catch (Exception error) {
                result = failurePrefix + ": " + message(error);
            }
            String message = result;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                busy = false;
                view.setBusy(false, message);
                view.setStatus(message, true);
                view.reload();
            });
        }, "mod-operation").start();
    }

    private static String message(Exception error) {
        return error.getMessage() == null || error.getMessage().trim().isEmpty()
                ? error.getClass().getSimpleName() : error.getMessage();
    }
}
