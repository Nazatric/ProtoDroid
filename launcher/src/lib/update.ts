import { createStore } from "./store";
import type { TrackedUpdate } from "./updatePolicy";

export type UpdateState = TrackedUpdate;
export { mergeUpdate } from "./updatePolicy";

export const updateStore = createStore<UpdateState>(null, {
  status: "hidden",
  visible: false,
  canPress: false,
  versionName: "",
  progress: -1,
  downloadedBytes: 0,
  totalBytes: 0,
  detail: "",
  requiresUninstall: false,
  warningVersion: "",
  upToDate: false,
});
