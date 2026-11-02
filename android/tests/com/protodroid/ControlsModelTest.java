package com.protodroid;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class ControlsModelTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void catalog() {
        check(ControlKeys.all().size() == 104, "standard PC keyboard must have 104 keys");
        Set<Integer> codes = new HashSet<>();
        Set<String> names = new HashSet<>();
        for (ControlKeys.Key key : ControlKeys.all()) {
            check(key.code > 0 && codes.add(key.code), "duplicate/invalid keycode " + key.code);
            check(names.add(key.name), "duplicate key name " + key.name);
            check(!key.shortName.isEmpty(), "empty key label");
        }
        check(ControlKeys.KEY_0 == 7 && ControlKeys.A == 29 && ControlKeys.Z == 54, "AOSP basic keycodes");
        check(ControlKeys.F1 == 131 && ControlKeys.F12 == 142 && ControlKeys.NUM_LOCK == 143,
                "AOSP function/numpad keycodes");
        check(ControlKeys.NUMPAD_ENTER == 160 && ControlKeys.PRINT_SCREEN == 120, "AOSP special keycodes");
        check(ControlKeys.isSupported(ControlKeys.UP) && ControlKeys.isSupported(ControlKeys.MENU),
                "cheat arrows and Application Menu missing");
        check(ControlKeys.isModifier(ControlKeys.CTRL_LEFT) && !ControlKeys.isModifier(ControlKeys.CAPS_LOCK),
                "modifier classification");
        check("LCTRL+C".equals(ControlKeys.chordLabel(ControlKeys.CTRL_LEFT, ControlKeys.C)), "chord label");
        check(!CustomButtonStore.validBinding(0, 0), "unbound button accepted");
        check(!CustomButtonStore.validBinding(ControlKeys.C, ControlKeys.C), "duplicate chord accepted");
        check(CustomButtonStore.validBinding(ControlKeys.C, 0), "single key rejected");
        check(CustomButtonStore.validBinding(ControlKeys.CTRL_LEFT, ControlKeys.C), "two-key chord rejected");
    }

    private static void persistence() {
        List<CustomButtonStore.Entry> input = new ArrayList<>();
        CustomButtonStore.Entry one = new CustomButtonStore.Entry(7, ControlKeys.E, 0);
        one.x = 0.25f;
        one.y = 0.75f;
        one.size = 1.4f;
        one.hidden = true;
        input.add(one);
        CustomButtonStore.Entry two = new CustomButtonStore.Entry(9, ControlKeys.CTRL_LEFT, ControlKeys.C);
        input.add(two);
        List<CustomButtonStore.Entry> output = CustomButtonStore.decode(CustomButtonStore.encode(input));
        check(output.size() == 2, "round-trip record count");
        CustomButtonStore.Entry restored = output.get(0);
        check(restored.id == 7 && restored.key1 == ControlKeys.E && restored.key2 == 0, "round-trip binding/id");
        check(restored.x == 0.25f && restored.y == 0.75f && restored.size == 1.4f && restored.hidden,
                "round-trip geometry/hidden state");
        check(output.get(1).key2 == ControlKeys.C && !output.get(1).hidden, "round-trip chord");

        check(CustomButtonStore.decode("not json").isEmpty(), "malformed JSON must be safe");
        check(CustomButtonStore.decode("[null,{},1]").isEmpty(), "wrong JSON records must be safe");
        String mixed = "[{\"id\":1,\"key1\":31,\"key2\":0},"
                + "{\"id\":1,\"key1\":32,\"key2\":0},"
                + "{\"id\":2,\"key1\":9999,\"key2\":0},"
                + "{\"id\":3,\"key1\":31,\"key2\":31},"
                + "{\"id\":4,\"key1\":33,\"x\":4,\"y\":-2,\"size\":99}]";
        List<CustomButtonStore.Entry> safe = CustomButtonStore.decode(mixed);
        check(safe.size() == 2, "duplicate IDs and invalid bindings were not filtered");
        check(safe.get(1).id == 4 && safe.get(1).x == -1 && safe.get(1).y == -1 && safe.get(1).size == 2,
                "geometry was not sanitized");

        List<CustomButtonStore.Entry> tooMany = new ArrayList<>();
        for (int i = 1; i <= CustomButtonStore.MAX_BUTTONS + 5; ++i) {
            tooMany.add(new CustomButtonStore.Entry(i, ControlKeys.A, 0));
        }
        check(CustomButtonStore.decode(CustomButtonStore.encode(tooMany)).size() == CustomButtonStore.MAX_BUTTONS,
                "custom-button limit not enforced");
    }

    private static void keyOwnership() {
        List<String> events = new ArrayList<>();
        TouchKeyState state = new TouchKeyState((key, down) -> events.add((down ? "+" : "-") + key));
        state.set(ControlKeys.CTRL_LEFT, true);
        state.set(ControlKeys.C, true);
        state.set(ControlKeys.C, true);  // another touch control owns C too
        check(events.size() == 2, "duplicate physical keydown emitted");
        state.set(ControlKeys.C, false);
        check(state.isDown(ControlKeys.C) && state.owners(ControlKeys.C) == 1 && events.size() == 2,
                "one owner released another owner's key");
        state.set(ControlKeys.CTRL_LEFT, false);
        state.set(ControlKeys.C, false);
        state.set(ControlKeys.C, false);
        check(events.toString().equals("[+113, +31, -113, -31]"), "unbalanced/chord cleanup: " + events);

        events.clear();
        state.set(ControlKeys.SHIFT_LEFT, true);
        state.set(ControlKeys.A, true);
        state.set(ControlKeys.CTRL_LEFT, true);
        state.releaseAll();
        check(events.toString().equals("[+59, +29, +113, -113, -29, -59]"),
                "global release is not balanced reverse cleanup: " + events);
        check(!state.isDown(ControlKeys.A) && state.owners(ControlKeys.SHIFT_LEFT) == 0, "cleanup retained keys");
        state.releaseAll();
        check(events.size() == 6, "second global cleanup emitted duplicate keyups");
    }

    public static void main(String[] args) {
        catalog();
        persistence();
        keyOwnership();
        System.out.println("104-key catalog, custom-button persistence, and touch-key ownership tests passed");
    }
}
