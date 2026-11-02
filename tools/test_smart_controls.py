#!/usr/bin/env python3
"""Structural contracts for the optional Smart touch overlay and its read-only Consume probe."""

import csv
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def text(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def generated_function(path, name):
    source = text(path)
    marker = "void %s(Ctx* c) {" % name
    start = source.find(marker)
    require(start >= 0, "missing generated function " + name)
    # Generated function declarations have no body; the definition was found above. The next definition is enough
    # to bound this targeted check without attempting to parse C.
    next_definition = re.search(r"\nvoid f_[0-9a-f]+\(Ctx\* c\) \{", source[start + len(marker):])
    end = start + len(marker) + next_definition.start() if next_definition else len(source)
    return source[start:end]


def engine_evidence():
    with (ROOT / "re/functions.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.reader(stream))
    matches = [row for row in rows if row and row[0] == "10122160"]
    require(len(matches) == 1, "Consume condition inventory address must remain unique")
    require(matches[0][1] == "130" and matches[0][3].endswith("TransformationConsumeDescriptionValidCondition")
            and matches[0][4] == "vfunction10", "Consume condition identity/size changed")

    condition = generated_function("build/recomp/native/part_0012.c", "f_7c122160")
    for instruction in ("7c122160: mov eax, dword ptr [esp + 0xc]",
                        "7c122170: call 0x7c7fc7a0",
                        "7c122185: mov ecx, dword ptr [0x7d28348c]",
                        "7c1221ce: call 0x7c7e0810",
                        "7c1221d3: cmp byte ptr [esp + 0x14], bl",
                        "7c1221d8: sete al"):
        require(instruction in condition, "generated Consume condition lost " + instruction)
    dispatcher = generated_function("build/recomp/native/part_007e.c", "f_7c7e0810")
    require("ret 4" in dispatcher, "message dispatcher no longer pops its pointer argument")
    player = generated_function("build/recomp/native/part_001d.c", "f_7c1d9040")
    require("7c1d90a8: cmp eax, dword ptr [0x7d26e40c]" in player
            and "7c1d908c: mov edx, dword ptr [0x7d28348c]" in player,
            "player-handle association evidence changed")

    # Model the stack operations around 0x101221ce. At entry S, the post-dispatch comparison is [esp+0x14].
    # The dispatcher's ret 4 is why that address is S-4, not S-8.
    entry = 0x10000
    esp = entry - 0x10       # sub esp, 0x10
    esp -= 4                 # push esi
    esp -= 4                 # push input reference
    esp -= 4                 # push helper output pointer
    esp -= 4                 # helper call return address
    esp += 4                 # helper ret
    esp += 8                 # add esp, 8
    esp -= 4                 # push ebx
    esp -= 4                 # push message pointer
    esp -= 4                 # dispatcher call return address
    esp += 4                 # dispatcher ret target
    esp += 4                 # dispatcher ret 4
    require(esp + 0x14 == entry - 4, "derived raw-message offset is not entry ESP-4")


def native_contract():
    source = text("src/native/smart_controls.cpp")
    header = text("src/native/smart_controls.h")
    for fragment in ("kEngineBase + 0x00122160u", "kEngineBase + 0x0128348cu",
                     "kEngineBase + 0x0126e40cu", "SmartPlayerHandleMatches(handle, player)",
                     "marker < 0x10000u || marker > 0x2ffffu", "index == 0xffffu",
                     "R16(entry + 4) != ((handle >> 16) & 0xffffu)", "R8(entry_esp - 4)",
                     "DecodeConsumeProbe(true, condition_result, expected, message)",
                     "g_consume_original(context)"):
        require(fragment in source, "native probe contract missing " + fragment)
    # This feature observes the engine. It must never patch a condition, object, message, or stack to manufacture
    # a label. Host atomic stores are fine; guest W*/SW* writes are not.
    require(not re.search(r"\b(?:W8|W16|W32|SW8|SW16|SW32)\s*\(", source),
            "Smart probe contains a guest-memory write")
    require("condition_result != (message == expected)" in header,
            "negated/inconsistent condition results are not rejected")
    require("age_ns < 0 || age_ns > kSmartContextFreshNs" in header,
            "packed context does not fail closed for future/stale samples")
    require("smart_controls.cpp" in text("src/native/Makefile"), "native source missing from Makefile")
    features = text("src/native/mod_features.cpp")
    require("SmartControlsInitialize();" in features, "probe is not installed before engine startup")
    main = text("src/native/main.cpp")
    require("Java_com_protodroid_TouchControls_nativeSetSmartControls" in main
            and "Java_com_protodroid_TouchControls_nativeSmartSpecialContext" in main,
            "Smart JNI entry points are incomplete")


def android_contract():
    settings = text("android/java/com/protodroid/GameSettings.java")
    require('new Setting("Touch controls", "touch_controls", new String[] {"Classic", "Smart"}' in settings,
            "Classic-first Smart setting missing")
    require('|| setting.key.equals("touch_controls")' in settings, "Smart setting is not live in gameplay")
    controls = text("android/java/com/protodroid/TouchControls.java")
    model = text("android/java/com/protodroid/SmartControlsModel.java")
    require('new Button("SPECIAL", 0, MOUSE_RIGHT' in controls,
            "built-in Special no longer retains the physical right-click binding")
    require("b.builtInIndex == BUTTON_SPECIAL" in controls
            and "SmartControlsModel.specialLabel(smartSpecialContext)" in controls,
            "dynamic label is not restricted to the built-in Special button")
    require('label + ":" + b.bindingText()' in controls and '":I" + icon' in controls,
            "dynamic label/icon omitted from bitmap cache key")
    require("nativeSmartSpecialContext()" in controls and "postDelayed(this, 50)" in controls,
            "bounded native polling is missing")
    require("removeCallbacks(smartContextPoll)" in controls
            and "nativeSetSmartControls(false)" in controls
            and "isAttachedToWindow()" in controls and "onWindowVisibilityChanged" in controls,
            "Smart lifecycle cleanup is missing")
    require("requestUnbufferedDispatch" not in controls, "unproven unbuffered input path introduced")
    require('return nativeContext == CONTEXT_CONSUME ? "CONSUME" : "SPECIAL";' in model,
            "Java unknown/invalid context does not fall back to Special")
    workflow = text(".github/workflows/build-apk.yml")
    require("smart_controls_test.cpp" in workflow and "tools/test_smart_controls.py" in workflow,
            "Smart tests are not wired into exact-commit CI")
    require("android/java/com/protodroid/SmartControlsModel.java" in workflow,
            "Smart Java model is not compiled by its host test")


def main():
    engine_evidence()
    native_contract()
    android_contract()
    print("Smart controls structure passed: exact DLL identity, player gate, ESP-4 derivation, read-only probe, "
          "Classic/right-click fallback, cache and lifecycle contracts")


if __name__ == "__main__":
    main()
