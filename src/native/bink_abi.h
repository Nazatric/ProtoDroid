// 32-bit Bink 1.9 ABI used by prototypeenginef.dll. All guest pointers are uint32_t offsets, not host pointers.
#pragma once

#include <cstddef>
#include <cstdint>

namespace native::bink {

constexpr uint32_t kNoFrameBuffers = 0x00000400u;
constexpr uint32_t kFrameRate = 0x00001000u;
constexpr uint32_t kPreloadAll = 0x00002000u;
constexpr uint32_t kSoundTrack = 0x00004000u;
constexpr uint32_t kRBInvert = 0x00010000u;
constexpr uint32_t kNoSkip = 0x00080000u;
constexpr uint32_t kAlpha = 0x00100000u;
constexpr uint32_t kFileHandle = 0x00800000u;
constexpr uint32_t kFromMemory = 0x04000000u;

constexpr uint32_t kCopy2XH = 0x10000000u;
constexpr uint32_t kCopy2XHI = 0x20000000u;
constexpr uint32_t kCopy2XW = 0x30000000u;
constexpr uint32_t kCopy2XWH = 0x40000000u;
constexpr uint32_t kCopy2XWHI = 0x50000000u;
constexpr uint32_t kCopy1XI = 0x60000000u;
constexpr uint32_t kCopyNoScaling = 0x70000000u;
constexpr uint32_t kCopyScalingMask = 0x70000000u;

constexpr uint32_t kSurfaceP8 = 0;
constexpr uint32_t kSurface24 = 1;
constexpr uint32_t kSurface24R = 2;
constexpr uint32_t kSurface32 = 3;
constexpr uint32_t kSurface32R = 4;
constexpr uint32_t kSurface32A = 5;
constexpr uint32_t kSurface32RA = 6;
constexpr uint32_t kSurface4444 = 7;
constexpr uint32_t kSurface5551 = 8;
constexpr uint32_t kSurface555 = 9;
constexpr uint32_t kSurface565 = 10;
constexpr uint32_t kSurface655 = 11;
constexpr uint32_t kSurface664 = 12;
constexpr uint32_t kSurfaceYUY2 = 13;
constexpr uint32_t kSurfaceUYVY = 14;
constexpr uint32_t kSurfaceYV12 = 15;
constexpr uint32_t kSurfaceMask = 0x1fu;

struct Rect {
    int32_t left, top, width, height;
};
static_assert(sizeof(Rect) == 16);

struct Plane {
    uint32_t allocate;
    uint32_t buffer;
    uint32_t pitch;  // signed at runtime: Prototype registers bottom-up planes with a negative pitch
};
static_assert(sizeof(Plane) == 12);

struct FramePlaneSet {
    Plane y, cr, cb, alpha;
};
static_assert(sizeof(FramePlaneSet) == 48);
static_assert(offsetof(FramePlaneSet, cr) == 12);
static_assert(offsetof(FramePlaneSet, cb) == 24);
static_assert(offsetof(FramePlaneSet, alpha) == 36);

struct FrameBuffers {
    int32_t total_frames;
    uint32_t ya_width, ya_height;
    uint32_t crcb_width, crcb_height;
    uint32_t frame_num;
    FramePlaneSet frames[2];
};
static_assert(sizeof(FrameBuffers) == 120);
static_assert(offsetof(FrameBuffers, frame_num) == 20);
static_assert(offsetof(FrameBuffers, frames) == 24);

// Public prefix of BINK from the 2008 1.9 SDK. The guest allocation is larger for RAD's private fields, but only
// this prefix is part of the public ABI. In particular the game itself reads Frames at +8 and FrameNum at +0xc.
struct Public {
    uint32_t width;                 // 0x00
    uint32_t height;                // 0x04
    uint32_t frames;                // 0x08
    uint32_t frame_num;             // 0x0c, one based
    uint32_t last_frame_num;        // 0x10
    uint32_t frame_rate;            // 0x14
    uint32_t frame_rate_div;        // 0x18
    uint32_t read_error;            // 0x1c
    uint32_t open_flags;            // 0x20
    uint32_t bink_type;             // 0x24
    uint32_t size;                  // 0x28
    uint32_t frame_size;            // 0x2c
    uint32_t snd_size;              // 0x30
    uint32_t frame_change_percent;  // 0x34
    Rect frame_rects[8];            // 0x38
    int32_t num_rects;              // 0xb8
    uint32_t frame_buffers;         // 0xbc
    uint32_t mask_plane;            // 0xc0
    uint32_t mask_pitch;            // 0xc4
    uint32_t mask_length;           // 0xc8
    uint32_t async_mask_plane;      // 0xcc
    uint32_t in_use_mask_plane;     // 0xd0
    uint32_t last_mask_plane;       // 0xd4
    uint32_t largest_frame_size;    // 0xd8
    uint32_t internal_frames;       // 0xdc
    int32_t num_tracks;             // 0xe0
    uint32_t highest_1sec_rate;     // 0xe4
    uint32_t highest_1sec_frame;    // 0xe8
    int32_t paused;                 // 0xec
};
static_assert(sizeof(Public) == 0xf0);
static_assert(offsetof(Public, frames) == 0x08);
static_assert(offsetof(Public, frame_num) == 0x0c);
static_assert(offsetof(Public, frame_buffers) == 0xbc);
static_assert(offsetof(Public, num_tracks) == 0xe0);

struct TrackPublic {
    uint32_t frequency;
    uint32_t bits;
    uint32_t channels;
    uint32_t max_size;
    uint32_t bink;
    uint32_t sndcomp;
    int32_t trackindex;
};
static_assert(sizeof(TrackPublic) == 28);

struct Realtime {
    uint32_t frame_num, frame_rate, frame_rate_div;
    uint32_t frames, frames_time;
    uint32_t frames_video_decomp_time, frames_audio_decomp_time, frames_read_time;
    uint32_t frames_idle_read_time, frames_thread_read_time, frames_blit_time;
    uint32_t read_buffer_size, read_buffer_used, frames_data_rate;
};
static_assert(sizeof(Realtime) == 56);

struct Summary {
    uint32_t width, height, total_time, file_frame_rate, file_frame_rate_div, frame_rate, frame_rate_div;
    uint32_t total_open_time, total_frames, total_played_frames, skipped_frames, skipped_blits, sound_skips;
    uint32_t total_blit_time, total_read_time, total_video_decomp_time, total_audio_decomp_time;
    uint32_t total_idle_read_time, total_back_read_time, total_read_speed;
    uint32_t slowest_frame_time, slowest_2frame_time, slowest_frame_num, slowest_2frame_num;
    uint32_t average_data_rate, average_frame_size, highest_mem_amount, total_io_memory, highest_io_used;
    uint32_t highest_1sec_rate, highest_1sec_frame;
};
static_assert(sizeof(Summary) == 124);

}  // namespace native::bink
