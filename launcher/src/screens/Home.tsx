import { useState } from "react";
import { UpdateNotice } from "../components/UpdateNotice";
import { AppIcon, Backdrop, Btn, Logo, Panel, Tag } from "../components/ui";
import { BACKGROUND_INTERVAL_MS, ART } from "../lib/art";
import { cancelDiagnosticTour, logoTap } from "../lib/bridge";
import { appStore, dataStore, isReady } from "../lib/data";
import { sfx } from "../lib/sfx";
import type { Destination } from "../App";

const MENU: { id: Destination; label: string; note: string }[] = [
  { id: "launch", label: "PLAY", note: "Start the native game" },
  { id: "settings", label: "SETTINGS", note: "Graphics, game video, controls & fixes" },
  { id: "drivers", label: "DRIVERS", note: "Validated Vulkan driver library" },
  { id: "mods", label: "MODS", note: "Imports, textures, options & plugins" },
  { id: "logs", label: "LOGS", note: "Native, DXVK & diagnostic records" },
  { id: "about", label: "ABOUT", note: "Version, device & updates" },
];

export default function Home({ go }: { go: (to: Destination) => void }) {
  const data = dataStore.use();
  const app = appStore.use();
  const [focus, setFocus] = useState(0);
  const ready = isReady(data);
  const status = app.tourArmed
    ? ready ? "TOUR ARMED · NEXT PLAY RUNS NATIVE DIAGNOSTICS" : "TOUR ARMED · GAME FILES REQUIRED"
    : data.checking ? "CHECKING GAME FILES"
      : ready ? "GAME READY"
        : data.dll && !data.uv ? "MISSING PROTO_UV.DAT"
          : data.access ? "GAME FILES NOT FOUND" : "SHARED FILE ACCESS NOT GRANTED";

  const pick = (index: number) => {
    sfx.select();
    const item = MENU[index];
    go(item.id === "launch" && app.tourArmed ? "launch" : item.id);
  };

  return (
    <div data-screen className="relative h-full w-full overflow-hidden">
      <Backdrop img={ART.home} interval={BACKGROUND_INTERVAL_MS} dim={0.55} tendrils={0.25} position="center 35%" />

      <div className="relative z-10 flex h-full flex-col justify-between px-[max(24px,5vw)] py-[max(14px,3.5vh)]">
        <div className="flex items-start justify-between gap-3">
          <div className="flex items-center gap-3">
            <AppIcon size={44} onClick={logoTap} className="rounded-lg" />
            <Logo size="clamp(30px,9.5vh,76px)" className="anim-logo-in" />
          </div>
          <div className="hidden items-center gap-2 sm:flex">
            <Tag tone={ready ? "ok" : data.dll ? "red" : "ash"}>{ready ? "Ready" : "Setup"}</Tag>
            <span className="font-mono text-[10px] uppercase tracking-widest text-ash">v{app.versionName}</span>
          </div>
        </div>

        <main className="flex min-h-0 flex-1 items-center gap-[5vw] py-2">
          <nav className="flex w-[min(46vw,390px)] shrink-0 flex-col gap-[clamp(0px,0.8vh,5px)]" aria-label="Launcher">
            {MENU.map((item, i) => (
              <button
                key={item.id}
                data-nav
                data-on={focus === i}
                onFocus={() => setFocus(i)}
                onMouseEnter={() => setFocus(i)}
                onClick={() => pick(i)}
                className="menu-item anim-rise min-h-[clamp(32px,7.4vh,54px)] px-1 font-ui text-[clamp(20px,6.2vh,46px)] font-medium uppercase leading-none tracking-[0.09em] text-bone/75"
                style={{ animationDelay: `${i * 55 + 120}ms` }}
              >
                <span className="tick font-mono text-[0.4em] text-blood-hi">▶</span>
                <span>{item.id === "launch" && app.tourArmed ? "START TOUR" : item.label}</span>
              </button>
            ))}
          </nav>

          <div className="anim-fade hidden min-w-0 flex-1 self-center md:block" style={{ animationDelay: "300ms" }}>
            <div className="mb-2 h-px w-16 bg-blood" />
            <div className="safe-wrap font-ui text-[clamp(13px,3.2vh,20px)] uppercase tracking-[0.18em] text-bone">
              {MENU[focus].label === "PLAY" && app.tourArmed ? "START TOUR" : MENU[focus].label}
            </div>
            <p className="safe-wrap mt-1 max-w-md text-[clamp(11px,2.4vh,14px)] leading-relaxed text-ash">
              {MENU[focus].id === "launch" && app.tourArmed
                ? "Enter gameplay and run the real automated diagnostic sequence"
                : MENU[focus].note}
            </p>
            <div className="mt-4 max-w-lg">
              <div className="flex items-center gap-2">
                <span className={`h-1.5 w-1.5 ${ready ? "bg-green-400" : "bg-blood-hi"}`} />
                <span className="safe-wrap font-mono text-[10px] uppercase tracking-[0.12em] text-ash">{status}</span>
              </div>
              {app.tourArmed && (
                <Panel className="anim-fade mt-3 p-2.5">
                  <div className="safe-wrap font-ui text-xs uppercase tracking-[0.14em] text-blood-hi">Native automated tour</div>
                  <p className="mt-1 text-[11px] leading-snug text-ash">Sequential gameplay tests, persistent logs, then app close.</p>
                  <Btn small className="mt-2" onClick={cancelDiagnosticTour}>Disarm tour</Btn>
                </Panel>
              )}
              <UpdateNotice className="mt-3" />
            </div>
          </div>
        </main>

        <div className="md:hidden"><UpdateNotice /></div>
        <footer className="mt-1 flex items-end justify-between gap-3 font-mono text-[9px] uppercase tracking-[0.14em] text-white/35">
          <span className="safe-wrap max-w-[65%] truncate">{data.gamePath || data.sharedPath || data.appPath || "Native Android port"}</span>
          <span className="shrink-0 sm:hidden">v{app.versionName}</span>
          <span className="hidden shrink-0 sm:inline">Original game files & saves stay user-owned</span>
        </footer>
      </div>
    </div>
  );
}
