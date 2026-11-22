package com.protodroid;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.math.BigDecimal;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/** Independent, bounded per-mod option schema and app-owned value persistence. No Android runtime is required. */
final class ModConfig {
    static final String FILE_NAME = "protodroid-settings.ini";
    private static final String TEMP_NAME = FILE_NAME + ".tmp";
    private static final int MAX_OPTIONS = 64;
    private static final int MAX_VALUES = 256;
    private static final int MAX_VALUE_FILE_BYTES = 128 * 1024;

    static final String TOGGLE = "toggle";
    static final String SLIDER = "slider";
    static final String CHOICE = "choice";
    static final String TEXT = "text";

    static final class Option {
        final String id;
        final String type;
        final String label;
        final String description;
        final String group;
        final String defaultValue;
        final int order;
        final double minimum;
        final double maximum;
        final double step;
        final int maxLength;
        final List<String> choices;

        Option(String id, String type, String label, String description, String group, String defaultValue,
               int order, double minimum, double maximum, double step, int maxLength, List<String> choices) {
            this.id = id;
            this.type = type;
            this.label = label;
            this.description = description;
            this.group = group;
            this.defaultValue = defaultValue;
            this.order = order;
            this.minimum = minimum;
            this.maximum = maximum;
            this.step = step;
            this.maxLength = maxLength;
            this.choices = choices;
        }

        String normalize(String input) throws IOException {
            String value = input == null ? defaultValue : input.trim();
            switch (type) {
                case TOGGLE:
                    return parseBoolean(value) ? "1" : "0";
                case SLIDER:
                    double number;
                    try {
                        number = Double.parseDouble(value);
                    } catch (NumberFormatException error) {
                        throw new IOException(label + " must be a number");
                    }
                    if (!finite(number) || number < minimum || number > maximum) {
                        throw new IOException(label + " is outside its allowed range");
                    }
                    double steps = Math.rint((number - minimum) / step);
                    double snapped = Math.max(minimum, Math.min(maximum, minimum + steps * step));
                    if (Math.abs(snapped - number) > Math.max(1e-9, step * 1e-7)) {
                        throw new IOException(label + " does not match its step");
                    }
                    return decimal(snapped);
                case CHOICE:
                    for (String choice : choices) if (choice.equals(value)) return value;
                    throw new IOException(label + " has an unknown choice");
                case TEXT:
                    if (value.length() > maxLength || hasControl(value)) {
                        throw new IOException(label + " is too long or contains control characters");
                    }
                    return value;
                default:
                    throw new IOException("Unsupported option type");
            }
        }

        String value(Map<String, String> saved) {
            String value = saved.get(id);
            if (value == null) return defaultValue;
            try {
                return normalize(value);
            } catch (IOException ignored) {
                return defaultValue;
            }
        }

        int sliderSteps() {
            return (int) Math.round((maximum - minimum) / step);
        }

        int sliderProgress(String value) {
            try {
                double number = Double.parseDouble(normalize(value));
                return Math.max(0, Math.min(sliderSteps(), (int) Math.rint((number - minimum) / step)));
            } catch (IOException | NumberFormatException ignored) {
                return 0;
            }
        }

        String sliderValue(int progress) {
            int bounded = Math.max(0, Math.min(sliderSteps(), progress));
            return decimal(minimum + bounded * step);
        }
    }

    private ModConfig() {}

    static List<Option> parseOptions(Map<String, String> values) throws IOException {
        Map<String, Map<String, String>> sections = new LinkedHashMap<>();
        for (Map.Entry<String, String> entry : values.entrySet()) {
            if (!entry.getKey().startsWith("setting.")) continue;
            String rest = entry.getKey().substring("setting.".length());
            int split = rest.lastIndexOf('.');
            if (split < 1 || split + 1 >= rest.length()) throw new IOException("Invalid mod option key");
            String id = rest.substring(0, split);
            String field = rest.substring(split + 1);
            if (!safeId(id)) throw new IOException("Invalid mod option ID " + id);
            Map<String, String> section = sections.computeIfAbsent(id, unused -> new LinkedHashMap<>());
            if (section.put(field, entry.getValue()) != null) throw new IOException("Duplicate mod option field");
        }
        if (sections.size() > MAX_OPTIONS) throw new IOException("Mod declares too many options");
        List<Option> result = new ArrayList<>();
        for (Map.Entry<String, Map<String, String>> entry : sections.entrySet()) {
            result.add(parseOption(entry.getKey(), entry.getValue()));
        }
        Collections.sort(result, Comparator.comparingInt((Option option) -> option.order)
                .thenComparing(option -> option.label.toLowerCase(Locale.ROOT)).thenComparing(option -> option.id));
        return result;
    }

