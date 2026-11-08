package com.protodroid;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * In-game mod settings panel, in the launcher's visual language: a thin red handle on the left
 * edge opens a black panel with Barlow Condensed type where every enabled mod's options can be
 * changed while the game runs. Writes persist through the same ModConfig store the launcher
 * uses, then notify the sandboxed Lua runtime — the mod sees new values on its next frame
 * (and through a protodroid.settings_changed event), so sliders adjust the game live.
 */
public class ModPanel extends FrameLayout {

    private static final int RED = 0xffc9141a;
    private static final int RED_HOT = 0xffff2b2b;
    private static final int TEXT_DIM = 0xff8a8f98;

    private static final class ModEntryView {
        final ModManifest mod;
        final Map<String, String> values = new HashMap<>();

        ModEntryView(ModManifest mod) {
            this.mod = mod;
        }
    }

    private final List<ModEntryView> entries = new ArrayList<>();
    private LinearLayout list;
    private final TextView handle;
    private final View panel;
    private final Typeface medium;
    private final Typeface semibold;
    private final Typeface bold;
    private boolean open;

    /** Native: the sandboxed runtime re-reads this mod's settings on the next engine frame. */
    private static native void nativeSettingsChanged(String modId);

    public ModPanel(Context context) {
        super(context);
        setVisibility(GONE);
        medium = typeface("barlow_condensed_medium.ttf");
        semibold = typeface("barlow_condensed_semibold.ttf");
        bold = typeface("barlow_condensed_bold.ttf");

        for (ModManifest mod : ModManager.list(context)) {
            if (!mod.enabled || mod.error != null && !mod.error.isEmpty() || mod.options.isEmpty()) continue;
            ModEntryView entry = new ModEntryView(mod);
            try {
                entry.values.putAll(ModConfig.loadValues(mod.directory));
            } catch (Exception ignored) {
                // defaults below fill anything missing
            }
            entries.add(entry);
        }

        panel = buildPanel(context);
        handle = buildHandle(context);
        addView(panel, new FrameLayout.LayoutParams(dp(320), ViewGroup.LayoutParams.MATCH_PARENT,
                Gravity.START | Gravity.TOP));
        addView(handle, new FrameLayout.LayoutParams(dp(30), dp(96), Gravity.START | Gravity.CENTER_VERTICAL));
        if (!entries.isEmpty()) {
            setVisibility(VISIBLE);
            setOpen(false);
        }
    }

    private TextView buildHandle(Context context) {
        TextView view = new TextView(context);
        view.setTypeface(bold);
        view.setText("M\nO\nD\nS");
        view.setTextColor(Color.WHITE);
        view.setTextSize(12f);
        view.setGravity(Gravity.CENTER);
        view.setLineSpacing(0f, 0.9f);
        GradientDrawable chrome = new GradientDrawable();
        chrome.setColor(RED);
        view.setBackground(chrome);
        view.setOnClickListener(v -> setOpen(!open));
        return view;
    }

