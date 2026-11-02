export type UpdatePhase = "hidden" | "checking" | "available" | "downloading" | "verifying" | "ready" | "error";

export interface GameSnapshot {
  access: boolean;
  gameFound: boolean;
  checking: boolean;
  status: string;
  gamePath: string;
  sharedPath: string;
  appPath: string;
  alternatePath: string;
  dll: boolean;
  uv: boolean;
}

export interface SettingDefinition {
  key: string;
  label: string;
  help: string;
  options: string[];
  index: number;
}

export interface SettingCategory {
  id: string;
  name: string;
  settings: SettingDefinition[];
}

export interface SettingsSnapshot {
  preset: number;
  categories: SettingCategory[];
}

export interface UpdateSnapshot {
  status: UpdatePhase;
  visible: boolean;
  canPress: boolean;
  versionName: string;
  progress: number;
  downloadedBytes: number;
  totalBytes: number;
  detail: string;
  requiresUninstall: boolean;
  warningVersion: string;
}

export interface DeviceSnapshot {
  model: string;
  os: string;
  cpu: string;
  gpu: string;
  abi: string;
  ram: string;
  screen: string;
  nativeBuild: string;
}

export interface HostSnapshot {
  versionName: string;
  tourArmed: boolean;
  motion: "full" | "off";
  game: GameSnapshot;
  settings: SettingsSnapshot;
  update: UpdateSnapshot;
  device: DeviceSnapshot;
}

export interface LogEntry {
  name: string;
  path: string;
  content: string;
  truncated: boolean;
  error: string;
}
