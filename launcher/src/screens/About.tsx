import type { Destination } from "../App";
import { UpdateNotice } from "../components/UpdateNotice";
import { Backdrop, Btn, Header, HintBar, Panel, Tag } from "../components/ui";
import { ART } from "../lib/art";
import { checkUpdates, openReleases } from "../lib/bridge";
import { appStore, dataStore, toast } from "../lib/data";
import { deviceStore } from "../lib/device";
import { updateStore } from "../lib/update";

export default function About({ go, onBack }: { go: (to: Destination) => void; onBack: () => void }) {
  const app = appStore.use();
  const device = deviceStore.use();
  const data = dataStore.use();
  const update = updateStore.use();
  const busy = update.status === "checking" || update.status === "downloading" || update.status === "verifying";

  const check = () => {
    if (!checkUpdates()) toast("Update checks require the Android app", "warn");
  };

  const info = [
    ["Device", device.model],
    ["OS", device.os],
    ["SoC", device.cpu],
    ["GPU family", device.gpu],
    ["ABI", device.abi],
    ["RAM", device.ram],
    ["Screen", device.screen],
    ["Game", data.gamePath || "Not detected"],
    ["Android build", device.nativeBuild],
  ];

  return (
    <div data-screen className="relative flex h-full w-full flex-col">
      <Backdrop img={ART.about} dim={0.72} />
      <Header title="ABOUT" onBack={onBack} icon right={<Tag>v{app.versionName}</Tag>} />
      <main className="scroll-y relative z-10 min-h-0 flex-1 px-[max(14px,env(safe-area-inset-left))] py-3 pr-[max(14px,env(safe-area-inset-right))]">
        <div className="mx-auto grid max-w-5xl gap-3 md:grid-cols-2">
          <section>
            <div className="mb-2 font-ui text-xs uppercase tracking-[0.22em] text-blood-hi">Updates</div>
            <UpdateNotice />
            {!update.visible && (
              <Panel className="p-3">
                <div className="font-ui text-sm uppercase tracking-[0.12em] text-white">
                  {update.upToDate ? "No newer release found" : "Official update checks"}
                </div>
                <p className="mt-1 text-xs leading-relaxed text-ash">
                  {update.upToDate
                    ? `The latest GitHub release is not newer than this installation (v${app.versionName}).`
                    : "Check the official Nazatric/ProtoDroid releases. Available updates open their exact release page; recovered downloads are verified before installation."}
                </p>
              </Panel>
            )}
            <div className="mt-2 flex flex-wrap gap-2">
              <Btn small variant="primary" disabled={busy} onClick={check}>{update.status === "checking" ? "Checking…" : "Check now"}</Btn>
              <Btn small onClick={openReleases}>All versions</Btn>
            </div>
            <p className="mt-2 max-w-md text-[11px] leading-relaxed text-ash">
              Previous releases remain available for recovery. Shared game files and saves are never bundled with an update.
            </p>
          </section>

          <section>
            <div className="mb-2 font-ui text-xs uppercase tracking-[0.22em] text-blood-hi">This installation</div>
            <Panel className="p-3">
              <dl className="grid grid-cols-[92px_1fr] gap-x-3 gap-y-1.5 font-mono text-[11px]">
                {info.map(([label, value]) => (
                  <div key={label} className="contents">
                    <dt className="uppercase text-ash">{label}</dt>
                    <dd className="safe-wrap text-bone">{value}</dd>
                  </div>
                ))}
              </dl>
            </Panel>
            <Btn small className="mt-2" onClick={() => go("logs")}>Open real logs</Btn>
          </section>
        </div>

        <Panel className="mx-auto mt-3 max-w-5xl p-3 text-[11px] leading-relaxed text-ash">
          ProtoDroid is an unofficial ARM64 Android port launcher. Prototype and its artwork belong to their respective owners.
          Game data is supplied by the user; the app does not include or modify the original installation.
        </Panel>
      </main>
      <HintBar text="Updates and release downloads are handled by Android, not simulated by the launcher." onBack={onBack} />
    </div>
  );
}