    private View buildPanel(Context context) {
        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setBackgroundColor(0xf2000000);
        column.setPadding(dp(18), dp(16), dp(18), dp(12));

        TextView title = new TextView(context);
        title.setTypeface(semibold);
        title.setText("MOD SETTINGS");
        title.setTextColor(Color.WHITE);
        title.setTextSize(20f);
        title.setLetterSpacing(0.06f);
        column.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView hint = new TextView(context);
        hint.setTypeface(medium);
        hint.setText("Changes apply instantly.");
        hint.setTextColor(TEXT_DIM);
        hint.setTextSize(13f);
        hint.setPadding(0, dp(2), 0, dp(8));
        column.addView(hint, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        ScrollView scroll = new ScrollView(context);
        scroll.setFillViewport(true);
        list = new LinearLayout(context);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        for (ModEntryView entry : entries) appendMod(entry);
        column.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        return column;
    }

    private void appendMod(ModEntryView entry) {
        Context context = getContext();
        TextView name = new TextView(context);
        name.setTypeface(semibold);
        name.setText(entry.mod.name.toUpperCase());
        name.setTextColor(RED_HOT);
        name.setTextSize(16f);
        name.setLetterSpacing(0.04f);
        name.setPadding(0, dp(10), 0, dp(4));
        list.addView(name, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        for (final ModConfig.Option option : entry.mod.options) {
            String value = option.id != null ? entry.values.get(option.id) : null;
            if (value == null) value = option.defaultValue;
            if (option.type.equals(ModConfig.TOGGLE)) list.addView(toggleRow(entry, option, value));
            else if (option.type.equals(ModConfig.SLIDER)) list.addView(sliderRow(entry, option, value));
            else if (option.type.equals(ModConfig.CHOICE)) list.addView(choiceRows(entry, option, value));
        }
    }

    private View toggleRow(final ModEntryView entry, final ModConfig.Option option, String value) {
        Context context = getContext();
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(6), 0, dp(6));

        TextView label = new TextView(context);
        label.setTypeface(medium);
        label.setText(option.label);
        label.setTextColor(Color.WHITE);
        label.setTextSize(16f);
        row.addView(label, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        final TextView state = new TextView(context);
        state.setTypeface(semibold);
        state.setTextSize(15f);
        state.setPadding(dp(12), dp(4), dp(12), dp(4));
        state.setText("1".equals(value) ? "ON" : "OFF");
        restyleSwitch(state, "1".equals(value));
        row.addView(state, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        row.setOnClickListener(v -> {
            boolean on = "OFF".contentEquals(state.getText());
            state.setText(on ? "ON" : "OFF");
            restyleSwitch(state, on);
            save(entry, option, on ? "1" : "0");
        });
        return row;
    }

    private View sliderRow(final ModEntryView entry, final ModConfig.Option option, String value) {
        Context context = getContext();
        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(0, dp(6), 0, dp(6));

        LinearLayout header = new LinearLayout(context);
        header.setOrientation(LinearLayout.HORIZONTAL);
        TextView label = new TextView(context);
        label.setTypeface(medium);
        label.setText(option.label);
        label.setTextColor(Color.WHITE);
        label.setTextSize(16f);
        header.addView(label, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        final TextView number = new TextView(context);
        number.setTypeface(semibold);
        number.setTextColor(RED_HOT);
        number.setTextSize(15f);
        header.addView(number, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        column.addView(header, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        double current;
        try {
            current = Double.parseDouble(value);
        } catch (NumberFormatException | NullPointerException e) {
            current = option.defaultValue != null ? Double.parseDouble(option.defaultValue) : option.minimum;
        }
        final double minimum = option.minimum;
        final double span = Math.max(option.maximum - option.minimum, option.step > 0 ? option.step : 0.01);
        final double step = option.step > 0 ? option.step : span / 100.0;
        int progress = (int) Math.round((current - minimum) / span * 1000.0);
        final double initial = current;

        SeekBar bar = new SeekBar(context);
        bar.setMax(1000);
        bar.setProgress(Math.max(0, Math.min(1000, progress)));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            private double latest = initial;

            @Override
            public void onProgressChanged(SeekBar seekBar, int progressValue, boolean fromUser) {
                double snapped = minimum + Math.round((progressValue / 1000.0 * span) / step) * step;
                snapped = Math.max(option.minimum, Math.min(option.maximum, snapped));
                latest = snapped;
                number.setText(format(option, snapped));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                save(entry, option, String.valueOf(latest));
            }
        });
        number.setText(format(option, initial));
        column.addView(bar, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return column;
    }

    private View choiceRows(final ModEntryView entry, final ModConfig.Option option, String value) {
        Context context = getContext();
        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(0, dp(4), 0, dp(4));

        TextView label = new TextView(context);
        label.setTypeface(medium);
        label.setText(option.label);
        label.setTextColor(Color.WHITE);
        label.setTextSize(16f);
        column.addView(label, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        for (final String choice : option.choices) {
            TextView row = new TextView(context);
            row.setTypeface(medium);
            row.setText(choice);
            row.setTextSize(15f);
            row.setTextColor(choice.equals(value) ? Color.WHITE : TEXT_DIM);
            row.setPadding(dp(8), dp(6), dp(8), dp(6));
            GradientDrawable chrome = new GradientDrawable();
            chrome.setColor(Color.TRANSPARENT);
            chrome.setStroke(dp(1), choice.equals(value) ? RED : 0x33ffffff);
            row.setBackground(chrome);
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            params.topMargin = dp(4);
            row.setOnClickListener(v -> {
                for (int i = 0; i < column.getChildCount(); ++i) {
                    View other = column.getChildAt(i);
                    if (other instanceof TextView && other.getTag() instanceof String) {
                        TextView otherChoice = (TextView) other;
                        boolean selected = otherChoice == row;
                        otherChoice.setTextColor(selected ? Color.WHITE : TEXT_DIM);
                        GradientDrawable otherChrome = new GradientDrawable();
                        otherChrome.setColor(Color.TRANSPARENT);
                        otherChrome.setStroke(dp(1), selected ? RED : 0x33ffffff);
                        otherChoice.setBackground(otherChrome);
                    }
                }
                save(entry, option, choice);
            });
            row.setTag(choice);
            column.addView(row, params);
        }
        return column;
    }

    private void save(ModEntryView entry, ModConfig.Option option, String value) {
        entry.values.put(option.id, value);
        try {
            Map<String, String> change = new HashMap<>();
            change.put(option.id, value);
            ModConfig.saveValues(entry.mod.directory, entry.mod.options, change);
            nativeSettingsChanged(entry.mod.id);
        } catch (Exception e) {
            Toast.makeText(getContext(), "Could not save: " + e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }

    private void restyleSwitch(TextView state, boolean on) {
        GradientDrawable chrome = new GradientDrawable();
        chrome.setColor(on ? RED : 0x00000000);
        chrome.setStroke(dp(1), on ? RED : 0x33ffffff);
        state.setBackground(chrome);
    }

    private void setOpen(boolean value) {
        open = value;
        panel.setVisibility(open ? VISIBLE : INVISIBLE);
        panel.setClickable(open);  // while open, consume touches inside the panel instead of the game
        handle.setText(open ? "×" : "M\nO\nD\nS");
        handle.setTextAlignment(View.TEXT_ALIGNMENT_CENTER);
    }

    private static String format(ModConfig.Option option, double value) {
        String text = ModConfig.decimal(value);
        return text.endsWith(".0") ? text.substring(0, text.length() - 2) : text;
    }

    private int dp(int value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                getResources().getDisplayMetrics()));
    }

    private Typeface typeface(String name) {
        try {
            return Typeface.createFromAsset(getContext().getAssets(), "setup/fonts/" + name);
        } catch (RuntimeException e) {
            return Typeface.DEFAULT;
        }
    }
}
