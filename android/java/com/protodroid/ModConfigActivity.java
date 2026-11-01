package com.protodroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.PorterDuff;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.GridLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/** Compact Fabric-style editor for options declared by one mod. Values are saved only on Done. */
public final class ModConfigActivity extends Activity {
    static final String EXTRA_MOD_ID = "com.protodroid.extra.MOD_ID";
    private static final int BONE = 0xfff0ece5;
    private static final int DIM = 0xffbdb7b0;
    private static final int RED = 0xffc9141a;
    private static final int RED_HOT = 0xffff463f;
    private static final String STATE_VALUE = "mod-option-value.";
    private static final String STATE_DIRTY = "mod-option-dirty";

    private final LinkedHashMap<String, String> values = new LinkedHashMap<>();
    private ModManifest mod;
    private GridLayout grid;
    private TextView help;
    private Button done;
    private Typeface medium;
    private Typeface semibold;
    private boolean dirty;
    private boolean saving;
    private int density;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ImmersiveMode.apply(this);
        density = (int) getResources().getDisplayMetrics().density;
        medium = font("setup/fonts/barlow_condensed_medium.ttf");
        semibold = font("setup/fonts/barlow_condensed_semibold.ttf");
        String id = getIntent().getStringExtra(EXTRA_MOD_ID);
        if (!ModManifest.isSafeId(id)) {
            Toast.makeText(this, "Invalid mod", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        mod = ModManifest.read(new File(ModManager.modsDir(this), id));
        if (!mod.error.isEmpty() || mod.options.isEmpty()) {
            Toast.makeText(this, mod.error.isEmpty() ? "This mod has no options" : mod.error,
                    Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        try {
            Map<String, String> saved = ModConfig.loadValues(mod.directory);
            for (ModConfig.Option option : mod.options) values.put(option.id, option.value(saved));
            if (savedInstanceState != null) {
                for (ModConfig.Option option : mod.options) {
                    String key = STATE_VALUE + option.id;
                    String restored = savedInstanceState.getString(key);
                    if (restored != null) values.put(option.id, restored);
                }
                dirty = savedInstanceState.getBoolean(STATE_DIRTY, dirty);
            }
        } catch (Exception error) {
            Toast.makeText(this, "Could not read mod settings: " + message(error), Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        buildContent();
        LauncherMotion.transition(this);
    }

    private Typeface font(String path) {
        try {
            return Typeface.createFromAsset(getAssets(), path);
        } catch (RuntimeException error) {
            return Typeface.DEFAULT;
        }
    }

    private int dp(int value) {
        return value * density;
    }

    private GradientDrawable background(int fill, int edge) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill);
        drawable.setCornerRadius(dp(3));
        drawable.setStroke(Math.max(1, density), edge);
        return drawable;
    }

    private TextView text(String value, int sp, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setTypeface(medium);
        view.setGravity(Gravity.CENTER_VERTICAL);
        return view;
    }

    private Button button(String label, boolean primary) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(14);
        button.setTextColor(BONE);
        button.setTypeface(semibold);
        button.setAllCaps(false);
        button.setMinHeight(dp(48));
        button.setMinimumHeight(dp(48));
        button.setBackground(background(primary ? 0xff72121a : 0xff202027, primary ? RED_HOT : 0xff51414a));
        return button;
    }

    private void buildContent() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        ImageView backdrop = new ImageView(this);
        try (java.io.InputStream input = getAssets().open("setup/images/bg_3.jpg")) {
            backdrop.setImageBitmap(BitmapFactory.decodeStream(input));
        } catch (Exception ignored) {
            backdrop.setBackgroundColor(0xff111218);
        }
        backdrop.setScaleType(ImageView.ScaleType.CENTER_CROP);
        backdrop.setAlpha(0.38f);
        backdrop.setColorFilter(0x66000000, PorterDuff.Mode.SRC_ATOP);
        root.addView(backdrop, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(18), dp(12), dp(18), dp(12));
        root.addView(content, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        content.addView(header, new LinearLayout.LayoutParams(-1, dp(52)));
        Button back = button("BACK", false);
        back.setOnClickListener(view -> onBackPressed());
        header.addView(back, new LinearLayout.LayoutParams(dp(94), dp(48)));
        TextView title = text(mod.name.toUpperCase(Locale.ROOT), 23, BONE);
        title.setTypeface(semibold);
        title.setSingleLine(true);
        title.setEllipsize(android.text.TextUtils.TruncateAt.END);
        LinearLayout.LayoutParams titleParams = new LinearLayout.LayoutParams(0, -1, 1f);
        titleParams.setMargins(dp(14), 0, dp(14), 0);
        header.addView(title, titleParams);
        Button reset = button("RESET", false);
        reset.setContentDescription("Reset all options to author defaults");
        reset.setOnClickListener(view -> resetDefaults());
        header.addView(reset, new LinearLayout.LayoutParams(dp(94), dp(48)));
        done = button("DONE", true);
        done.setOnClickListener(view -> save());
        LinearLayout.LayoutParams doneParams = new LinearLayout.LayoutParams(dp(94), dp(48));
        doneParams.setMargins(dp(8), 0, 0, 0);
        header.addView(done, doneParams);

        TextView subtitle = text("MOD OPTIONS  /  SAVED FOR THE NEXT PLAY", 12, RED_HOT);
        subtitle.setTypeface(semibold);
        content.addView(subtitle, new LinearLayout.LayoutParams(-1, dp(28)));
        help = text("Focus, hover, or hold an option for details.", 14, DIM);
        help.setMaxLines(3);
        help.setPadding(dp(12), dp(5), dp(12), dp(5));
        help.setBackground(background(0xe613151b, 0xff51313a));
        LinearLayout.LayoutParams helpParams = new LinearLayout.LayoutParams(-1, dp(52));
        helpParams.setMargins(0, 0, 0, dp(7));
        content.addView(help, helpParams);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        content.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1f));
        grid = new GridLayout(this);
        grid.setColumnCount(getResources().getConfiguration().screenWidthDp >= 560 ? 2 : 1);
        grid.setAlignmentMode(GridLayout.ALIGN_BOUNDS);
        grid.setUseDefaultMargins(false);
        scroll.addView(grid, new ScrollView.LayoutParams(-1, -2));
        renderOptions();
        setContentView(root);
        if (LauncherMotion.enabled()) {
            content.setAlpha(0f);
            content.setTranslationX(dp(18));
            content.animate().alpha(1f).translationX(0f).setDuration(240).start();
        }
    }

