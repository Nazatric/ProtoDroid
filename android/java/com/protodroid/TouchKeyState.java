package com.protodroid;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Reference-counted ownership of touch-injected keys, independent of Android for host-side tests. */
final class TouchKeyState {
    interface Sink {
        void send(int keycode, boolean down);
    }

    private final Sink sink;
    private final Map<Integer, Integer> counts = new HashMap<>();
    private final List<Integer> acquisitionOrder = new ArrayList<>();

    TouchKeyState(Sink sink) {
        this.sink = sink;
    }

    void set(int keycode, boolean down) {
        if (keycode <= 0) return;
        Integer old = counts.get(keycode);
        int count = old == null ? 0 : old;
        if (down) {
            counts.put(keycode, count + 1);
            if (count == 0) {
                acquisitionOrder.add(keycode);
                sink.send(keycode, true);
            }
            return;
        }
        if (count == 0) return;
        if (count == 1) {
            counts.remove(keycode);
            acquisitionOrder.remove(Integer.valueOf(keycode));
            sink.send(keycode, false);
        } else {
            counts.put(keycode, count - 1);
        }
    }

    boolean isDown(int keycode) {
        return counts.containsKey(keycode);
    }

    int owners(int keycode) {
        Integer count = counts.get(keycode);
        return count == null ? 0 : count;
    }

    void releaseAll() {
        for (int i = acquisitionOrder.size() - 1; i >= 0; --i) {
            sink.send(acquisitionOrder.get(i), false);
        }
        acquisitionOrder.clear();
        counts.clear();
    }
}
