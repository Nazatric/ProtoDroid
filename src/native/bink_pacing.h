#ifndef PROTORE_BINK_PACING_H
#define PROTORE_BINK_PACING_H

#include <cstddef>
#include <cstdint>

namespace native {

// Decided once after decoder setup: only a genuinely trackless movie may use the menu's non-blocking path.
// Keeping the integer setting here also makes the enabled-by-default/disabled behavior directly testable.
constexpr bool BinkMenuWaitUnlocked(int unlock_menu_setting, std::size_t audio_tracks) {
    return unlock_menu_setting != 0 && audio_tracks == 0;
}

// Prototype's f_7c5a01b0 skips DoFrame/NextFrame when BinkWait returns nonzero. For movies without an audio
// track, the optional menu mode uses that existing branch as a non-blocking poll: the guest renders another UI
// frame, while a zero result is produced only when the next video frame is due. Audio-backed movies retain the
// original blocking behavior and clock. Keeping this decision pure makes the guest ABI contract host-testable.
constexpr uint32_t BinkWaitPollResult(double seconds_until_frame, bool unlock_menu, uint32_t audio_tracks) {
    return unlock_menu && audio_tracks == 0 && seconds_until_frame > 0.0 ? 1u : 0u;
}

}  // namespace native

#endif
