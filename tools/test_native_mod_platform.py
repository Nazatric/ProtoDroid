#!/usr/bin/env python3
"""Source/signature tests for the optional native mod platform; no device-validation claim."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def text(path: str) -> str:
    return (ROOT / path).read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


spec = importlib.util.spec_from_file_location("patch_native_recomp", ROOT / "tools/patch_native_recomp.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
part = "build/recomp/native/part_0089.c"
patched = text(part)
require(patched.count(module.NEW) == 1 and module.OLD not in patched, "committed tick gate is not patched exactly once")
require("0x3a98u" in patched and "7c897bf8: jb" in patched, "original tick compare/evidence comment was lost")

with tempfile.TemporaryDirectory(prefix="protodroid-tick-test-") as directory:
    root = Path(directory)
    original = patched.replace(module.NEW, module.OLD)
    (root / "part_0089.c").write_text(original)
    for _ in range(2):
        subprocess.run(["python3", str(ROOT / "tools/patch_native_recomp.py"), str(root)], check=True,
                       stdout=subprocess.PIPE, text=True)
    require((root / "part_0089.c").read_text() == patched, "regenerated patch is not exact/idempotent")
    (root / "part_0089.c").write_text(original + "\n" + module.OLD + "\n")
    failed = subprocess.run(["python3", str(ROOT / "tools/patch_native_recomp.py"), str(root)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    require(failed.returncode != 0 and "expected exactly one tick gate" in failed.stderr,
            "ambiguous tick gate was not refused")

runtime = text("src/recomp/runtime.h")
bridge = text("src/recomp/bridge_x86.cpp")
native_runtime = text("src/native/runtime.cpp")
require("int RecompRemoveTickRateLimit(void);" in runtime, "generated policy declaration missing")
require('RecompRemoveTickRateLimit(void) { return 0; }' in bridge, "x86 bridge does not preserve original tick policy")
require('SettingInt("remove_tick_rate_limit", 0)' in native_runtime, "native tick policy is not default-off")

features = text("src/native/mod_features.cpp")
require('Setting("fov_offset", "0")' in features and 'std::strtof' in features,
        "FOV must preserve finite fractional offsets with a safe zero default")
for setting in ('SettingInt("shadow_map_resolution", 0)',
                'SettingInt("unlock_dev_menu", 0)', 'SettingInt("prefer_loose_files", 0)'):
    require(setting in features or setting in text("src/native/mod_files.cpp"), f"unsafe feature default: {setting}")
feature_policy = text("src/native/mod_feature_policy.h")
for value in (512, 1024, 2048, 4096, 8192):
    require(f"value == {value}" in feature_policy, f"documented shadow override {value} is missing")
require("ModScaleShadowResolution" in features and "4096.0" not in features,
        "shadow detour does not use the complete bounded scaling policy")
main_source = text("src/native/main.cpp")
bink_source = text("src/native/bink.cpp")
require('SettingInt("unlock_menu_fps", 1)' in main_source and
        'BinkMenuWaitUnlocked(MenuFpsUnlocked() ? 1 : 0, audio_streams)' in bink_source,
        "default-On live menu FPS unlock or Bink-open consumption changed")
require('Java_com_protodroid_TouchControls_nativeSetMenuFpsUnlocked' in main_source,
        "live menu FPS JNI setter is missing")
require('SettingInt("vram_report_mb", 0)' in text("src/native/main.cpp"), "VRAM report cap is not default-off")

platform = text("src/native/mod_platform.cpp")
order = [platform.index(name) for name in ("ModFilesInitialize", "ModTexturesInitialize", "ModFeaturesInitialize",
                                          "ModPluginsInitialize")]
require(order == sorted(order), "mod startup subsystem order changed")
require("ModPlatformFrame();" in text("src/native/main.cpp"), "plugin frame lifecycle is not wired")
require("ModPlatformShutdown();" in text("src/native/main.cpp"), "plugin shutdown lifecycle is not wired")
kernel = text("src/native/kernel32.cpp")
require("if (!write && disposition == 3) host = ModFilesResolveReadPath(name, host);" in kernel,
        "guest drive read-only opens do not reach overlays")
require("K(DeleteFileA) {\n    int r = unlink(HostPath" in kernel, "delete route was redirected")
crt = text("src/native/msvcr80.cpp")
require('m.find_first_of("wa+") == std::string::npos' in crt, "CRT write modes are not excluded from overlays")

java = text("android/java/com/protodroid/ModManifest.java")
require('d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071' in java
        and 'd331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071' in text("src/native/mod_platform.h"),
        "engine identity mismatch")
require("ProtoDroidTextureV1" in text("src/native/mod_textures.cpp")
        and "ProtoDroidTextureV1" in text("re/native-mod-platform.md"), "texture key contract mismatch")
require("if (ModTexturesEnabled())" in text("src/native/d3d9.cpp"),
        "ordinary texture unlocks lost the disabled fast path")
plugin_header = text("modding/include/protodroid_plugin.h")
require("#define PD_PLUGIN_API_VERSION 2u" in plugin_header and
        "#define PD_PLUGIN_API_VERSION_V1 1u" in plugin_header and
        "PD_HOST_API_V1_SIZE" in plugin_header,
        "appended ABI v2 must retain the exact v1 contract")
plugins = text("src/native/mod_plugins.cpp")
for getter in ("ApiGetModBoolean", "ApiGetModNumber", "ApiGetModString", "LoadSettings"):
    require(getter in plugins, f"native per-mod configuration path missing: {getter}")
require("PD_HOST_API_V1_SIZE" in plugins and "plugin->info.abi_version != abi" in plugins,
        "host does not select and validate the manifest-requested plugin ABI")
textures = text("src/native/mod_textures.cpp")
require("ReadDdsLevel" in textures and "uint64_t offset" in textures and "std::vector<uint8_t> bytes;" not in textures,
        "DDS replacements must retain offsets rather than permanent whole-file level buffers")
require("ModConfig.java" in text(".github/workflows/build-apk.yml"), "mod configuration is absent from host CI")
build_deps = text("android/build-deps.sh")
require("5dfc8546954ce3b3198132ab13fddb89ee92cdd7" in build_deps,
        "Dobby source revision is not pinned")
require('"-DCMAKE_ASM_FLAGS=-x assembler-with-cpp"' in build_deps,
        "Dobby assembly architecture guards require the preprocessor on every ABI")
require("readNBytes" not in java, "mod imports must retain the API-26 Java baseline")
manager = text("android/java/com/protodroid/ModManager.java")
require("flattenLegacyWrapper" in manager and "legacyContentRoot" in manager,
        "single-folder legacy archives are not normalized")
settings = text("android/java/com/protodroid/GameSettings.java")
require("DEFAULTS" not in settings and "setting.defaultIndex()" in settings,
        "settings defaults can become misaligned with their declarations")
for key in ("fov_offset", "shadow_map_resolution", "remove_tick_rate_limit", "fix_button_tap_tick_rate",
            "prefer_loose_files", "unlock_dev_menu", "vram_report_mb", "dump_textures"):
    require(f'"{key}"' in settings, f"Prototype Fix setting is not exposed: {key}")
require('new int[] {0, 512, 1024, 2048, 4096, 8192}' in settings,
        "launcher shadow choices do not cover the documented range")
for marker in ("INI_LOCK", "protodroid.ini.tmp", "stream.getFD().sync()",
               "StandardCopyOption.ATOMIC_MOVE", "Toast.makeText"):
    require(marker in settings, f"engine INI durability/visibility regression: {marker}")
touch_controls = text("android/java/com/protodroid/TouchControls.java")
require("settingsStore.setIndex(s, s.index + delta)" in touch_controls and
        "settingsStore.gameplaySettings" in touch_controls and "restartNeeded" not in touch_controls,
        "filtered in-game settings must write the selected setting without restart-only rows")
require("applyLiveSettings();" in touch_controls and "nativeSetMenuFpsUnlocked" in touch_controls,
        "live FPS/menu state is not applied by the gear panel")
mods_view = text("android/java/com/protodroid/ModsView.java")
mods_activity = text("android/java/com/protodroid/ModsActivity.java")
require("ACTION_FIXES" not in mods_view and "ACTION_CONFIGURE" in mods_view,
        "Mods must be a library with per-mod configuration, not an engine-fixes panel")
motion = text("android/java/com/protodroid/LauncherMotion.java")
require("ValueAnimator.areAnimatorsEnabled()" in motion, "Android reduced-motion policy is not authoritative")
require("contentShiftX" in mods_view and "actionBounds[action].offset(contentShiftX" in mods_view,
        "animated mod content and its touch bounds can diverge")
require("pluginArmedId" in mods_view and "removeArmedId" in mods_view,
        "native-plugin/removal confirmation was lost")
require("runBackground" in mods_activity and mods_activity.count("runBackground(") >= 4,
        "mod import/lifecycle operations are not serialized off the UI thread")
driver_view = text("android/java/com/protodroid/DriverView.java")
setup_view = text("android/java/com/protodroid/SetupView.java")
require("LauncherMotion.enabled()" in driver_view and "focusNext" in driver_view,
        "driver motion/focus behavior regressed")
require("motionEnabled" in setup_view and "drawBootCurtain" in setup_view,
        "Setup does not retain reduced-motion behavior")
require("Files.isSymbolicLink(directory.toPath())" in java, "symlinked package roots are not rejected")
require('unlink((mod.root + "/launch-error.txt").c_str());' in platform,
        "native diagnostics could follow a link into original data")
require("dlclose(" not in text("src/native/mod_plugins.cpp"), "plugin code must remain process-lifetime")

focus = text("android/java/com/protodroid/SetupView.java")
require(focus.count("ACTION_MODS, ACTION_DRIVERS") >= 5 and
        "settingsOrder.add(ACTION_MODS)" in focus and "settingsOrder.add(ACTION_DRIVERS)" in focus and
        "SETTING_ACTION_BASE + i * 2 + 1" in focus,
        "Mods/Drivers or progressive setting-help targets are missing from launcher focus order")
require("paths-ignore:\n      - '**.md'" in text(".github/workflows/build-apk.yml"), "workflow trigger unexpectedly changed")

print("native mod source/signature tests passed")