    private void renderOptions() {
        grid.removeAllViews();
        for (ModConfig.Option option : mod.options) grid.addView(optionCard(option));
    }

    private View optionCard(ModConfig.Option option) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(13), dp(9), dp(13), dp(10));
        card.setBackground(background(0xf013151b, 0xff45363f));
        GridLayout.LayoutParams params = new GridLayout.LayoutParams();
        params.width = 0;
        params.height = ViewGroup.LayoutParams.WRAP_CONTENT;
        params.setMargins(dp(5), dp(5), dp(5), dp(5));
        params.columnSpec = GridLayout.spec(GridLayout.UNDEFINED, 1, 1f);
        card.setLayoutParams(params);

        TextView group = text(option.group.toUpperCase(Locale.ROOT), 11, RED_HOT);
        group.setTypeface(semibold);
        card.addView(group, new LinearLayout.LayoutParams(-1, dp(20)));
        TextView label = text(option.label, 18, BONE);
        label.setTypeface(semibold);
        label.setSingleLine(true);
        label.setEllipsize(android.text.TextUtils.TruncateAt.END);
        card.addView(label, new LinearLayout.LayoutParams(-1, dp(30)));

        if (option.type.equals(ModConfig.TOGGLE)) {
            Button toggle = button(display(option, values.get(option.id)), true);
            attachHelp(toggle, option);
            toggle.setOnClickListener(view -> {
                if (saving) return;
                String next = values.get(option.id).equals("1") ? "0" : "1";
                values.put(option.id, next);
                toggle.setText(display(option, next));
                changed(option);
            });
            card.addView(toggle, new LinearLayout.LayoutParams(-1, dp(48)));
        } else if (option.type.equals(ModConfig.SLIDER)) {
            TextView value = text(display(option, values.get(option.id)), 15, BONE);
            value.setGravity(Gravity.CENTER);
            card.addView(value, new LinearLayout.LayoutParams(-1, dp(26)));
            SeekBar slider = new SeekBar(this);
            slider.setMax(option.sliderSteps());
            slider.setProgress(option.sliderProgress(values.get(option.id)));
            attachHelp(slider, option);
            slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                    if (saving) return;
                    String next = option.sliderValue(progress);
                    values.put(option.id, next);
                    value.setText(display(option, next));
                    if (fromUser) dirty = true;
                }
                @Override public void onStartTrackingTouch(SeekBar seekBar) { showHelp(option); }
                @Override public void onStopTrackingTouch(SeekBar seekBar) { }
            });
            card.addView(slider, new LinearLayout.LayoutParams(-1, dp(42)));
        } else if (option.type.equals(ModConfig.CHOICE)) {
            Button choice = button(display(option, values.get(option.id)) + "  ›", false);
            attachHelp(choice, option);
            choice.setOnClickListener(view -> {
                if (saving) return;
                String current = values.get(option.id);
                int index = option.choices.indexOf(current);
                String next = option.choices.get((index + 1 + option.choices.size()) % option.choices.size());
                values.put(option.id, next);
                choice.setText(display(option, next) + "  ›");
                changed(option);
            });
            card.addView(choice, new LinearLayout.LayoutParams(-1, dp(48)));
        } else {
            EditText input = new EditText(this);
            input.setSingleLine(true);
            input.setText(values.get(option.id));
            input.setTextSize(16);
            input.setTextColor(BONE);
            input.setHintTextColor(DIM);
            input.setTypeface(medium);
            input.setPadding(dp(10), 0, dp(10), 0);
            input.setFilters(new InputFilter[] {new InputFilter.LengthFilter(option.maxLength)});
            input.setBackground(background(0xff0b0d12, 0xff51414a));
            attachHelp(input, option);
            input.addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence value, int start, int count, int after) { }
                @Override public void onTextChanged(CharSequence value, int start, int before, int count) { }
                @Override public void afterTextChanged(Editable value) {
                    if (saving) return;
                    values.put(option.id, value.toString());
                    dirty = true;
                }
            });
            card.addView(input, new LinearLayout.LayoutParams(-1, dp(48)));
        }
        return card;
    }

    private void attachHelp(View view, ModConfig.Option option) {
        view.setContentDescription(option.label + ". " + option.description);
        view.setOnFocusChangeListener((focused, hasFocus) -> { if (hasFocus) showHelp(option); });
        view.setOnHoverListener((hovered, event) -> {
            if (event.getActionMasked() == MotionEvent.ACTION_HOVER_ENTER) showHelp(option);
            return false;
        });
        view.setOnLongClickListener(held -> {
            showHelp(option);
            help.announceForAccessibility(option.label + ". " + option.description);
            return true;
        });
    }

    private String display(ModConfig.Option option, String value) {
        if (option.type.equals(ModConfig.TOGGLE)) return value.equals("1") ? "ON" : "OFF";
        return value;
    }

    private void changed(ModConfig.Option option) {
        dirty = true;
        showHelp(option);
        done.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
    }

    private void showHelp(ModConfig.Option option) {
        if (help == null) return;
        help.setText(option.label + "  /  Default: " + display(option, option.defaultValue)
                + "\n" + option.description);
    }

    private void resetDefaults() {
        if (saving) return;
        for (ModConfig.Option option : mod.options) values.put(option.id, option.defaultValue);
        dirty = true;
        renderOptions();
        help.setText("Author defaults restored. Tap Done to save.");
    }

    private void save() {
        if (saving) return;
        saving = true;
        done.setEnabled(false);
        help.setText("Saving mod options…");
        final LinkedHashMap<String, String> snapshot = new LinkedHashMap<>(values);
        new Thread(() -> {
            String error = null;
            try {
                ModConfig.saveValues(mod.directory, mod.options, snapshot);
            } catch (Exception failure) {
                error = message(failure);
            }
            String result = error;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                saving = false;
                done.setEnabled(true);
                if (result != null) {
                    help.setText("Save failed: " + result);
                    Toast.makeText(this, "Could not save mod settings: " + result, Toast.LENGTH_LONG).show();
                    return;
                }
                dirty = false;
                Toast.makeText(this, "Mod options saved for the next Play", Toast.LENGTH_SHORT).show();
                finish();
            });
        }, "mod-config-save").start();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        for (Map.Entry<String, String> entry : values.entrySet()) {
            outState.putString(STATE_VALUE + entry.getKey(), entry.getValue());
        }
        outState.putBoolean(STATE_DIRTY, dirty);
        super.onSaveInstanceState(outState);
    }

    @Override
    public void onBackPressed() {
        if (saving) return;
        if (!dirty) {
            super.onBackPressed();
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle("Discard unsaved changes?")
                .setMessage("Saved options and game files stay unchanged.")
                .setNegativeButton("KEEP EDITING", null)
                .setPositiveButton("DISCARD", (dialog, which) -> {
                    dirty = false;
                    ModConfigActivity.super.onBackPressed();
                })
                .show();
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
    public void finish() {
        super.finish();
        LauncherMotion.finishTransition(this);
    }

    private static String message(Exception error) {
        return error.getMessage() == null || error.getMessage().trim().isEmpty()
                ? error.getClass().getSimpleName() : error.getMessage();
    }
}
