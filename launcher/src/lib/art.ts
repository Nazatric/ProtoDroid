import { LOCAL_ASSETS } from "./localAssets";

/** One editable catalogue for the complete no-gameplay cinematic background set. */
const wall = (name: string) => LOCAL_ASSETS[`wall-${name}` as keyof typeof LOCAL_ASSETS];
const intro = (name: string) => LOCAL_ASSETS[`intro-${name}` as keyof typeof LOCAL_ASSETS];

export const CINEMATIC_WALLPAPERS = [
  wall("mercer-city"),
  wall("sunset-helicopters"),
  wall("blacklight-portrait"),
  wall("biomass-title"),
  wall("mercer-urban"),
  wall("ashen-profile"),
  wall("viral-title"),
  wall("claw-white"),
  wall("mercer-silhouette"),
  wall("mercer-fog"),
  wall("mercer-cityline"),
  wall("white-biomass"),
  wall("quarantine-city"),
  wall("manhattan-smoke"),
];

/** Still frames sampled from the pre-rendered Prototype intro cinematic. */
export const BINK_CINEMATIC_STILLS = [
  intro("03"),
  intro("06"),
  intro("09"),
  intro("11"),
  intro("12"),
  intro("13"),
];

export const ART = {
  icon: LOCAL_ASSETS.icon,
  boot: [wall("mercer-city"), wall("blacklight-portrait"), wall("biomass-title")],
  home: [...CINEMATIC_WALLPAPERS, ...BINK_CINEMATIC_STILLS],
  setup: [wall("sunset-helicopters"), wall("quarantine-city")],
  settings: [wall("ashen-profile"), wall("manhattan-smoke"), wall("mercer-cityline")],
  drivers: [wall("blacklight-portrait"), wall("quarantine-city")],
  mods: [wall("biomass-title"), wall("mercer-silhouette")],
  logs: [wall("manhattan-smoke")],
  about: [wall("mercer-urban"), wall("sunset-helicopters")],
  launch: [...BINK_CINEMATIC_STILLS, wall("mercer-city"), wall("blacklight-portrait")],
  game: BINK_CINEMATIC_STILLS[2],
  layout: wall("mercer-city"),
  stills: [
    wall("mercer-city"),
    wall("blacklight-portrait"),
    wall("biomass-title"),
    wall("sunset-helicopters"),
    wall("ashen-profile"),
    wall("mercer-urban"),
  ],
};

/** Change the rotation cadence here; all scene lists above are local data URLs. */
export const BACKGROUND_INTERVAL_MS = 10_000;