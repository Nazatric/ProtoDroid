import { appStore, dataStore, toast } from "./data";
import { deviceStore } from "./device";
import { focusables, goBack } from "./nav";
import { nativeHost } from "./native";
import { settingsStore } from "./settings";
import type { HostSnapshot, LogEntry } from "./types";
import { mergeUpdate, updateStore } from "./update";

function applySnapshot(snapshot: HostSnapshot) {
  const current = dataStore.get();
  dataStore.set({ ...snapshot.game, setupDone: current.setupDone || snapshot.game.gameFound });
  appStore.set({ loaded: true, versionName: snapshot.versionName, tourArmed: snapshot.tourArmed });
  settingsStore.set({
    preset: snapshot.settings.preset,
    categories: snapshot.settings.categories,
    animations: snapshot.motion,
  });
  deviceStore.set(snapshot.device);
  updateStore.set(mergeUpdate(updateStore.get(), snapshot.update));
  document.documentElement.dataset.motion = snapshot.motion;
}

export function installHostBridge() {
  window.protodroidHostChanged = applySnapshot;
  window.protodroidHostResumed = () => window.dispatchEvent(new Event("protodroid:resume"));
  window.protodroidBack = () => {
    if (!goBack()) exitApp();
  };
  window.protodroidActivate = () => {
    const active = document.activeElement as HTMLElement | null;
    if (active?.matches("button, [role=button], a")) active.click();
    else {
      const primary = document.querySelector<HTMLElement>("[data-primary-action]");
      if (primary) primary.click();
      else focusables()[0]?.focus({ preventScroll: true });
    }
  };

  const raw = nativeHost()?.getSnapshot();
  if (raw) {
    try {
      applySnapshot(JSON.parse(raw) as HostSnapshot);
    } catch {
      toast("Could not read Android launcher state", "warn");
    }
  }
  return () => {
    delete window.protodroidHostChanged;
    delete window.protodroidHostResumed;
    delete window.protodroidBack;
    delete window.protodroidActivate;
  };
}

export function launchEngine() {
  const host = nativeHost();
  if (!host) return false;
  host.play();
  return true;
}

export function exitApp() {
  nativeHost()?.exit();
}

export function requestFileAccess() {
  const host = nativeHost();
  if (!host) return false;
  host.requestFileAccess();
  return true;
}

export function refreshGameData() {
  const host = nativeHost();
  if (!host) return false;
  host.checkGameData();
  return true;
}

export type NativeScreen = "drivers" | "mods" | "layout";
export function openNativeScreen(screen: NativeScreen) {
  const host = nativeHost();
  if (!host) return false;
  if (screen === "drivers") host.openDrivers();
  else if (screen === "mods") host.openMods();
  else host.openTouchLayout();
  return true;
}

export function setSetting(key: string, index: number) {
  const host = nativeHost();
  if (!host) return false;
  host.setSetting(key, index);
  return true;
}

export function applyPreset(preset: number) {
  const host = nativeHost();
  if (!host) return false;
  host.applyPreset(preset);
  return true;
}

export function resetSettings() {
  const host = nativeHost();
  if (!host) return false;
  host.resetSettings();
  return true;
}

export function checkUpdates() {
  const host = nativeHost();
  if (!host) return false;
  host.checkUpdates();
  return true;
}

export function updateNow() {
  const host = nativeHost();
  if (!host) return false;
  host.updateNow();
  return true;
}

export function openReleases() {
  const host = nativeHost();
  if (!host) return false;
  host.openReleases();
  return true;
}

export function confirmUpdate() {
  nativeHost()?.confirmUpdate();
}

export function dismissUpdateWarning() {
  nativeHost()?.dismissUpdateWarning();
  updateStore.set({ warningVersion: "" });
}

export function logoTap() {
  const host = nativeHost();
  if (!host) return false;
  host.logoTap();
  return true;
}

export function cancelDiagnosticTour() {
  nativeHost()?.cancelDiagnosticTour();
}

export function readLog(id: string, name: string): LogEntry {
  const host = nativeHost();
  if (!host) return { name, path: "", content: "", truncated: false, error: "Logs are available in the Android app" };
  try {
    return JSON.parse(host.readLog(id)) as LogEntry;
  } catch {
    return { name, path: "", content: "", truncated: false, error: "Could not read this log" };
  }
}

export async function copyText(text: string) {
  const host = nativeHost();
  if (host) {
    host.copyText(text);
    return true;
  }
  if (!navigator.clipboard) return false;
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    return false;
  }
}
