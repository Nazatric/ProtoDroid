package com.protodroid;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Android keycodes for the standard 104-key PC keyboard, kept independent of Android for model tests. */
final class ControlKeys {
    static final int UNKNOWN = 0;
    static final int KEY_0 = 7, KEY_1 = 8, KEY_2 = 9, KEY_3 = 10, KEY_4 = 11;
    static final int KEY_5 = 12, KEY_6 = 13, KEY_7 = 14, KEY_8 = 15, KEY_9 = 16;
    static final int UP = 19, DOWN = 20, LEFT = 21, RIGHT = 22;
    static final int A = 29, B = 30, C = 31, D = 32, E = 33, F = 34, G = 35;
    static final int H = 36, I = 37, J = 38, K = 39, L = 40, M = 41, N = 42;
    static final int O = 43, P = 44, Q = 45, R = 46, S = 47, T = 48, U = 49;
    static final int V = 50, W = 51, X = 52, Y = 53, Z = 54;
    static final int COMMA = 55, PERIOD = 56, ALT_LEFT = 57, ALT_RIGHT = 58;
    static final int SHIFT_LEFT = 59, SHIFT_RIGHT = 60, TAB = 61, SPACE = 62;
    static final int ENTER = 66, BACKSPACE = 67, GRAVE = 68, MINUS = 69, EQUALS = 70;
    static final int LEFT_BRACKET = 71, RIGHT_BRACKET = 72, BACKSLASH = 73;
    static final int SEMICOLON = 74, APOSTROPHE = 75, SLASH = 76, MENU = 82;
    static final int PAGE_UP = 92, PAGE_DOWN = 93;
    static final int ESCAPE = 111, DELETE = 112, CTRL_LEFT = 113, CTRL_RIGHT = 114;
    static final int CAPS_LOCK = 115, SCROLL_LOCK = 116, META_LEFT = 117, META_RIGHT = 118;
    static final int PRINT_SCREEN = 120, PAUSE = 121, HOME = 122, END = 123, INSERT = 124;
    static final int F1 = 131, F2 = 132, F3 = 133, F4 = 134, F5 = 135, F6 = 136;
    static final int F7 = 137, F8 = 138, F9 = 139, F10 = 140, F11 = 141, F12 = 142;
    static final int NUM_LOCK = 143;
    static final int NUMPAD_0 = 144, NUMPAD_1 = 145, NUMPAD_2 = 146, NUMPAD_3 = 147;
    static final int NUMPAD_4 = 148, NUMPAD_5 = 149, NUMPAD_6 = 150, NUMPAD_7 = 151;
    static final int NUMPAD_8 = 152, NUMPAD_9 = 153, NUMPAD_DIVIDE = 154;
    static final int NUMPAD_MULTIPLY = 155, NUMPAD_SUBTRACT = 156, NUMPAD_ADD = 157;
    static final int NUMPAD_PERIOD = 158, NUMPAD_ENTER = 160;

    static final class Key {
        final int code;
        final String name, shortName;

        Key(int code, String name, String shortName) {
            this.code = code;
            this.name = name;
            this.shortName = shortName;
        }
    }

    private static final Map<Integer, Key> BY_CODE = new LinkedHashMap<>();
    private static final List<Key> ALL;

    private static void key(int code, String name, String shortName) {
        if (code <= 0 || BY_CODE.put(code, new Key(code, name, shortName)) != null) {
            throw new ExceptionInInitializerError("duplicate/invalid control key " + code);
        }
    }

