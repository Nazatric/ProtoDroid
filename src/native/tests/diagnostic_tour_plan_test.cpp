#include "diagnostic_tour_plan.h"

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
    }
    Check(action_total > 60 && frame_total >= 2000, "tour does not contain the expected sequential coverage");
    Check(shadows && powers && skips, "shadow/power coverage or honest skip records are missing");
}

void TestCursorUsesPresentFrames() {
    for (size_t i = 0; i < native::kTourPhaseCount; ++i) {
        const auto& phase = native::kTourPhases[i];
        native::TourPhaseCursor cursor;
        cursor.Begin(i);
        size_t emitted = 0;
        for (uint32_t frame = 0; frame < phase.frames; ++frame) {
            TourAction action{};
            while (cursor.Next(action)) {
                Check(action.frame == frame, "cursor emitted an action on the wrong presented frame");
                ++emitted;
            }
            if (frame + 1 < phase.frames) Check(!cursor.Complete(), "phase completed before its rendered dwell");
            cursor.AdvanceFrame();
        }
        Check(emitted == phase.action_count && cursor.next_action() == phase.action_count,
              "cursor dropped or duplicated an action");
        Check(cursor.Complete(), "phase did not complete after all presents and actions");
        TourAction extra{};
        Check(!cursor.Next(extra), "cursor emitted an action after phase completion");
    }
}

}  // namespace

int main() {
    TestPlanIntegrity();
    TestCursorUsesPresentFrames();
    std::puts("diagnostic-tour plan tests passed (present-frame timing, sequential actions, skips, input cleanup)");
    return 0;
}
