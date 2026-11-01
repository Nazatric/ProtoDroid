#include "diagnostic_tour_plan.h"
#include "diagnostic_tour_probe.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

namespace {

using namespace native;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "diagnostic-tour plan test failed: %s\n", message);
        std::exit(1);
    }
}

bool IsMouse(TourInput input) { return input == TourInput::LeftMouse || input == TourInput::RightMouse; }

void TestPlanIntegrity() {
    std::set<std::string> names;
    size_t action_total = 0;
    uint64_t frame_total = 0;
    bool shadows = false, powers = false, skips = false;
    bool target = false, grab = false, power_wheel = false, disguise_wheel = false, forms = false;
    for (size_t i = 0; i < native::kTourPhaseCount; ++i) {
        const auto& phase = native::kTourPhases[i];
        Check(phase.name && *phase.name && names.insert(phase.name).second, "phase names are not unique");
        Check(phase.coverage && *phase.coverage, "phase coverage is missing");
        if (phase.skip_reason) {
            Check(phase.frames == 0 && phase.action_count == 0, "skipped phase has fabricated actions");
            skips = true;
            continue;
        }
        Check(phase.frames >= 60, "real-render dwell is too short");
        frame_total += phase.frames;
        action_total += phase.action_count;
        bool held[256] = {};
        uint16_t previous = 0;
        for (size_t j = 0; j < phase.action_count; ++j) {
            const auto& action = phase.actions[j];
            Check(action.frame >= previous && action.frame < phase.frames, "actions are unordered or outside dwell");
            previous = action.frame;
            if (action.type == TourActionType::MouseMove) {
                Check(action.input == TourInput::None && (action.x != 0 || action.y != 0), "invalid camera action");
                continue;
            }
            const auto index = static_cast<unsigned>(action.input);
            Check(index < 256 && action.input != TourInput::None, "key action has no input");
            if (action.type == TourActionType::KeyDown) Check(!held[index], "key pressed twice without release");
            else Check(held[index] || IsMouse(action.input), "key released without a press");
            held[index] = action.type == TourActionType::KeyDown;
        }
        for (bool down : held) Check(!down, "phase leaks a held key into the next phase");
        shadows = shadows || std::strstr(phase.name, "shadow") != nullptr;
        powers = powers || std::strstr(phase.name, "powers") != nullptr;
        target = target || std::strstr(phase.name, "target_lock") != nullptr;
        grab = grab || std::strstr(phase.name, "grab_followup") != nullptr;
        power_wheel = power_wheel || std::strstr(phase.name, "power_wheel") != nullptr;
        disguise_wheel = disguise_wheel || std::strstr(phase.name, "disguise_wheel") != nullptr;
        forms = forms || std::strstr(phase.name, "disguise_forms") != nullptr;
    }
    Check(action_total > 60 && frame_total >= 2000, "tour does not contain the expected sequential coverage");
    Check(shadows && powers && skips, "shadow/power coverage or honest skip records are missing");
    Check(target && grab && power_wheel && disguise_wheel && forms, "target/grab/wheel/form phases are missing");
}

void TestCursorUsesActionFrames() {
    for (size_t i = 0; i < native::kTourPhaseCount; ++i) {
        const auto& phase = native::kTourPhases[i];
        native::TourPhaseCursor cursor;
        cursor.Begin(i);
        size_t emitted = 0;
        for (uint32_t frame = 0; frame < phase.frames; ++frame) {
            TourAction action{};
            while (cursor.Next(action)) {
                Check(action.frame == frame, "cursor emitted an action on the wrong action frame");
                ++emitted;
            }
            if (frame + 1 < phase.frames) Check(!cursor.Complete(), "phase completed before its minimum dwell");
            cursor.AdvanceFrame();
        }
        Check(emitted == phase.action_count && cursor.next_action() == phase.action_count,
              "cursor dropped or duplicated an action");
        Check(cursor.Complete(), "phase did not complete after all action frames and inputs");
        TourAction extra{};
        Check(!cursor.Next(extra), "cursor emitted an action after phase completion");
    }
}

void TestPresentBatchPolicy() {
    Check(native::TourActionStepsForPresentBatch(0) == 0, "idle engine update advanced the action timeline");
    Check(native::TourActionStepsForPresentBatch(1) == 1, "single Present did not advance one action frame");
    Check(native::TourActionStepsForPresentBatch(2) == 1, "two Presents compressed two action frames into one update");
    Check(native::TourActionStepsForPresentBatch(60) == 1, "large Present batch compressed the action timeline");
    Check(native::TourActionStepsForPresentBatch(UINT64_MAX) == 1, "batch policy overflowed");
}

void TestGameplayProbeTargets() {
    constexpr uint32_t base = 0x7c000000u;
    Check(DiagnosticTourGameplayTaskTarget(base + 0x000246b3u, base) == base + 0x004e45c0u,
          "simulation vtable thunk is not mapped to its task");
    Check(DiagnosticTourGameplayTaskTarget(base + 0x0002bc01u, base) == base + 0x004e4ad0u,
          "real-time vtable thunk is not mapped to its task");
    Check(DiagnosticTourGameplayTaskTarget(base + 0x004e45c0u, base) == base + 0x004e45c0u,
          "direct simulation task is not recognized");
    Check(DiagnosticTourGameplayTaskTarget(base + 0x004e4ad0u, base) == base + 0x004e4ad0u,
          "direct real-time task is not recognized");
    Check(DiagnosticTourGameplayTaskTarget(base + 0x004e5250u, base) == 0,
          "unrelated InGameState method is a false gameplay probe match");
    Check(DiagnosticTourGameplayTaskTarget(0, base) == 0, "null target is a false gameplay probe match");

    constexpr uint32_t preferred_base = 0x10000000u;
    Check(DiagnosticTourGameplayTaskTarget(0x100246b3u, preferred_base) == 0x104e45c0u,
          "preferred-base simulation thunk mapping is incorrect");
    Check(DiagnosticTourGameplayTaskTarget(0x1002bc01u, preferred_base) == 0x104e4ad0u,
          "preferred-base real-time thunk mapping is incorrect");
}

}  // namespace

int main() {
    TestPlanIntegrity();
    TestCursorUsesActionFrames();
    TestPresentBatchPolicy();
    TestGameplayProbeTargets();
    std::puts("diagnostic-tour plan tests passed (22 phases, probe targets, uncompressed action timing, sequential actions, skips, input cleanup)");
    return 0;
}