    static {
        key(ESCAPE, "Escape", "ESC");
        for (int i = 0; i < 12; ++i) key(F1 + i, "F" + (i + 1), "F" + (i + 1));
        key(PRINT_SCREEN, "Print Screen", "PRTSC");
        key(SCROLL_LOCK, "Scroll Lock", "SCRLOK");
        key(PAUSE, "Pause", "PAUSE");

        key(GRAVE, "Grave / tilde", "`");
        int[] numberCodes = {KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0};
        for (int i = 0; i < numberCodes.length; ++i) key(numberCodes[i], "Number " + ((i + 1) % 10),
                String.valueOf((i + 1) % 10));
        key(MINUS, "Minus / underscore", "-");
        key(EQUALS, "Equals / plus", "=");
        key(BACKSPACE, "Backspace", "BACKSPACE");
        key(INSERT, "Insert", "INS");
        key(HOME, "Home", "HOME");
        key(PAGE_UP, "Page Up", "PGUP");
        key(TAB, "Tab", "TAB");
        String[] qwerty = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"};
        int[] qwertyCodes = {Q, W, E, R, T, Y, U, I, O, P};
        for (int i = 0; i < qwerty.length; ++i) key(qwertyCodes[i], qwerty[i], qwerty[i]);
        key(LEFT_BRACKET, "Left bracket", "[");
        key(RIGHT_BRACKET, "Right bracket", "]");
        key(BACKSLASH, "Backslash / pipe", "\\");
        key(DELETE, "Delete", "DEL");
        key(END, "End", "END");
        key(PAGE_DOWN, "Page Down", "PGDN");

        key(CAPS_LOCK, "Caps Lock", "CAPS");
        String[] home = {"A", "S", "D", "F", "G", "H", "J", "K", "L"};
        int[] homeCodes = {A, S, D, F, G, H, J, K, L};
        for (int i = 0; i < home.length; ++i) key(homeCodes[i], home[i], home[i]);
        key(SEMICOLON, "Semicolon / colon", ";");
        key(APOSTROPHE, "Apostrophe / quote", "'");
        key(ENTER, "Enter", "ENTER");

        key(SHIFT_LEFT, "Left Shift", "LSHIFT");
        String[] lower = {"Z", "X", "C", "V", "B", "N", "M"};
        int[] lowerCodes = {Z, X, C, V, B, N, M};
        for (int i = 0; i < lower.length; ++i) key(lowerCodes[i], lower[i], lower[i]);
        key(COMMA, "Comma / less than", ",");
        key(PERIOD, "Period / greater than", ".");
        key(SLASH, "Slash / question", "/");
        key(SHIFT_RIGHT, "Right Shift", "RSHIFT");
        key(UP, "Up Arrow", "UP");

        key(CTRL_LEFT, "Left Control", "LCTRL");
        key(META_LEFT, "Left Windows", "LWIN");
        key(ALT_LEFT, "Left Alt", "LALT");
        key(SPACE, "Space", "SPACE");
        key(ALT_RIGHT, "Right Alt", "RALT");
        key(META_RIGHT, "Right Windows", "RWIN");
        key(MENU, "Application Menu", "MENU");
        key(CTRL_RIGHT, "Right Control", "RCTRL");
        key(LEFT, "Left Arrow", "LEFT");
        key(DOWN, "Down Arrow", "DOWN");
        key(RIGHT, "Right Arrow", "RIGHT");

        key(NUM_LOCK, "Num Lock", "NUM LOCK");
        key(NUMPAD_DIVIDE, "Numpad Divide", "NUM /");
        key(NUMPAD_MULTIPLY, "Numpad Multiply", "NUM *");
        key(NUMPAD_SUBTRACT, "Numpad Minus", "NUM -");
        for (int i = 0; i < 10; ++i) key(NUMPAD_0 + i, "Numpad " + i, "NUM " + i);
        key(NUMPAD_ADD, "Numpad Plus", "NUM +");
        key(NUMPAD_ENTER, "Numpad Enter", "NUM ENT");
        key(NUMPAD_PERIOD, "Numpad Decimal", "NUM .");

        ALL = Collections.unmodifiableList(new ArrayList<>(BY_CODE.values()));
        if (ALL.size() != 104) throw new ExceptionInInitializerError("PC key catalog is " + ALL.size());
    }

    private ControlKeys() {}

    static List<Key> all() {
        return ALL;
    }

    static boolean isSupported(int code) {
        return BY_CODE.containsKey(code);
    }

    static boolean isModifier(int code) {
        return code == SHIFT_LEFT || code == SHIFT_RIGHT || code == CTRL_LEFT || code == CTRL_RIGHT
                || code == ALT_LEFT || code == ALT_RIGHT || code == META_LEFT || code == META_RIGHT;
    }

    static String nameOf(int code) {
        Key key = BY_CODE.get(code);
        return key == null ? "UNBOUND" : key.name;
    }

    static String shortNameOf(int code) {
        Key key = BY_CODE.get(code);
        return key == null ? "?" : key.shortName;
    }

    /** Familiar keycap text; binding labels retain their more specific L/R names. */
    static String keycap(int code) {
        if (code >= NUMPAD_0 && code <= NUMPAD_9) return String.valueOf(code - NUMPAD_0);
        switch (code) {
            case ESCAPE:
                return "ESC";
            case DELETE:
                return "DEL";
            case BACKSPACE:
                return "BACKSPACE";
            case SCROLL_LOCK:
                return "SCROLL";
            case CAPS_LOCK:
                return "CAPS";
            case SHIFT_LEFT:
            case SHIFT_RIGHT:
                return "SHIFT";
            case CTRL_LEFT:
            case CTRL_RIGHT:
                return "CTRL";
            case ALT_LEFT:
            case ALT_RIGHT:
                return "ALT";
            case META_LEFT:
            case META_RIGHT:
                return "WIN";
            case UP:
                return "UP";
            case DOWN:
                return "DOWN";
            case LEFT:
                return "LEFT";
            case RIGHT:
                return "RIGHT";
            case NUM_LOCK:
                return "NUM";
            case NUMPAD_DIVIDE:
                return "/";
            case NUMPAD_MULTIPLY:
                return "*";
            case NUMPAD_SUBTRACT:
                return "-";
            case NUMPAD_ADD:
                return "+";
            case NUMPAD_PERIOD:
                return ".";
            case NUMPAD_ENTER:
                return "ENTER";
            default:
                return shortNameOf(code);
        }
    }

    static String chordLabel(int first, int second) {
        if (!isSupported(first)) return "NEW";
        return shortNameOf(first) + (isSupported(second) ? "+" + shortNameOf(second) : "");
    }
}
