import assert from "node:assert/strict";
import { readFileSync, readdirSync, statSync } from "node:fs";
import { join } from "node:path";
import test from "node:test";

const root = new URL("../../", import.meta.url).pathname;
const read = (path) => readFileSync(join(root, path), "utf8");
const activity = read("android/java/com/protodroid/SetupActivity.java");
const view = read("android/java/com/protodroid/LauncherView.java");
const native = read("launcher/src/lib/native.ts");

const methods = [
  "getSnapshot", "play", "exit", "requestFileAccess", "checkGameData", "openDrivers", "openMods",
  "openTouchLayout", "setSetting", "applyPreset", "resetSettings", "checkUpdates", "updateNow",
  "openReleases", "confirmUpdate", "dismissUpdateWarning", "logoTap", "cancelDiagnosticTour", "readLog", "copyText",
];

test("web and Android bridges expose the same validated operations", () => {
  for (const method of methods) {
    assert.match(native, new RegExp(`\\b${method}\\s*\\(`), `web bridge lacks ${method}`);
    assert.match(view, new RegExp(`@JavascriptInterface public (?:String|void) ${method}\\s*\\(`), `Android bridge lacks ${method}`);
  }
  assert.doesNotMatch(native, /writeIni|saveFile|deleteFile/);
  assert.match(view, /setBlockNetworkLoads\(true\)/);
  assert.match(view, /setAllowUniversalAccessFromFileURLs\(false\)/);
});

test("SetupActivity retains native launch, driver, mod, layout, and diagnostic behavior", () => {
  assert.match(activity, /new LauncherView/);
  assert.match(activity, /new DriverView/);
  assert.match(activity, /ModsActivity\.class/);
  assert.match(activity, /TouchLayoutActivity\.class/);
  assert.match(activity, /PROTODROID_DIAGNOSTIC_TOUR/);
  assert.match(activity, /PROTODROID_CUSTOM_DRIVER/);
  assert.match(activity, /startActivity\(new Intent\(this, ProtoActivity\.class\)\)/);
});

test("demo screens and fake update/tour behavior are absent", () => {
  const app = read("launcher/src/App.tsx");
  const home = read("launcher/src/screens/Home.tsx");
  const about = read("launcher/src/screens/About.tsx");
  const launch = read("launcher/src/screens/Launch.tsx");
  for (const removed of ["screens/Drivers", "screens/Mods", "screens/Layout", "screens/Game"]) {
    assert.ok(!app.includes(removed), `stale demo import: ${removed}`);
  }
  assert.match(app, /openNativeScreen/);
  assert.match(home, /logoTap/);
  assert.match(home, /UpdateNotice/);
  assert.match(about, /checkUpdates/);
  assert.doesNotMatch(about, /setTimeout|Math\.random/);
  assert.doesNotMatch(launch, /setTimeout|setInterval|Math\.random|\[boot\]|\[engine\]/);
  assert.match(launch, /launchEngine\(\)/);
  const settings = read("launcher/src/screens/Settings.tsx");
  assert.match(settings, /setting\.key === "touch_layout"/);
  assert.match(settings, /go\("layout"\)/);
  assert.match(settings, /active\?\.settings\.map/);
});

test("no fictional package/settings database remains in active source", () => {
  const files = [];
  const walk = (directory) => {
    for (const name of readdirSync(directory)) {
      const path = join(directory, name);
      if (statSync(path).isDirectory()) walk(path);
      else if (/\.(ts|tsx)$/.test(name) && name !== "localAssets.ts") files.push(path);
    }
  };
  walk(join(root, "launcher/src"));
  const source = files.map((path) => readFileSync(path, "utf8")).join("\n");
  for (const forbidden of ["DEFAULT_MODS", "CATALOG", "driverStore", "modsStore", "layoutStore", "0.9.2", "writeIni", "dxvkStateCacheSize"]) {
    assert.ok(!source.includes(forbidden), `stale demo state: ${forbidden}`);
  }
});

test("Android asset is one offline document with embedded art and no demo version", () => {
  const html = read("android/assets/launcher/index.html");
  assert.ok(html.length > 1_000_000, "embedded artwork/fonts are missing");
  assert.ok(html.includes("ProtoDroidHost"), "native bridge is absent from the bundle");
  assert.ok(/data:image\/(?:jpeg|webp|png);base64,/.test(html), "image data URLs are absent");
  assert.ok(!/<script[^>]+src\s*=/i.test(html), "an external script would break offline startup");
  assert.ok(!/0\.9\.2|Ultra Smash|Reality Shatter|Turnip v2[45]-custom/.test(html), "demo content reached the bundle");
});
