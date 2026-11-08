package com.protodroid;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Window;

/** Lightweight launcher-hosted layout editor. It does not launch Prototype or load the SDL runtime. */
public final class TouchLayoutActivity extends Activity {
    private TouchControls controls;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window window = getWindow();
        ImmersiveMode.apply(this);
        window.setStatusBarColor(Color.BLACK);
        window.setNavigationBarColor(Color.BLACK);
        controls = new TouchControls(this, true);
        controls.openLayoutEditor();
        setContentView(controls);
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
    public void onBackPressed() {
        if (controls != null && controls.handleBack()) return;
        super.onBackPressed();
    }

    @Override
    public void finish() {
        super.finish();
        LauncherMotion.finishTransition(this);
    }
}
