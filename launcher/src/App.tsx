import { useCallback, useEffect, useRef, useState, type ReactNode } from "react";
import About from "./screens/About";
import Boot from "./screens/Boot";
import Home from "./screens/Home";
import Launch from "./screens/Launch";
import Logs from "./screens/Logs";
import Settings from "./screens/Settings";
import Setup from "./screens/Setup";
import { Btn, Modal, Toast } from "./components/ui";
import { confirmUpdate, dismissUpdateWarning, installHostBridge, openNativeScreen, type NativeScreen } from "./lib/bridge";
import { dataStore, toast } from "./lib/data";
import { installNav } from "./lib/nav";
import { settingsStore } from "./lib/settings";
import { updateStore } from "./lib/update";

export type Screen = "boot" | "setup" | "home" | "settings" | "logs" | "about" | "launch";
export type Destination = Screen | NativeScreen;

export default function App() {
  const [stack, setStack] = useState<Screen[]>(["boot"]);
  const [wipe, setWipe] = useState(0);
  const [portrait, setPortrait] = useState(false);
  const [ignorePortrait, setIgnorePortrait] = useState(false);
  const busy = useRef(false);
  const screen = stack[stack.length - 1];
  const update = updateStore.use();

  useEffect(() => installNav(), []);
  useEffect(() => installHostBridge(), []);

  useEffect(() => {
    const mq = window.matchMedia("(orientation: portrait) and (max-width: 820px)");
    const changed = () => setPortrait(mq.matches);
    changed();
    mq.addEventListener("change", changed);
    return () => mq.removeEventListener("change", changed);
  }, []);

  const transition = useCallback((fn: (stack: Screen[]) => Screen[]) => {
    if (busy.current) return;
    if (settingsStore.get().animations === "off") {
      setStack(fn);
      return;
    }
    busy.current = true;
    setWipe(Date.now());
    window.setTimeout(() => setStack(fn), 260);
    window.setTimeout(() => {
      setWipe(0);
      busy.current = false;
    }, 620);
  }, []);

  const go = useCallback(
    (to: Destination) => {
      if (to === "drivers" || to === "mods" || to === "layout") {
        if (!openNativeScreen(to)) toast("This screen is available in the Android app", "warn");
        return;
      }
      if (to === "launch" && !dataStore.get().gameFound) {
        toast("Game files are required", "warn");
        transition((stack) => [...stack, "setup"]);
        return;
      }
      transition((stack) => [...stack, to]);
    },
    [transition]
  );
  const replace = useCallback((to: Screen) => transition((stack) => [...stack.slice(0, -1), to]), [transition]);
  const back = useCallback(() => transition((stack) => (stack.length > 1 ? stack.slice(0, -1) : ["home"])), [transition]);
  const home = useCallback(() => transition(() => ["home"]), [transition]);

  let view: ReactNode;
  switch (screen) {
    case "boot":
      view = <Boot onDone={() => replace(dataStore.get().setupDone || dataStore.get().gameFound ? "home" : "setup")} />;
      break;
    case "setup":
      view = <Setup onDone={home} onBack={home} />;
      break;
    case "home":
      view = <Home go={go} />;
      break;
    case "settings":
      view = <Settings go={go} onBack={back} />;
      break;
    case "logs":
      view = <Logs onBack={back} />;
      break;
    case "about":
      view = <About go={go} onBack={back} />;
      break;
    case "launch":
      view = <Launch onDone={home} />;
      break;
  }

  return (
    <div className="relative h-full w-full overflow-hidden bg-ink">
      <div key={screen} className="h-full w-full">{view}</div>
      <div className="grain" aria-hidden />
      <div className="scanlines" aria-hidden />
      {wipe > 0 && (
        <div key={wipe} className="wipe" aria-hidden><i /><i /></div>
      )}
      <Toast />

      {update.warningVersion && (
        <Modal
          title="Signing certificate changed"
          onClose={dismissUpdateWarning}
          actions={
            <>
              <Btn onClick={dismissUpdateWarning}>Cancel</Btn>
              <Btn variant="danger" onClick={confirmUpdate}>Uninstall & install</Btn>
            </>
          }
        >
          <p>
            ProtoDroid {update.warningVersion} is official, but its signing certificate differs from this installation.
            Android must remove the old app before installing it.
          </p>
          <p className="mt-3 text-bone/70">
            Shared game files and saves in /Prototype stay in place. Private settings may reset. The verified APK remains in Downloads.
          </p>
        </Modal>
      )}

      {portrait && !ignorePortrait && (
        <div className="fixed inset-0 z-[90] flex flex-col items-center justify-center gap-6 bg-ink p-8 text-center">
          <div className="anim-rotate relative h-24 w-14 rounded-lg border-2 border-bone/80">
            <div className="absolute inset-x-4 bottom-1.5 h-0.5 rounded bg-blood-hi" />
          </div>
          <div>
            <div className="font-dirt text-3xl text-bone">ROTATE DEVICE</div>
            <div className="mt-2 font-ui text-[11px] uppercase tracking-[0.28em] text-ash">Landscape works best</div>
          </div>
          <Btn variant="plain" small onClick={() => setIgnorePortrait(true)}>Continue</Btn>
        </div>
      )}
    </div>
  );
}
