import { useEffect, useState } from "react";
import { AppIcon, Backdrop, Logo } from "../components/ui";
import { ART } from "../lib/art";
import { appStore, dataStore } from "../lib/data";
import { sfx } from "../lib/sfx";

export default function Boot({ onDone }: { onDone: () => void }) {
  const data = dataStore.use();
  const app = appStore.use();
  const [count, setCount] = useState(0);
  const [ready, setReady] = useState(false);
  const lines = [
    `protodroid ${app.versionName} · arm64`,
    "renderer · DXVK / Vulkan",
    !app.loaded ? "game data · checking" : data.gameFound ? "game data · found" : "game data · not ready",
  ];

  useEffect(() => {
    if (count < lines.length) {
      const timer = window.setTimeout(() => {
        setCount(count + 1);
        sfx.type();
      }, count === 0 ? 260 : 220);
      return () => clearTimeout(timer);
    }
    const timer = window.setTimeout(() => {
      setReady(true);
      sfx.impact();
    }, 360);
    return () => clearTimeout(timer);
  }, [count, lines.length]);

  const start = () => {
    sfx.unlock();
    if (!ready) {
      setCount(lines.length);
      setReady(true);
      return;
    }
    sfx.select();
    onDone();
  };

  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      if (["Enter", " ", "a", "A"].includes(event.key)) start();
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  });

  return (
    <div data-screen data-primary-action className="relative h-full w-full cursor-pointer overflow-hidden bg-ink" onClick={start}>
      <Backdrop img={ART.boot} dim={0.12} fade="none" tendrils={0.7} />
      <div className="absolute left-[max(18px,env(safe-area-inset-left))] top-4 space-y-0.5 font-mono text-[clamp(9px,2.4vh,12px)] text-ash">
        {lines.slice(0, count).map((line, index) => (
          <div key={index} className={index === 0 ? "text-blood-hi" : line.includes("not ready") ? "text-amber-400" : ""}>{line}</div>
        ))}
      </div>
      <div className="absolute right-[max(18px,env(safe-area-inset-right))] top-4 flex items-center gap-2 opacity-80">
        <AppIcon size={24} className="rounded-md" />
        <span className="font-ui text-[10px] uppercase tracking-[0.24em] text-bone/70">ProtoDroid</span>
      </div>
      <div className="absolute inset-0 flex flex-col items-center justify-center">
        <Logo size="clamp(40px,14vh,104px)" className="anim-logo-in" />
        <div className="anim-fade mt-3 font-ui text-[clamp(9px,2.3vh,12px)] uppercase tracking-[0.55em] text-ash" style={{ animationDelay: "400ms" }}>
          Native Android Port
        </div>
        {ready && (
          <div className="anim-fade mt-[7vh] flex items-center gap-3 font-ui text-[clamp(11px,3.2vh,15px)] uppercase tracking-[0.45em] text-bone">
            <span className="h-px w-8 bg-blood-hi" />
            <span className="anim-blink">Tap to start</span>
            <span className="h-px w-8 bg-blood-hi" />
          </div>
        )}
      </div>
    </div>
  );
}
