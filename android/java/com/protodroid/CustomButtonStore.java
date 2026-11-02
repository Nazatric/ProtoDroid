package com.protodroid;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Versioned persistence for custom controls only; built-in index-based preferences are intentionally separate. */
final class CustomButtonStore {
    static final String PREFERENCE = "custom_buttons_v1";
    static final int MAX_BUTTONS = 24;

    static final class Entry {
        long id;
        int key1, key2, mouseButton;
        float x = -1, y = -1, size = 1;
        boolean hidden;
        String label = "";

        Entry(long id, int key1, int key2) {
            this.id = id;
            this.key1 = key1;
            this.key2 = key2;
        }

        Entry copy() {
            Entry result = new Entry(id, key1, key2);
            result.mouseButton = mouseButton;
            result.x = x;
            result.y = y;
            result.size = size;
            result.hidden = hidden;
            result.label = label;
            return result;
        }
    }

    private CustomButtonStore() {}

    static boolean validBinding(int key1, int key2) {
        return ControlKeys.isSupported(key1) && (key2 == 0
                || (ControlKeys.isSupported(key2) && key2 != key1));
    }

    static boolean validMouseButton(int button) {
        return button == 1 || button == 2 || button == 4;
    }

    static boolean validBinding(int key1, int key2, int mouseButton) {
        if (mouseButton != 0) return key1 == 0 && key2 == 0 && validMouseButton(mouseButton);
        return validBinding(key1, key2);
    }

    private static float coordinate(JSONObject object, String name) {
        double value = object.optDouble(name, -1);
        if (Double.isNaN(value) || Double.isInfinite(value) || value < 0 || value > 1) return -1;
        return (float) value;
    }

    private static float size(JSONObject object) {
        double value = object.optDouble("size", 1);
        if (Double.isNaN(value) || Double.isInfinite(value)) return 1;
        return (float) Math.max(0.6, Math.min(2.0, value));
    }

    static List<Entry> decode(String encoded) {
        List<Entry> result = new ArrayList<>();
        if (encoded == null || encoded.trim().isEmpty()) return result;
        Set<Long> ids = new HashSet<>();
        try {
            JSONArray array = new JSONArray(encoded);
            for (int i = 0; i < array.length() && result.size() < MAX_BUTTONS; ++i) {
                JSONObject object = array.optJSONObject(i);
                if (object == null) continue;
                long id = object.optLong("id", 0);
                int key1 = object.optInt("key1", 0), key2 = object.optInt("key2", 0);
                int mouseButton = object.optInt("mouse", 0);
                if (id <= 0 || !ids.add(id) || !validBinding(key1, key2, mouseButton)) continue;
                Entry entry = new Entry(id, key1, key2);
                entry.mouseButton = mouseButton;
                entry.x = coordinate(object, "x");
                entry.y = coordinate(object, "y");
                entry.size = size(object);
                entry.hidden = object.optBoolean("hidden", false);
                entry.label = ControlKeys.cleanLabel(object.optString("label", ""));
                result.add(entry);
            }
        } catch (Exception ignored) {
            // Android and host JSON exceptions differ; malformed optional controls stay non-fatal on both.
        }
        return result;
    }

    static String encode(List<Entry> entries) {
        JSONArray array = new JSONArray();
        if (entries == null) return array.toString();
        Set<Long> ids = new HashSet<>();
        int count = 0;
        for (Entry source : entries) {
            if (source == null || source.id <= 0 || !ids.add(source.id)
                    || !validBinding(source.key1, source.key2, source.mouseButton)) {
                continue;
            }
            if (++count > MAX_BUTTONS) break;
            Entry entry = source.copy();
            try {
                JSONObject object = new JSONObject();
                object.put("id", entry.id);
                object.put("key1", entry.key1);
                object.put("key2", entry.key2);
                object.put("mouse", entry.mouseButton);
                object.put("x", saneCoordinate(entry.x));
                object.put("y", saneCoordinate(entry.y));
                object.put("size", saneSize(entry.size));
                object.put("hidden", entry.hidden);
                object.put("label", ControlKeys.cleanLabel(entry.label));
                array.put(object);
            } catch (JSONException ignored) {
                // Values are validated above; skip rather than corrupt the remaining records.
            }
        }
        return array.toString();
    }

    private static float saneCoordinate(float value) {
        return Float.isNaN(value) || Float.isInfinite(value) || value < 0 || value > 1 ? -1 : value;
    }

    private static float saneSize(float value) {
        if (Float.isNaN(value) || Float.isInfinite(value)) return 1;
        return Math.max(0.6f, Math.min(2f, value));
    }
}
