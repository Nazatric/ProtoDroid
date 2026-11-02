import { useEffect, useState } from "react";
import { Backdrop, Btn, Header, HintBar } from "../components/ui";
import { cn } from "../utils/cn";
import { ART } from "../lib/art";
import { copyText, readLog } from "../lib/bridge";
import { toast } from "../lib/data";
import { sfx } from "../lib/sfx";
import type { LogEntry } from "../lib/types";

const FILES = [
  { id: "native", name: "proto-native.log" },
  { id: "prev", name: "proto-native.prev.log" },
  { id: "dxvk", name: "app_process64_d3d9.log" },
  { id: "tour", name: "tour/proto-native.log" },
  { id: "journal", name: "tour/journal.log" },
  { id: "result", name: "tour/result.txt" },
];

export default function Logs({ onBack }: { onBack: () => void }) {
  const [file, setFile] = useState("native");
  const [revision, setRevision] = useState(0);
  const [entry, setEntry] = useState<LogEntry | null>(null);
  const selected = FILES.find((item) => item.id === file) ?? FILES[0];

  useEffect(() => {
    setEntry(readLog(selected.id, selected.name));
  }, [selected.id, selected.name, revision]);

  const copy = async () => {
    if (!entry?.content) return;
    const copied = await copyText(entry.content);
    toast(copied ? "Log copied" : "Clipboard unavailable", copied ? "ok" : "warn");
  };

  return (
    <div data-screen className="relative flex h-full w-full flex-col">
      <Backdrop img={ART.logs} dim={0.8} />
      <Header
        title="LOGS"
        onBack={onBack}
        icon
        right={
          <>
            <Btn small onClick={() => setRevision(revision + 1)}>Refresh</Btn>
            <Btn small disabled={!entry?.content} onClick={() => void copy()}>Copy</Btn>
          </>
        }
      />
      <main className="relative z-10 flex min-h-0 flex-1">
        <nav className="scroll-y w-[clamp(150px,25vw,230px)] shrink-0 border-r border-white/8 bg-black/50 py-1 pl-[env(safe-area-inset-left)]">
          {FILES.map((item) => (
            <button
              key={item.id}
              data-nav
              onFocus={() => setFile(item.id)}
              onClick={() => (sfx.tick(), setFile(item.id))}
              className={cn(
                "nav-row safe-wrap block w-full min-h-11 px-3 py-2 text-left font-mono text-[11px]",
                file === item.id ? "bg-blood/20 text-white" : "text-bone/65"
              )}
            >
              {item.name}
            </button>
          ))}
        </nav>
        <pre className="scroll-y anim-fade min-w-0 flex-1 select-text whitespace-pre-wrap break-words bg-black/55 px-4 py-3 pr-[max(14px,env(safe-area-inset-right))] font-mono text-[11px] leading-[1.6] text-bone/80">
          {entry?.error || entry?.content || "Reading log…"}
        </pre>
      </main>
      <HintBar
        text={entry?.truncated ? `Showing the last 512 KiB · ${entry.path}` : entry?.path || "Logs are read directly and never cleared by this screen."}
        onBack={onBack}
      />
    </div>
  );
}
