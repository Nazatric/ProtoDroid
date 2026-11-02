/** The only surface exposed to the offline document. Every operation is validated by the Android host. */
export interface ProtoDroidHost {
  getSnapshot(): string;
  play(): void;
  exit(): void;
  requestFileAccess(): void;
  checkGameData(): void;
  openDrivers(): void;
  openMods(): void;
  openTouchLayout(): void;
  setSetting(key: string, index: number): void;
  applyPreset(preset: number): void;
  resetSettings(): void;
  checkUpdates(): void;
  updateNow(): void;
  openReleases(): void;
  confirmUpdate(): void;
  dismissUpdateWarning(): void;
  logoTap(): void;
  cancelDiagnosticTour(): void;
  readLog(id: string): string;
  copyText(text: string): void;
}

declare global {
  interface Window {
    ProtoDroidHost?: ProtoDroidHost;
    protodroidHostChanged?: (snapshot: import("./types").HostSnapshot) => void;
    protodroidHostResumed?: () => void;
    protodroidBack?: () => void;
    protodroidActivate?: () => void;
  }
}

export const nativeHost = () => (typeof window === "undefined" ? undefined : window.ProtoDroidHost);
export const hasHost = () => !!nativeHost();
export const hostName = () => (hasHost() ? "Android host" : "Browser preview");