    private static Option parseOption(String id, Map<String, String> fields) throws IOException {
        for (String field : fields.keySet()) {
            if (!field.matches("type|label|description|group|default|order|min|max|step|options|max_length")) {
                throw new IOException("Unknown field '" + field + "' for mod option " + id);
            }
        }
        String type = required(fields, "type", id).toLowerCase(Locale.ROOT);
        if (type.equals("boolean")) type = TOGGLE;
        if (!type.equals(TOGGLE) && !type.equals(SLIDER) && !type.equals(CHOICE) && !type.equals(TEXT)) {
            throw new IOException("Unsupported type for mod option " + id);
        }
        String label = required(fields, "label", id);
        if (label.length() > 80 || hasControl(label)) throw new IOException("Invalid label for mod option " + id);
        String description = optional(fields, "description", "Applies the next time you play.").trim();
        if (description.length() > 300 || hasControl(description)) {
            throw new IOException("Invalid description for mod option " + id);
        }
        String group = optional(fields, "group", "Options").trim();
        if (group.isEmpty() || group.length() > 60 || hasControl(group)) {
            throw new IOException("Invalid group for mod option " + id);
        }
        int order = integer(optional(fields, "order", "100"), -10000, 10000, "option order");
        double minimum = 0, maximum = 0, step = 0;
        int maxLength = 0;
        List<String> choices = new ArrayList<>();
        String defaultValue;
        if (type.equals(SLIDER)) {
            minimum = number(required(fields, "min", id), "minimum");
            maximum = number(required(fields, "max", id), "maximum");
            step = number(required(fields, "step", id), "step");
            double intervals = (maximum - minimum) / step;
            if (!(maximum > minimum) || !(step > 0) || intervals > 10000
                    || Math.abs(intervals - Math.rint(intervals)) > 1e-7) {
                throw new IOException("Invalid slider bounds for mod option " + id);
            }
        } else if (type.equals(CHOICE)) {
            String[] names = required(fields, "options", id).split("\\|", -1);
            if (names.length < 2 || names.length > 32) throw new IOException("Choice count is invalid for " + id);
            Set<String> unique = new HashSet<>();
            for (String name : names) {
                String choice = name.trim();
                if (choice.isEmpty() || choice.length() > 64 || hasControl(choice)
                        || !unique.add(choice.toLowerCase(Locale.ROOT))) {
                    throw new IOException("Invalid or duplicate choice for " + id);
                }
                choices.add(choice);
            }
        } else if (type.equals(TEXT)) {
            maxLength = integer(optional(fields, "max_length", "256"), 1, 1024, "text length");
        }
        Option option = new Option(id, type, label, description, group, "", order,
                minimum, maximum, step, maxLength, Collections.unmodifiableList(choices));
        defaultValue = option.normalize(required(fields, "default", id));
        return new Option(id, type, label, description, group, defaultValue, order,
                minimum, maximum, step, maxLength, option.choices);
    }

