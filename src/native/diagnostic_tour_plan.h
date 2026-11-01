// Declarative, host-testable diagnostic-tour plan. Actions are ordinary physical inputs at the SDL boundary, not
// engine debug commands. Frame numbers are successful D3D9 Present counts within each phase, never pump iterations.
#pragma once

#include <cstddef>
#include <cstdint>

namespace native {

enum class TourInput : uint8_t {
    None,
    W,
    A,
    S,
    D,
    LeftShift,
    Space,
    Tab,
    R,
    F1,
    F2,
    Num1,
    Num2,
    Num3,
    Num4,
    Q,
    E,
    F,
    LeftMouse,
    RightMouse,
};

enum class TourActionType : uint8_t { KeyDown, KeyUp, MouseMove };

struct TourAction {
    uint16_t frame;
    TourActionType type;
    TourInput input;
    int16_t x;
    int16_t y;
};

struct TourPhase {
    const char* name;
    const char* coverage;
    const char* skip_reason;  // non-null means requested -> skipped, with no fabricated gameplay actions
    uint16_t frames;
    const TourAction* actions;
    size_t action_count;
};

constexpr TourAction K(uint16_t frame, bool down, TourInput input) {
    return {frame, down ? TourActionType::KeyDown : TourActionType::KeyUp, input, 0, 0};
}
constexpr TourAction M(uint16_t frame, int16_t x, int16_t y) {
    return {frame, TourActionType::MouseMove, TourInput::None, x, y};
}
template <size_t N>
constexpr TourPhase P(const char* name, const char* coverage, uint16_t frames, const TourAction (&actions)[N]) {
    return {name, coverage, nullptr, frames, actions, N};
}
constexpr TourPhase Idle(const char* name, const char* coverage, uint16_t frames) {
    return {name, coverage, nullptr, frames, nullptr, 0};
}
constexpr TourPhase Skip(const char* name, const char* coverage, const char* reason) {
    return {name, coverage, reason, 0, nullptr, 0};
}

inline constexpr TourAction kCamera[] = {
    M(0, 24, 0), M(15, 24, 0), M(30, 24, 0), M(45, 24, 0),
    M(60, 24, 0), M(75, 24, 0), M(90, 24, 0), M(105, 24, 0),
};
inline constexpr TourAction kShadowGround[] = {M(0, 0, 32), M(15, 0, 24)};
inline constexpr TourAction kShadowMotion[] = {
    K(0, true, TourInput::W), M(30, 12, 0), M(60, -12, 0), K(90, false, TourInput::W),
};
inline constexpr TourAction kMovement[] = {
    K(0, true, TourInput::W), K(55, false, TourInput::W),
    K(55, true, TourInput::A), K(85, false, TourInput::A),
    K(85, true, TourInput::D), K(115, false, TourInput::D),
    K(115, true, TourInput::S), K(140, false, TourInput::S),
};
inline constexpr TourAction kJumpSprint[] = {
    K(0, true, TourInput::LeftShift), K(0, true, TourInput::W),
    K(30, true, TourInput::Space), K(45, false, TourInput::Space),
    K(90, true, TourInput::Space), K(105, false, TourInput::Space),
    K(130, false, TourInput::W), K(130, false, TourInput::LeftShift),
};
inline constexpr TourAction kCombat[] = {
    K(10, true, TourInput::LeftMouse), K(25, false, TourInput::LeftMouse),
    K(40, true, TourInput::LeftMouse), K(55, false, TourInput::LeftMouse),
    K(70, true, TourInput::LeftMouse), K(85, false, TourInput::LeftMouse),
    K(100, true, TourInput::RightMouse), K(120, false, TourInput::RightMouse),
};
inline constexpr TourAction kTargetLock[] = {
    K(0, true, TourInput::Tab), K(12, false, TourInput::Tab), M(20, 20, 0),
    K(35, true, TourInput::Tab), K(47, false, TourInput::Tab),
    K(55, true, TourInput::LeftMouse), K(70, false, TourInput::LeftMouse), M(80, -20, 0),
    K(90, true, TourInput::Tab), K(102, false, TourInput::Tab),
};
inline constexpr TourAction kGrabFollowup[] = {
    K(0, true, TourInput::E), K(25, false, TourInput::E),
    K(40, true, TourInput::E), K(65, false, TourInput::E),
    K(70, true, TourInput::RightMouse), K(85, false, TourInput::RightMouse),
    K(95, true, TourInput::E), K(110, false, TourInput::E),
};
inline constexpr TourAction kPowers[] = {
    K(0, true, TourInput::Num1), K(10, false, TourInput::Num1),
    K(30, true, TourInput::Num2), K(40, false, TourInput::Num2),
    K(60, true, TourInput::Num3), K(70, false, TourInput::Num3),
    K(90, true, TourInput::Num4), K(100, false, TourInput::Num4),
    K(120, true, TourInput::Q), K(130, false, TourInput::Q),
};
inline constexpr TourAction kPowerWheel[] = {
    K(0, true, TourInput::R), M(15, 24, 0), M(30, 0, 24), M(45, -24, 0),
    K(60, false, TourInput::R), K(75, true, TourInput::Num1), K(85, false, TourInput::Num1),
};
inline constexpr TourAction kDisguiseWheel[] = {
    K(0, true, TourInput::F), M(15, -24, 0), M(30, 0, 24), M(45, 24, 0),
    K(60, false, TourInput::F),
};
inline constexpr TourAction kDisguiseForms[] = {
    K(0, true, TourInput::F1), K(12, false, TourInput::F1),
    K(35, true, TourInput::F2), K(47, false, TourInput::F2),
    K(70, true, TourInput::F1), K(82, false, TourInput::F1),
};
inline constexpr TourAction kParticles[] = {
    K(0, true, TourInput::W),
    K(15, true, TourInput::LeftMouse), K(30, false, TourInput::LeftMouse),
    K(45, true, TourInput::RightMouse), K(65, false, TourInput::RightMouse),
    K(80, true, TourInput::LeftMouse), K(95, false, TourInput::LeftMouse),
    K(110, false, TourInput::W), M(110, 20, -12),
};
inline constexpr TourAction kLighting[] = {
    M(0, -32, -16), M(20, -32, 0), M(40, -32, 0), M(60, -32, 0),
    M(80, -32, 0), M(100, -32, 0),
};
inline constexpr TourAction kStreaming[] = {
    K(0, true, TourInput::LeftShift), K(0, true, TourInput::W),
    M(60, 18, 0), M(120, -18, 0), M(180, 18, 0),
    K(200, false, TourInput::W), K(200, false, TourInput::LeftShift),
};
inline constexpr TourAction kAudio[] = {
    K(0, true, TourInput::W),
    K(15, true, TourInput::LeftMouse), K(30, false, TourInput::LeftMouse),
    K(60, false, TourInput::W),
    K(65, true, TourInput::RightMouse), K(85, false, TourInput::RightMouse),
};
inline constexpr TourAction kStress[] = {
    K(0, true, TourInput::LeftShift), K(0, true, TourInput::W),
    K(20, true, TourInput::LeftMouse), K(35, false, TourInput::LeftMouse),
    M(45, 16, 0), K(60, true, TourInput::Space), K(75, false, TourInput::Space),
    K(85, true, TourInput::RightMouse), K(105, false, TourInput::RightMouse),
    M(120, -16, 0), K(140, false, TourInput::W), K(140, false, TourInput::LeftShift),
};

// "tested" below always means these requested inputs ran with real rendered dwell. It is not visual verification.
inline constexpr TourPhase kTourPhases[] = {
    Idle("render_shaders_pipelines", "initial gameplay rendering, shader/pipeline creation, DF shadow profile; actions only", 120),
    P("camera_orbit", "camera-relative rendering, lighting angles, culling and input", 120, kCamera),
    P("shadow_ground_view", "dynamic-shadow view toward the ground while stationary; no visual pass claim", 90, kShadowGround),
    P("shadow_motion", "dynamic shadows and DF-format effects during movement; no visual pass claim", 120, kShadowMotion),
    P("movement_directions", "W/A/S/D movement, animation, collision, camera and character rendering", 150, kMovement),
    P("jump_sprint", "sprint, jumping, animation state, timing and input release", 150, kJumpSprint),
    P("target_lock", "Tab lock-on requests, camera adjustment and follow-up attack; target acquisition is unverified", 120, kTargetLock),
    P("combat_primary", "primary/secondary combat inputs, hit effects, animation and audio", 150, kCombat),
    P("grab_followup", "E grab holds and follow-up combat input; grabbed target and consume/throw result are unverified", 130, kGrabFollowup),
    P("powers_and_abilities", "1/2/3/4/Q physical inputs; activated power/result remains unverified", 180, kPowers),
    P("power_wheel", "R power-selection wheel held through relative mouse choices; selected entry is unverified", 100, kPowerWheel),
    P("disguise_wheel", "F disguise-selection wheel held through relative mouse choices; selected entry is unverified", 90, kDisguiseWheel),
    P("disguise_forms", "F1 take-disguise and F2 return-to-Alex inputs; available disguise/form result is unverified", 100, kDisguiseForms),
    P("particles_effects", "combat/movement particles, effects, shader variants and audio", 140, kParticles),
    P("lighting_exposure", "opposite camera angles for lighting, shadows, culling and exposure", 120, kLighting),
    P("assets_streaming", "sustained sprint/camera asset requests and streaming; distance covered is unverified", 240, kStreaming),
    P("audio_mix", "gameplay/combat audio timing, underruns, SDL mixer and worker activity", 120, kAudio),
    Skip("bink_video", "Bink audio/video playback", "direct Level entry does not request a movie; normal Bink playback is not covered"),
    Skip("android_touch_overlay", "physical Android touch and overlay widgets", "automation uses the ordinary SDL input boundary, not physical touch"),
    Idle("save_isolation", "NoAutoSave command, isolated guest save route and durable tour markers", 60),
    P("memory_thread_stress", "combined rendering/input work with memory, thread, CPU and timing diagnostics", 180, kStress),
    Idle("recovery_idle", "all injected inputs released; post-activity rendering, audio and memory recovery", 90),
};

inline constexpr size_t kTourPhaseCount = sizeof(kTourPhases) / sizeof(kTourPhases[0]);
static_assert(kTourPhaseCount == 22, "crash-marker phase indices must be reviewed when the tour changes");

// Multiple Presents can arrive between two engine updates. Advancing every logical action frame at that later boundary
// would inject a press and its release before the game runs again. Preserve the action timeline: one action frame per
// engine update with new Present activity, while the controller separately records the actual presented-frame dwell.
constexpr uint64_t TourActionStepsForPresentBatch(uint64_t present_delta) {
    return present_delta == 0 ? 0 : 1;
}

// A small pure scheduler used by production and host tests. It emits each action exactly at its requested frame;
// phase completion is separate so callers can release every held input before recording the result.
class TourPhaseCursor {
public:
    void Begin(size_t phase) {
        phase_ = phase;
        frame_ = 0;
        next_action_ = 0;
    }

    // Returns true when an action is available. Call again until false, then AdvanceFrame() once per engine update
    // with new Present activity (not once for every Present coalesced into that update).
    bool Next(TourAction& action) {
        if (phase_ >= kTourPhaseCount) return false;
        const TourPhase& phase = kTourPhases[phase_];
        if (next_action_ >= phase.action_count) return false;
        const TourAction& next = phase.actions[next_action_];
        if (next.frame != frame_) return false;
        action = next;
        ++next_action_;
        return true;
    }

    void AdvanceFrame() { ++frame_; }
    bool Complete() const {
        if (phase_ >= kTourPhaseCount) return false;
        const TourPhase& phase = kTourPhases[phase_];
        return next_action_ == phase.action_count && frame_ >= phase.frames;
    }
    size_t phase() const { return phase_; }
    uint32_t frame() const { return frame_; }
    size_t next_action() const { return next_action_; }

private:
    size_t phase_ = 0;
    size_t next_action_ = 0;
    uint32_t frame_ = 0;
};

}  // namespace native
