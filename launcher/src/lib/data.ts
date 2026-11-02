import { createStore } from "./store";
import type { GameSnapshot } from "./types";

export type GameDataState = GameSnapshot & { setupDone: boolean };

export const dataStore = createStore<GameDataState>(null, {
  access: false,
  gameFound: false,
  checking: true,
  setupDone: false,
  status: "Checking game files",
  gamePath: "",
  sharedPath: "",
  appPath: "",
  alternatePath: "",
  dll: false,
  uv: false,
});

export const appStore = createStore(null, {
  loaded: false,
  versionName: "…",
  tourArmed: false,
});

export const isReady = (data: Pick<GameDataState, "gameFound">) => data.gameFound;

type Tone = "ok" | "warn";
export const toastStore = createStore(null, { msg: "", tone: "ok" as Tone, id: 0 });
let toastTimer: number | undefined;
export function toast(msg: string, tone: Tone = "ok") {
  const id = Date.now();
  toastStore.set({ msg, tone, id });
  clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => toastStore.set({ msg: "" }), 2400);
}