    static LinkedHashMap<String, String> loadValues(File directory) throws IOException {
        LinkedHashMap<String, String> result = new LinkedHashMap<>();
        if (Files.isSymbolicLink(directory.toPath())) throw new IOException("Mod folder must not be a symbolic link");
        File file = new File(directory, FILE_NAME);
        if (Files.isSymbolicLink(file.toPath())) throw new IOException("Mod settings must not be a symbolic link");
        if (!file.exists()) return result;
        if (!file.isFile() || file.length() > MAX_VALUE_FILE_BYTES) throw new IOException("Invalid mod settings file");
        try (BufferedReader reader = Files.newBufferedReader(file.toPath(), StandardCharsets.UTF_8)) {
            String line;
            int number = 0;
            while ((line = reader.readLine()) != null) {
                ++number;
                if (number == 1 && line.startsWith("\ufeff")) line = line.substring(1);
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) continue;
                int equals = line.indexOf('=');
                if (equals < 1) throw new IOException("Invalid mod settings line " + number);
                String key = line.substring(0, equals).trim().toLowerCase(Locale.ROOT);
                String value = line.substring(equals + 1).trim();
                if (!safeId(key) || value.length() > 4096 || hasControl(value)) {
                    throw new IOException("Invalid mod settings line " + number);
                }
                if (result.put(key, value) != null) throw new IOException("Duplicate mod setting " + key);
                if (result.size() > MAX_VALUES) throw new IOException("Too many saved mod settings");
            }
        }
        return result;
    }

    static synchronized void saveValues(File directory, List<Option> options, Map<String, String> changes) throws IOException {
        LinkedHashMap<String, String> values = loadValues(directory);
        for (Option option : options) {
            String proposed = changes.containsKey(option.id) ? changes.get(option.id) : option.defaultValue;
            values.put(option.id, option.normalize(proposed));
        }
        File target = new File(directory, FILE_NAME);
        File temporary = new File(directory, TEMP_NAME);
        if (Files.isSymbolicLink(target.toPath()) || Files.isSymbolicLink(temporary.toPath())) {
            throw new IOException("Mod settings must not be symbolic links");
        }
        if ((temporary.exists() || Files.isSymbolicLink(temporary.toPath())) && !temporary.delete()) {
            throw new IOException("Could not replace temporary mod settings");
        }
        boolean complete = false;
        try {
            StringBuilder output = new StringBuilder("# Written by ProtoDroid. Values apply on the next Play.\n");
            for (Map.Entry<String, String> entry : values.entrySet()) {
                if (!safeId(entry.getKey()) || hasControl(entry.getValue())) {
                    throw new IOException("Unsafe saved mod setting");
                }
                output.append(entry.getKey()).append('=').append(entry.getValue()).append('\n');
            }
            String serialized = output.toString();
            if (serialized.getBytes(StandardCharsets.UTF_8).length > MAX_VALUE_FILE_BYTES) {
                throw new IOException("Mod settings exceed the 128 KiB limit");
            }
            try (FileOutputStream stream = new FileOutputStream(temporary);
                 OutputStreamWriter writer = new OutputStreamWriter(stream, StandardCharsets.UTF_8)) {
                writer.write(serialized);
                writer.flush();
                stream.getFD().sync();
            }
            try {
                Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException error) {
                Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
            complete = true;
        } finally {
            if (!complete) temporary.delete();
        }
    }

    private static String required(Map<String, String> fields, String key, String id) throws IOException {
        if (!fields.containsKey(key)) throw new IOException("Missing '" + key + "' for mod option " + id);
        return fields.get(key).trim();
    }

    private static String optional(Map<String, String> fields, String key, String fallback) {
        String value = fields.get(key);
        return value == null ? fallback : value;
    }

    private static int integer(String value, int minimum, int maximum, String label) throws IOException {
        try {
            int result = Integer.parseInt(value);
            if (result < minimum || result > maximum) throw new NumberFormatException();
            return result;
        } catch (NumberFormatException error) {
            throw new IOException("Invalid " + label);
        }
    }

    private static double number(String value, String label) throws IOException {
        try {
            double result = Double.parseDouble(value);
            if (!finite(result)) throw new NumberFormatException();
            return result;
        } catch (NumberFormatException error) {
            throw new IOException("Invalid slider " + label);
        }
    }

    private static boolean parseBoolean(String value) throws IOException {
        switch (value.toLowerCase(Locale.ROOT)) {
            case "1": case "true": case "on": case "yes": return true;
            case "0": case "false": case "off": case "no": return false;
            default: throw new IOException("Expected an On/Off value");
        }
    }

    private static String decimal(double value) {
        if (!finite(value)) throw new IllegalArgumentException("Non-finite mod option");
        return BigDecimal.valueOf(value).stripTrailingZeros().toPlainString();
    }

    private static boolean finite(double value) {
        return !Double.isNaN(value) && !Double.isInfinite(value);
    }

    private static boolean safeId(String id) {
        return id != null && id.matches("[a-z0-9][a-z0-9._-]{0,47}");
    }

    private static boolean hasControl(String value) {
        for (int i = 0; i < value.length(); ++i) if (Character.isISOControl(value.charAt(i))) return true;
        return false;
    }
}
