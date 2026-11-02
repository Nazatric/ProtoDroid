import { updateNow } from "../lib/bridge";
import { updateStore } from "../lib/update";
import { Btn, Panel, Progress, Tag } from "./ui";

function title(status: string, version: string) {
  const suffix = version ? ` ${version}` : "";
  switch (status) {
    case "checking": return "Checking for updates";
    case "available": return `Update${suffix} available`;
    case "downloading": return `Downloading update${suffix}`;
    case "verifying": return `Verifying update${suffix}`;
    case "ready": return `Update${suffix} ready to install`;
    case "error": return "Update needs attention";
    default: return "Updates";
  }
}

export function UpdateNotice({ className = "" }: { className?: string }) {
  const update = updateStore.use();
  if (!update.visible) return null;
  const action = update.status === "available" ? "Open release" : update.status === "ready" ? "Install" : update.status === "error" ? "Retry" : "";
  return (
    <Panel className={`anim-fade p-2.5 ${className}`}>
      <div className="flex items-center gap-2">
        <Tag tone={update.status === "error" || update.status === "available" ? "red" : update.status === "ready" ? "ok" : "ash"}>
          Update
        </Tag>
        <span className="safe-wrap min-w-0 flex-1 font-ui text-[clamp(12px,2.7vh,15px)] uppercase tracking-[0.1em] text-white">
          {title(update.status, update.versionName)}
        </span>
        {action && update.canPress && <Btn small variant="primary" onClick={updateNow}>{action}</Btn>}
      </div>
      {update.detail && <div className="safe-wrap mt-1 text-[11px] leading-snug text-ash">{update.detail}</div>}
      {(update.status === "downloading" || update.status === "verifying") && update.progress >= 0 && (
        <div className="mt-2"><Progress value={update.progress} stripes={update.status === "downloading"} /></div>
      )}
    </Panel>
  );
}
