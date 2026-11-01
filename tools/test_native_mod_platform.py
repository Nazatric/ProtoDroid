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
for setting in ('SettingInt("fov_offset", 0)', 'SettingInt("shadow_map_resolution", 0)',
                'SettingInt("unlock_dev_menu", 0)', 'SettingInt("prefer_loose_files", 0)'):
    require(setting in features or setting in text("src/native/mod_files.cpp"), f"unsafe feature default: {setting}")
require('SettingInt("unlock_menu_fps", 1)' in text("src/native/bink.cpp"), "default-On menu FPS unlock changed")
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
require("#define PD_PLUGIN_API_VERSION 1u" in text("modding/include/protodroid_plugin.h"), "plugin ABI version changed")
require("5dfc8546954ce3b3198132ab13fddb89ee92cdd7" in text("android/build-deps.sh"),
        "Dobby source revision is not pinned")
require("readNBytes" not in java, "mod imports must retain the API-26 Java baseline")
require("Files.isSymbolicLink(directory.toPath())" in java, "symlinked package roots are not rejected")
require('unlink((mod.root + "/launch-error.txt").c_str());' in platform,
        "native diagnostics could follow a link into original data")
require("dlclose(" not in text("src/native/mod_plugins.cpp"), "plugin code must remain process-lifetime")

focus = text("android/java/com/protodroid/SetupView.java")
require(focus.count("ACTION_MODS, ACTION_DRIVERS") >= 7, "Mods is missing from a launcher focus order")
require("paths-ignore:\n      - '**.md'" in text(".github/workflows/build-apk.yml"), "workflow trigger unexpectedly changed")

print("native mod source/signature tests passed")
