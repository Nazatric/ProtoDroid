// Declarative, host-testable diagnostic-tour plan. Actions are ordinary physical inputs at the SDL boundary, not
// engine debug commands. Frame numbers are successful D3D9 Present counts within each phase, never pump iterations.
// The isolated tour's guest timers run at 4x; action scheduling, host timeouts and measured wall time remain real time.
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
    Return,
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
    M(0, 24, 0), M(20, 24, 0), M(40, 24, 0), M(60, 24, 0), M(80, 24, 0),
    M(100, -24, 0), M(120, -24, 0), M(140, -24, 0), M(160, -24, 0),
};
inline constexpr TourAction kShadowGround[] = {M(0, 0, 32), M(30, 0, 24), M(90, 0, -20), M(120, 0, 24)};
inline constexpr TourAction kShadowMotion[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift),
    M(40, 12, 0), M(80, -12, 0), M(120, 12, 12),
    K(160, false, TourInput::W), K(160, false, TourInput::LeftShift),
};
inline constexpr TourAction kMovement[] = {
    K(0, true, TourInput::W), K(55, false, TourInput::W),
    K(55, true, TourInput::A), K(95, false, TourInput::A),
    K(95, true, TourInput::D), K(135, false, TourInput::D),
    K(135, true, TourInput::S), K(175, false, TourInput::S),
    K(175, true, TourInput::W), K(175, true, TourInput::D),
    K(220, false, TourInput::W), K(220, false, TourInput::D),
};
inline constexpr TourAction kJumpSprint[] = {
    K(0, true, TourInput::LeftShift), K(0, true, TourInput::W),
    K(25, true, TourInput::Space), K(45, false, TourInput::Space),
    K(85, true, TourInput::Space), K(115, false, TourInput::Space),
    K(150, true, TourInput::Space), K(170, false, TourInput::Space),
    K(215, false, TourInput::W), K(215, false, TourInput::LeftShift),
};
inline constexpr TourAction kSprintLong[] = {
    K(0, true, TourInput::LeftShift), K(0, true, TourInput::W),
    M(50, 14, 0), M(100, -14, 0), M(150, 14, 0), M(200, -14, 0),
    K(250, false, TourInput::W), K(250, true, TourInput::D),
    K(280, false, TourInput::D), K(280, false, TourInput::LeftShift),
};
// Sprint + charged jump + a second airborne jump is the normal PC glide input sequence. The second jump and any
// glide/air-dash result are requests only; no player position or ability state is inferred by the controller.
inline constexpr TourAction kChargedJump[] = {
    K(0, true, TourInput::LeftShift), K(0, true, TourInput::W),
    K(20, true, TourInput::Space), K(75, false, TourInput::Space),
    K(95, true, TourInput::Space), K(115, false, TourInput::Space),
    K(155, true, TourInput::Space), K(200, false, TourInput::Space),
    K(215, false, TourInput::W), K(215, false, TourInput::LeftShift),
};
inline constexpr TourAction kGlideDash[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift),
    K(20, true, TourInput::Space), K(70, false, TourInput::Space),
    K(90, true, TourInput::Space), K(110, false, TourInput::Space),
    K(130, false, TourInput::LeftShift), K(140, true, TourInput::LeftShift),
    K(160, true, TourInput::Space), K(180, false, TourInput::Space),
    K(210, false, TourInput::LeftShift), K(220, true, TourInput::LeftShift),
    K(240, true, TourInput::Space), K(260, false, TourInput::Space),
    M(280, 16, 0), K(300, true, TourInput::Space), K(320, false, TourInput::Space),
    K(340, false, TourInput::W), K(340, false, TourInput::LeftShift),
};
// These are deliberately attempted near whatever geometry the direct test level provides. No wall/rooftop is spawned,
// and no contact, vertical gain, roof arrival, or landing is claimed without a separate visual/device check.
inline constexpr TourAction kBuildingClimb[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift), M(20, 0, -12),
    K(40, true, TourInput::Space), K(55, false, TourInput::Space),
    K(105, true, TourInput::Space), K(120, false, TourInput::Space),
    M(150, 12, -8), K(170, true, TourInput::Space), K(185, false, TourInput::Space),
    K(235, true, TourInput::Space), K(250, false, TourInput::Space),
    M(280, -12, 0), K(300, true, TourInput::Space), K(315, false, TourInput::Space),
    K(340, false, TourInput::W), K(340, false, TourInput::LeftShift),
};
inline constexpr TourAction kBuildingStrafe[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift),
    K(20, true, TourInput::A), K(45, true, TourInput::Space), K(60, false, TourInput::Space),
    K(85, false, TourInput::A), K(85, true, TourInput::D),
    K(115, true, TourInput::Space), K(130, false, TourInput::Space),
    K(155, false, TourInput::D), K(155, true, TourInput::A),
    K(185, true, TourInput::Space), K(200, false, TourInput::Space),
    K(225, false, TourInput::A), K(225, true, TourInput::D),
    K(250, true, TourInput::Space), K(265, false, TourInput::Space),
    K(280, false, TourInput::D), K(280, false, TourInput::W), K(280, false, TourInput::LeftShift),
};
inline constexpr TourAction kWallGrab[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift),
    K(30, true, TourInput::Space), K(50, false, TourInput::Space),
    K(75, true, TourInput::E), K(105, false, TourInput::E),
    K(125, true, TourInput::A), K(155, false, TourInput::A),
    K(155, true, TourInput::D), K(185, false, TourInput::D),
    K(195, true, TourInput::Space), K(240, false, TourInput::Space),
    K(270, false, TourInput::W), K(270, false, TourInput::LeftShift),
};
inline constexpr TourAction kRooftopDescent[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift),
    K(20, true, TourInput::Space), K(70, false, TourInput::Space),
    K(90, true, TourInput::Space), K(110, false, TourInput::Space),
    M(130, 20, 12), K(150, false, TourInput::LeftShift), K(160, true, TourInput::LeftShift),
    K(180, true, TourInput::Space), K(200, false, TourInput::Space),
    K(220, false, TourInput::W), K(220, true, TourInput::S),
    K(250, false, TourInput::S), K(250, false, TourInput::LeftShift),
    M(270, 0, 28),
};
inline constexpr TourAction kParkour[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift),
    K(25, true, TourInput::Space), K(40, false, TourInput::Space),
    K(75, true, TourInput::Space), K(95, false, TourInput::Space),
    K(115, true, TourInput::Space), K(135, false, TourInput::Space),
    M(150, 18, 0), K(165, true, TourInput::Space), K(185, false, TourInput::Space),
    K(215, true, TourInput::Space), K(240, false, TourInput::Space),
    K(255, true, TourInput::Space), K(270, false, TourInput::Space),
    K(280, false, TourInput::W), K(280, false, TourInput::LeftShift),
};
inline constexpr TourAction kLandingRecovery[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift),
    K(15, true, TourInput::Space), K(65, false, TourInput::Space),
    K(85, false, TourInput::W), K(85, true, TourInput::S),
    K(105, true, TourInput::Space), K(120, false, TourInput::Space),
    K(140, false, TourInput::S), K(140, false, TourInput::LeftShift), M(155, 0, 30),
};
inline constexpr TourAction kTargetLock[] = {
    K(0, true, TourInput::Tab), K(12, false, TourInput::Tab), M(20, 20, 0),
    K(35, true, TourInput::Tab), K(47, false, TourInput::Tab),
    K(55, true, TourInput::LeftMouse), K(75, false, TourInput::LeftMouse), M(85, -20, 0),
    K(100, true, TourInput::Tab), K(112, false, TourInput::Tab),
    K(130, true, TourInput::RightMouse), K(150, false, TourInput::RightMouse),
};
inline constexpr TourAction kCombat[] = {
    K(10, true, TourInput::LeftMouse), K(25, false, TourInput::LeftMouse),
    K(40, true, TourInput::LeftMouse), K(55, false, TourInput::LeftMouse),
    K(70, true, TourInput::LeftMouse), K(85, false, TourInput::LeftMouse),
    K(100, true, TourInput::RightMouse), K(120, false, TourInput::RightMouse),
    K(140, true, TourInput::LeftMouse), K(165, false, TourInput::LeftMouse),
    K(185, true, TourInput::RightMouse), K(215, false, TourInput::RightMouse),
};
inline constexpr TourAction kChargedCombat[] = {
    K(10, true, TourInput::LeftMouse), K(70, false, TourInput::LeftMouse),
    K(90, true, TourInput::RightMouse), K(155, false, TourInput::RightMouse),
    K(175, true, TourInput::LeftMouse), K(195, false, TourInput::LeftMouse),
    K(205, true, TourInput::RightMouse), K(225, false, TourInput::RightMouse),
};
inline constexpr TourAction kGrabFollowup[] = {
    K(0, true, TourInput::E), K(25, false, TourInput::E),
    K(40, true, TourInput::E), K(65, false, TourInput::E),
    K(70, true, TourInput::RightMouse), K(95, false, TourInput::RightMouse),
    K(110, true, TourInput::E), K(135, false, TourInput::E),
    K(150, true, TourInput::LeftMouse), K(175, false, TourInput::LeftMouse),
    K(195, true, TourInput::E), K(220, false, TourInput::E),
};
inline constexpr TourAction kGrabThrow[] = {
    K(0, true, TourInput::E), K(25, false, TourInput::E),
    K(30, true, TourInput::W), K(55, true, TourInput::RightMouse), K(80, false, TourInput::RightMouse),
    K(100, true, TourInput::LeftMouse), K(125, false, TourInput::LeftMouse),
    K(145, true, TourInput::E), K(170, false, TourInput::E),
    K(185, true, TourInput::RightMouse), K(210, false, TourInput::RightMouse),
    K(220, false, TourInput::W),
};
inline constexpr TourAction kPowers[] = {
    K(0, true, TourInput::Num1), K(10, false, TourInput::Num1),
    K(30, true, TourInput::Num2), K(40, false, TourInput::Num2),
    K(60, true, TourInput::Num3), K(70, false, TourInput::Num3),
    K(90, true, TourInput::Num4), K(100, false, TourInput::Num4),
    K(120, true, TourInput::Q), K(135, false, TourInput::Q),
    K(160, true, TourInput::Num1), K(170, false, TourInput::Num1),
    K(195, true, TourInput::Num3), K(205, false, TourInput::Num3),
    K(230, true, TourInput::Q), K(245, false, TourInput::Q),
    K(270, true, TourInput::Num4), K(280, false, TourInput::Num4),
};
inline constexpr TourAction kPowerWheel[] = {
    K(0, true, TourInput::R), M(15, 24, 0), M(30, 0, 24), M(45, -24, 0),
    K(60, false, TourInput::R), K(75, true, TourInput::Num1), K(85, false, TourInput::Num1),
    K(105, true, TourInput::R), M(120, -24, 0), M(135, 0, -24), K(150, false, TourInput::R),
};
inline constexpr TourAction kPowerCombat[] = {
    K(0, true, TourInput::Num1), K(10, false, TourInput::Num1),
    K(20, true, TourInput::LeftMouse), K(45, false, TourInput::LeftMouse),
    K(60, true, TourInput::Num2), K(70, false, TourInput::Num2),
    K(80, true, TourInput::RightMouse), K(110, false, TourInput::RightMouse),
    K(130, true, TourInput::Num3), K(140, false, TourInput::Num3),
    K(150, true, TourInput::LeftMouse), K(180, false, TourInput::LeftMouse),
    K(195, true, TourInput::Num4), K(205, false, TourInput::Num4),
    K(215, true, TourInput::RightMouse), K(245, false, TourInput::RightMouse),
    K(260, true, TourInput::Q), K(275, false, TourInput::Q),
};
inline constexpr TourAction kDisguiseWheel[] = {
    K(0, true, TourInput::F), M(15, -24, 0), M(30, 0, 24), M(45, 24, 0),
    K(60, false, TourInput::F), K(90, true, TourInput::F), M(105, 24, 0), K(120, false, TourInput::F),
};
inline constexpr TourAction kDisguiseForms[] = {
    K(0, true, TourInput::F1), K(12, false, TourInput::F1),
    K(35, true, TourInput::F2), K(47, false, TourInput::F2),
    K(70, true, TourInput::F1), K(82, false, TourInput::F1),
    K(110, true, TourInput::F2), K(122, false, TourInput::F2),
    K(150, true, TourInput::F1), K(162, false, TourInput::F1),
};
inline constexpr TourAction kDisguiseMovement[] = {
    K(0, true, TourInput::F1), K(12, false, TourInput::F1),
    K(25, true, TourInput::W), K(25, true, TourInput::LeftShift),
    M(60, 16, 0), K(90, true, TourInput::F2), K(102, false, TourInput::F2),
    K(125, true, TourInput::F1), K(137, false, TourInput::F1),
    K(160, true, TourInput::Space), K(180, false, TourInput::Space),
    K(210, false, TourInput::W), K(210, false, TourInput::LeftShift),
};
inline constexpr TourAction kParticles[] = {
    K(0, true, TourInput::W),
    K(15, true, TourInput::LeftMouse), K(30, false, TourInput::LeftMouse),
    K(45, true, TourInput::RightMouse), K(65, false, TourInput::RightMouse),
    K(80, true, TourInput::LeftMouse), K(95, false, TourInput::LeftMouse),
    K(110, false, TourInput::W), M(110, 20, -12),
    K(135, true, TourInput::Num1), K(145, false, TourInput::Num1),
    K(160, true, TourInput::LeftMouse), K(190, false, TourInput::LeftMouse),
    K(205, true, TourInput::RightMouse), K(225, false, TourInput::RightMouse),
};
inline constexpr TourAction kLighting[] = {
    M(0, -32, -16), M(30, -32, 0), M(60, -32, 0), M(90, 32, 0),
    M(120, 32, 0), M(150, 32, 16),
};
inline constexpr TourAction kStreaming[] = {
    K(0, true, TourInput::LeftShift), K(0, true, TourInput::W),
    M(60, 18, 0), M(120, -18, 0), M(180, 18, 0), M(240, -18, 0),
    K(280, true, TourInput::Space), K(300, false, TourInput::Space),
    M(330, 18, 0), K(370, true, TourInput::Space), K(395, false, TourInput::Space),
    K(450, false, TourInput::W), K(450, false, TourInput::LeftShift),
};
inline constexpr TourAction kStreamingGlide[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift),
    K(20, true, TourInput::Space), K(75, false, TourInput::Space),
    K(95, true, TourInput::Space), K(115, false, TourInput::Space),
    K(140, false, TourInput::LeftShift), K(150, true, TourInput::LeftShift),
    K(175, true, TourInput::Space), K(200, false, TourInput::Space),
    M(220, 18, 0), K(240, true, TourInput::Space), K(290, false, TourInput::Space),
    K(310, true, TourInput::Space), K(330, false, TourInput::Space),
    K(350, false, TourInput::LeftShift), K(360, true, TourInput::LeftShift),
    K(385, true, TourInput::Space), K(410, false, TourInput::Space),
    M(425, -18, 0), K(450, false, TourInput::W), K(450, false, TourInput::LeftShift),
};
inline constexpr TourAction kAudio[] = {
    K(0, true, TourInput::W),
    K(15, true, TourInput::LeftMouse), K(30, false, TourInput::LeftMouse),
    K(60, false, TourInput::W),
    K(65, true, TourInput::RightMouse), K(85, false, TourInput::RightMouse),
    K(110, true, TourInput::LeftMouse), K(135, false, TourInput::LeftMouse),
    K(150, true, TourInput::RightMouse), K(170, false, TourInput::RightMouse),
};
inline constexpr TourAction kAudioCombat[] = {
    K(0, true, TourInput::W), K(0, true, TourInput::LeftShift),
    K(20, true, TourInput::LeftMouse), K(40, false, TourInput::LeftMouse),
    K(60, true, TourInput::RightMouse), K(85, false, TourInput::RightMouse),
    K(105, true, TourInput::E), K(130, false, TourInput::E),
    K(150, true, TourInput::LeftMouse), K(175, false, TourInput::LeftMouse),
    K(195, true, TourInput::RightMouse), K(220, false, TourInput::RightMouse),
    K(240, true, TourInput::LeftMouse), K(260, false, TourInput::LeftMouse),
    K(280, false, TourInput::W), K(280, false, TourInput::LeftShift),
};
inline constexpr TourAction kStress[] = {
    K(0, true, TourInput::LeftShift), K(0, true, TourInput::W),
    K(20, true, TourInput::LeftMouse), K(35, false, TourInput::LeftMouse),
    M(45, 16, 0), K(60, true, TourInput::Space), K(80, false, TourInput::Space),
    K(95, true, TourInput::RightMouse), K(120, false, TourInput::RightMouse),
    M(140, -16, 0), K(160, true, TourInput::Num1), K(170, false, TourInput::Num1),
    K(190, true, TourInput::LeftMouse), K(215, false, TourInput::LeftMouse),
    K(235, true, TourInput::Space), K(255, false, TourInput::Space),
    K(275, true, TourInput::RightMouse), K(305, false, TourInput::RightMouse),
    K(330, false, TourInput::W), K(330, false, TourInput::LeftShift),
};
inline constexpr TourAction kInputRelease[] = {
    K(0, true, TourInput::W), K(8, false, TourInput::W),
    K(10, true, TourInput::A), K(18, false, TourInput::A),
    K(20, true, TourInput::S), K(28, false, TourInput::S),
    K(30, true, TourInput::D), K(38, false, TourInput::D),
    K(40, true, TourInput::LeftShift), K(50, false, TourInput::LeftShift),
    K(55, true, TourInput::Space), K(65, false, TourInput::Space),
    K(70, true, TourInput::E), K(80, false, TourInput::E),
    K(85, true, TourInput::Q), K(95, false, TourInput::Q),
    K(100, true, TourInput::Tab), K(110, false, TourInput::Tab),
    K(115, true, TourInput::LeftMouse), K(125, false, TourInput::LeftMouse),
    K(130, true, TourInput::RightMouse), K(140, false, TourInput::RightMouse),
    K(150, true, TourInput::W), K(150, true, TourInput::LeftShift),
    K(170, true, TourInput::Space), K(185, false, TourInput::Space),
    K(205, false, TourInput::W), K(205, false, TourInput::LeftShift), M(220, 24, -12),
};
inline constexpr TourAction kSustainedCombat[] = {
    K(0, true, TourInput::W), K(15, true, TourInput::LeftMouse), K(35, false, TourInput::LeftMouse),
    K(50, true, TourInput::RightMouse), K(80, false, TourInput::RightMouse),
    K(95, true, TourInput::Tab), K(107, false, TourInput::Tab),
    K(120, true, TourInput::LeftMouse), K(150, false, TourInput::LeftMouse),
    K(165, true, TourInput::E), K(190, false, TourInput::E),
    K(205, true, TourInput::RightMouse), K(235, false, TourInput::RightMouse),
    M(250, 18, 0), K(265, true, TourInput::Num1), K(275, false, TourInput::Num1),
    K(290, true, TourInput::LeftMouse), K(320, false, TourInput::LeftMouse),
    K(335, true, TourInput::RightMouse), K(365, false, TourInput::RightMouse),
    K(380, true, TourInput::E), K(405, false, TourInput::E),
    K(420, true, TourInput::LeftMouse), K(440, false, TourInput::LeftMouse),
    K(455, false, TourInput::W),
};
inline constexpr TourAction kSustainedEffects[] = {
    K(0, true, TourInput::Num1), K(10, false, TourInput::Num1),
    K(20, true, TourInput::LeftMouse), K(45, false, TourInput::LeftMouse),
    M(60, 24, 0), K(75, true, TourInput::Num2), K(85, false, TourInput::Num2),
    K(100, true, TourInput::RightMouse), K(130, false, TourInput::RightMouse),
    M(145, -24, 0), K(160, true, TourInput::Num3), K(170, false, TourInput::Num3),
    K(185, true, TourInput::LeftMouse), K(215, false, TourInput::LeftMouse),
    M(230, 24, 12), K(245, true, TourInput::Num4), K(255, false, TourInput::Num4),
    K(270, true, TourInput::RightMouse), K(300, false, TourInput::RightMouse),
    K(315, true, TourInput::Q), K(330, false, TourInput::Q),
};

