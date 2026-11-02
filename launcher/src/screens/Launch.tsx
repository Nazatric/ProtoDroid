import { useEffect } from "react";
import { Backdrop, Logo } from "../components/ui";
import { ART } from "../lib/art";
import { launchEngine } from "../lib/bridge";
import { toast } from "../lib/data";
import { sfx } from "../lib/sfx";

export default function Launch({ onDone }: { onDone: () => void }) {
  useEffect(() => {
    if (!launchEngine()) {
      toast("Play requires the Android app and detected game files", "warn");
      onDone();
      return;
    }
    sfx.launch();
  }, [onDone]);

  useEffect(() => {
    const resumed = () => onDone();
    window.addEventListener("protodroid:resume", resumed);
    return () => window.removeEventListener("protodroid:resume", resumed);
  }, [onDone]);

  return (
    <div data-screen className="relative h-full w-full overflow-hidden bg-ink">
      <Backdrop img={ART.launch} dim={0.35} tendrils={0.8} fade="none" position="center 40%" />
      <div className="relative z-10 flex h-full flex-col items-center justify-center gap-[4vh] px-8">
        <Logo size="clamp(32px,10.5vh,76px)" className="anim-flicker" />
        <div className="w-full max-w-md">
          <div className="h-1.5 w-full overflow-hidden bg-white/10">
            <div className="launch-indeterminate h-full w-1/3 bg-blood-hi" />
          </div>
          <div className="mt-2 text-center font-ui text-[11px] uppercase tracking-[0.18em] text-ash">
            Handing off to the native ARM64 game
          </div>
        </div>
      </div>
    </div>
  );
}
