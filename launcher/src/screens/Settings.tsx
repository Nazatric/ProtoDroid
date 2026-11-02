import { useState } from "react";
import type { Destination } from "../App";
import { Backdrop, Btn, Header, HintBar, Modal, Panel, Tag } from "../components/ui";
import { cn } from "../utils/cn";
import { applyPreset, resetSettings, setSetting } from "../lib/bridge";
import { toast } from "../lib/data";
import { settingsStore } from "../lib/settings";
import { sfx } from "../lib/sfx";
import { ART } from "../lib/art";
import type { SettingDefinition } from "../lib/types";

const PRESETS = ["LOW", "MEDIUM", "HIGH", "CUSTOM"];

export default function Settings({ go, onBack }: { go: (to: Destination) => void; onBack: () => void }) {
  const state = settingsStore.use();
  const [tab, setTab] = useState("graphics");
  const [help, setHelp] = useState<SettingDefinition | null>(null);
  const [armReset, setArmReset] = useState(false);
  const active = state.categories.find((category) => category.id === tab) ?? state.categories[0];

  const change = (setting: SettingDefinition, delta: number) => {
    if (setting.key === "touch_layout") {
      go("layout");
      return;
    }
    const next = (setting.index + delta + setting.options.length) % setting.options.length;
    if (!setSetting(setting.key, next)) toast("Settings require the Android host", "warn");
  };

  const reset = () => {
    if (!armReset) {
      setArmReset(true);
      sfx.error();
      window.setTimeout(() => setArmReset(false), 3500);
      return;
    }
    if (resetSettings()) {
      setArmReset(false);
      sfx.impact();
      toast("Launcher settings reset; touch layout preserved");
    }
  };

  return (
    <div data-screen className="relative flex h-full w-full flex-col">
      <Backdrop img={ART.settings} dim={0.65} />
      <Header
        title="SETTINGS"
        onBack={onBack}
        icon
        right={
          <Btn small variant={armReset ? "danger" : "plain"} onClick={reset}>
            {armReset ? "Confirm reset" : "Reset all"}
          </Btn>
        }
      />

      <div className="relative z-10 flex shrink-0 gap-1 overflow-x-auto border-b border-white/8 bg-black/45 px-[max(10px,env(safe-area-inset-left))] pr-[max(10px,env(safe-area-inset-right))] pt-1.5">
        {state.categories.map((category) => (
          <button
            key={category.id}
            data-nav
            onClick={() => (sfx.tick(), setTab(category.id))}
            className={cn(
              "btn relative min-h-10 shrink-0 px-[clamp(10px,2vw,22px)] font-ui text-[clamp(11px,2.8vh,14px)] uppercase tracking-[0.15em]",
              active?.id === category.id ? "text-white" : "text-ash hover:text-bone"
            )}
          >
            {category.name}
            {active?.id === category.id && <span className="absolute inset-x-2 bottom-0 h-0.5 bg-blood-hi" />}
          </button>
        ))}
      </div>

      <main className="scroll-y relative z-10 min-h-0 flex-1 px-[max(14px,env(safe-area-inset-left))] py-3 pr-[max(14px,env(safe-area-inset-right))]">
        <div className="mx-auto max-w-5xl">
          {active?.id === "graphics" && (
            <Panel className="anim-fade mb-3 flex flex-wrap items-center gap-2 p-2.5">
              <span className="mr-2 font-ui text-xs uppercase tracking-[0.2em] text-ash">Graphics preset</span>
              {PRESETS.map((name, index) => (
                <button
                  key={name}
                  data-nav
                  disabled={index === 3}
                  onClick={() => index < 3 && (sfx.select(), applyPreset(index))}
                  className={cn(
                    "btn min-h-11 border px-4 font-ui text-xs uppercase tracking-[0.16em]",
                    state.preset === index ? "border-blood-hi bg-blood text-white" : "border-white/15 text-ash hover:text-white",
                    index === 3 && state.preset !== 3 && "hidden"
                  )}
                >
                  {name}
                </button>
              ))}
              <span className="ml-auto text-[11px] text-ash">Presets leave FPS and controls unchanged</span>
            </Panel>
          )}

          <div className="grid gap-2 lg:grid-cols-2">
            {active?.settings.map((setting, index) => (
              <div key={setting.key} className="frame anim-rise flex min-h-14 items-center gap-2 p-2 pl-3" style={{ animationDelay: `${index * 30}ms` }}>
                <div className="min-w-0 flex-1">
                  <div className="safe-wrap font-ui text-[clamp(13px,3vh,16px)] uppercase tracking-[0.08em] text-bone">{setting.label}</div>
                  <div className="safe-wrap font-mono text-[10px] text-ash">
                    {setting.key === "touch_layout" ? "Full native editor" : setting.options[setting.index]}
                  </div>
                </div>
                <button
                  data-nav
                  aria-label={`About ${setting.label}`}
                  onClick={() => setHelp(setting)}
                  className="btn grid h-11 w-11 shrink-0 place-items-center border border-white/10 font-mono text-xs text-ash hover:text-white"
                >
                  i
                </button>
                {setting.key === "touch_layout" ? (
                  <Btn small variant="primary" onClick={() => go("layout")}>Open editor</Btn>
                ) : (
                  <div className="flex shrink-0 items-center gap-1">
                    <button data-nav aria-label={`Previous ${setting.label}`} onClick={() => change(setting, -1)} className="btn grid h-11 w-11 place-items-center border border-white/15 text-lg text-white">‹</button>
                    <button data-nav aria-label={`Next ${setting.label}`} onClick={() => change(setting, 1)} className="btn grid h-11 w-11 place-items-center border border-white/15 text-lg text-white">›</button>
                  </div>
                )}
              </div>
            ))}
          </div>
          {!active && <Panel className="p-5 text-sm text-ash">Waiting for Android settings…</Panel>}
        </div>
      </main>

      <HintBar text="Changes save automatically. Engine options use the existing protodroid.ini writer." onBack={onBack} />

      {help && (
        <Modal title={help.label} onClose={() => setHelp(null)} actions={<Btn onClick={() => setHelp(null)}>Close</Btn>}>
          <p className="leading-relaxed">{help.help}</p>
          <div className="mt-3 flex flex-wrap gap-1.5">
            {help.options.map((option, index) => <Tag key={option} tone={index === help.index ? "ok" : "ash"}>{option}</Tag>)}
          </div>
        </Modal>
      )}
    </div>
  );
}
