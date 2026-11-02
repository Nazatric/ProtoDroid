import { Backdrop, Btn, Header, HintBar, Panel, Tag } from "../components/ui";
import { ART } from "../lib/art";
import { refreshGameData, requestFileAccess } from "../lib/bridge";
import { dataStore, isReady, toast } from "../lib/data";

export default function Setup({ onDone, onBack }: { onDone: () => void; onBack: () => void }) {
  const data = dataStore.use();
  const ready = isReady(data);

  const allowAccess = () => {
    if (!requestFileAccess()) toast("File access is available in the Android app", "warn");
  };
  const checkAgain = () => {
    if (!refreshGameData()) toast("Game detection requires the Android app", "warn");
  };
  const continueToLauncher = () => {
    dataStore.set({ setupDone: true });
    onDone();
  };

  return (
    <div data-screen className="relative flex h-full w-full flex-col">
      <Backdrop img={ART.setup} dim={0.35} position="55% 45%" />
      <Header title="SETUP" onBack={onBack} icon />
      <main className="scroll-y relative z-10 min-h-0 flex-1 px-[max(16px,env(safe-area-inset-left))] py-3 pr-[max(16px,env(safe-area-inset-right))]">
        <Panel className="anim-fade mx-auto max-w-3xl p-[clamp(14px,3vw,24px)]">
          <div className="flex flex-wrap items-center gap-2">
            <Tag tone={ready ? "ok" : "red"}>{ready ? "Game data ready" : "Game data incomplete"}</Tag>
            <Tag tone={data.access ? "ok" : "ash"}>{data.access ? "Shared access granted" : "Shared access optional for app-local data"}</Tag>
          </div>
          <h2 className="mt-3 font-ui text-[clamp(20px,5vh,30px)] font-medium uppercase tracking-[0.08em] text-white">
            {ready ? "Ready to launch" : data.dll && !data.uv ? "Missing proto_uv.dat" : "Locate your PC game files"}
          </h2>
          <p className="safe-wrap mt-2 max-w-2xl text-[clamp(12px,2.4vh,15px)] leading-relaxed text-bone/75">
            {data.status}. ProtoDroid checks both locations without copying or changing the original game.
          </p>

          <div className="mt-4 grid gap-2 md:grid-cols-2">
            <div className="frame p-3">
              <div className="font-ui text-xs uppercase tracking-[0.16em] text-blood-hi">Shared folder</div>
              <div className="safe-wrap mt-1 font-mono text-[11px] text-bone">{data.sharedPath || "/storage/emulated/0/Prototype"}</div>
              {data.alternatePath && <div className="safe-wrap mt-1 font-mono text-[10px] text-ash">Also: {data.alternatePath}</div>}
            </div>
            <div className="frame p-3">
              <div className="font-ui text-xs uppercase tracking-[0.16em] text-blood-hi">App-specific folder</div>
              <div className="safe-wrap mt-1 font-mono text-[11px] text-bone">{data.appPath || "Android/data/com.protodroid/files/game"}</div>
            </div>
          </div>

          <div className="mt-3 flex flex-wrap gap-2 font-mono text-[11px]">
            <Tag tone={data.dll ? "ok" : "red"}>prototypeenginef.dll · {data.dll ? "found" : "missing"}</Tag>
            <Tag tone={data.uv ? "ok" : "red"}>proto_uv.dat · {data.uv ? "found" : "missing"}</Tag>
          </div>
          {data.gamePath && <div className="safe-wrap mt-3 font-mono text-[10px] text-ash">Using: {data.gamePath}</div>}

          <div className="mt-4 flex flex-wrap gap-2">
            {!data.access && <Btn onClick={allowAccess}>Allow shared access</Btn>}
            <Btn variant={ready ? "primary" : "ghost"} onClick={checkAgain}>Check again</Btn>
            {(ready || data.access) && <Btn variant="primary" onClick={continueToLauncher}>Continue</Btn>}
          </div>
        </Panel>
      </main>
      <HintBar text="Required game files remain read-only; saves stay in the existing shared location." onBack={onBack} />
    </div>
  );
}
