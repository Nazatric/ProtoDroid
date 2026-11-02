import { useSyncExternalStore } from "react";

/** Tiny persistent store. localStorage-backed, fully offline. */
export function createStore<T extends object>(key: string | null, init: T, merge = true) {
  let state: T = init;
  if (key) {
    try {
      const raw = localStorage.getItem(key);
      if (raw) {
        const parsed = JSON.parse(raw);
        state = merge && !Array.isArray(init) ? { ...init, ...parsed } : parsed;
      }
    } catch {
      /* corrupted storage → defaults */
    }
  }
  const subs = new Set<() => void>();
  const subscribe = (fn: () => void) => {
    subs.add(fn);
    return () => subs.delete(fn);
  };
  const get = () => state;
  const set = (patch: Partial<T> | ((s: T) => Partial<T> | T)) => {
    const p = typeof patch === "function" ? patch(state) : patch;
    state = Array.isArray(state) ? (p as T) : { ...state, ...p };
    if (key) {
      try {
        localStorage.setItem(key, JSON.stringify(state));
      } catch {
        /* quota */
      }
    }
    subs.forEach((f) => f());
  };
  const reset = () => set(() => init);
  const use = () => useSyncExternalStore(subscribe, get, get);
  return { get, set, reset, subscribe, use };
}