// "tested" below always means these requested inputs ran with real rendered dwell and gameplay task activity. It is
// not visual verification or proof that the named ability, wall/rooftop interaction, target, or disguise was available.
inline constexpr TourPhase kTourPhases[] = {
    Idle("render_shaders_pipelines", "initial gameplay rendering, shader/pipeline creation, DF shadow profile; actions only", 180),
    P("camera_orbit", "camera-relative rendering, lighting angles, culling and input", 180, kCamera),
    P("shadow_ground_view", "dynamic-shadow view toward the ground while stationary; no visual pass claim", 150, kShadowGround),
    P("shadow_motion", "dynamic shadows and DF-format effects during accelerated movement; no visual pass claim", 180, kShadowMotion),
    P("movement_directions", "W/A/S/D and diagonal movement, animation, collision, camera and character rendering", 240, kMovement),
    P("sprint_sustained", "sustained 4x-clock sprint, steering, animation and collision activity", 300, kSprintLong),
    P("charged_jump", "sprint, charged jump and airborne second-jump/glide requests; ability results unverified", 240, kChargedJump),
    P("glide_dash_chain", "jump/glide and repeated sprint/air-dash input chain; glide/dash outcomes unverified", 360, kGlideDash),
    P("building_climb_forward", "accelerated sprint/jump/wall-run requests toward nearby geometry; wall contact and rooftop arrival unverified", 360, kBuildingClimb),
    P("building_climb_strafe", "sideways wall-run/climb requests with jumps and direction changes; geometry and vertical gain unverified", 300, kBuildingStrafe),
    P("wall_grab_climb", "wall-grab, strafe and charged-jump requests during climbing; grab/contact results unverified", 300, kWallGrab),
    P("rooftop_leap_descent", "roof-edge/leap/glide/descent input sequence; starting roof, air time and landing unverified", 300, kRooftopDescent),
    P("parkour_obstacles", "sustained sprint and repeated vault/jump requests over available obstacles", 300, kParkour),
    P("landing_recovery", "jump, direction reversal, descent and recovery inputs; landing location unverified", 180, kLandingRecovery),
    P("jump_sprint", "sprint, repeated jumping, animation state, timing and input release", 240, kJumpSprint),
    P("target_lock", "Tab lock-on requests, camera adjustment and follow-up attacks; target acquisition unverified", 180, kTargetLock),
    P("combat_primary", "primary/secondary combat inputs, hit effects, animation and audio", 240, kCombat),
    P("combat_charged", "held/charged primary and secondary attack requests with exact release timing", 240, kChargedCombat),
    P("grab_followup", "E grab holds and follow-up combat inputs; grabbed target and consume/throw result unverified", 240, kGrabFollowup),
    P("grab_throw_movement", "grab, movement, primary/secondary throw requests; object/target availability unverified", 240, kGrabThrow),
    P("powers_and_abilities", "1/2/3/4/Q physical inputs; activated power/result remains unverified", 300, kPowers),
    P("power_wheel", "R power-selection wheel held through relative mouse choices; selected entry unverified", 180, kPowerWheel),
    P("power_combat_combo", "power selection followed by primary/secondary combat and effects", 300, kPowerCombat),
    P("disguise_wheel", "F disguise-selection wheel held through relative mouse choices; selected entry unverified", 150, kDisguiseWheel),
    P("disguise_forms", "F1 take-disguise and F2 return-to-Alex inputs; available disguise/form result unverified", 180, kDisguiseForms),
    P("disguise_movement", "form-change requests during sprint/jump movement; active form remains unverified", 240, kDisguiseMovement),
    P("particles_effects", "combat/movement/power particles, effects, shader variants and audio", 240, kParticles),
    P("lighting_exposure", "opposite camera angles for lighting, shadows, culling and exposure", 180, kLighting),
    P("assets_streaming", "long accelerated sprint/jump/camera asset requests and streaming; distance covered unverified", 480, kStreaming),
    P("assets_streaming_glide", "long jump/glide/dash/camera streaming sequence; traversal and asset distance unverified", 480, kStreamingGlide),
    P("audio_mix", "gameplay/combat audio timing, underruns, SDL mixer and worker activity", 180, kAudio),
    P("audio_combat_stress", "sustained movement/grab/combat audio and worker activity", 300, kAudioCombat),
    Skip("bink_video", "Bink audio/video playback", "direct Level entry does not request a movie; normal Bink playback is not covered"),
    Skip("android_touch_overlay", "physical Android touch and overlay widgets", "automation uses the ordinary SDL input boundary, not physical touch"),
    Idle("save_isolation", "NoAutoSave command, isolated guest save route and durable tour markers", 120),
    P("memory_thread_stress", "combined accelerated rendering/input work with memory, thread, CPU and timing diagnostics", 360, kStress),
    P("input_release_stress", "rapid press/release sequences and held-input cleanup across keyboard and mouse", 240, kInputRelease),
    P("sustained_combat", "long target/grab/power/combat sequence for sustained frame, audio and worker behavior", 480, kSustainedCombat),
    P("sustained_parkour", "long accelerated sprint/jump/glide/parkour sequence for streaming and recovery", 480, kStreamingGlide),
    P("sustained_effects", "long power/combat/camera sequence for effects and shader/pipeline activity", 360, kSustainedEffects),
    Idle("recovery_idle", "all injected inputs released; post-activity rendering, audio and memory recovery", 240),
};

inline constexpr size_t kTourPhaseCount = sizeof(kTourPhases) / sizeof(kTourPhases[0]);
static_assert(kTourPhaseCount == 41, "crash-marker phase indices must be reviewed when the tour changes");
constexpr uint32_t TourPlannedActionFrames() {
    uint32_t total = 0;
    for (const TourPhase& phase : kTourPhases) total += phase.frames;
    return total;
}
constexpr size_t TourPlannedActionCount() {
    size_t total = 0;
    for (const TourPhase& phase : kTourPhases) total += phase.action_count;
    return total;
}
static_assert(TourPlannedActionFrames() == 10440, "README/changelog and the reviewed long-tour deadline need updating");

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
