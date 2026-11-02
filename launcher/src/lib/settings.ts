import { createStore } from "./store";
import type { SettingCategory } from "./types";

/** Authoritative rows arrive from GameSettings; the web UI never maintains a second INI schema. */
export const settingsStore = createStore(null, {
  preset: 3,
  categories: [] as SettingCategory[],
  animations: "full" as "full" | "off",
});
