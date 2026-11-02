import { createStore } from "./store";
import type { DeviceSnapshot } from "./types";

/** Replaced by Android build/device data as soon as the host snapshot arrives. */
export const deviceStore = createStore<DeviceSnapshot>(null, {
  model: "Browser preview",
  os: "Android host not connected",
  cpu: "—",
  gpu: "—",
  abi: "—",
  ram: "—",
  screen: `${Math.round(screen.width * devicePixelRatio)}×${Math.round(screen.height * devicePixelRatio)}`,
  nativeBuild: "—",
});
