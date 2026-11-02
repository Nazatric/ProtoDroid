package com.protodroid;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Window;

/** Standalone host for the existing validated driver library, opened from the native Compose launcher. */
public final class DriverActivity extends Activity {
    private DriverManager driverManager;
    private DriverView driverView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window window = getWindow();
        ImmersiveMode.apply(this);
        window.setStatusBarColor(Color.BLACK);
        window.setNavigationBarColor(Color.BLACK);
        driverManager = new DriverManager(this);
        driverView = new DriverView(this, new GameSettings(this), driverManager, this::finish);
        setContentView(driverView);
        LauncherMotion.transition(this);
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
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        driverManager.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    protected void onDestroy() {
        if (driverManager != null) {
            driverManager.setObserver(null);
            driverManager.close();
        }
        super.onDestroy();
    }

    @Override
    public void finish() {
        super.finish();
        LauncherMotion.finishTransition(this);
    }
}
