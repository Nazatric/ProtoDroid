import type { UpdateSnapshot } from "./types";

export interface TrackedUpdate extends UpdateSnapshot {
  upToDate: boolean;
}

/** A hidden card alone is not evidence of a successful check. */
export function mergeUpdate(previous: TrackedUpdate, next: UpdateSnapshot): TrackedUpdate {
  const checked = previous.status === "checking" && next.status === "hidden";
  const checking = next.status === "checking";
  const changed = next.status === "available" || next.status === "downloading" || next.status === "ready";
  return { ...next, upToDate: checked || (!checking && !changed && previous.upToDate) };
}
