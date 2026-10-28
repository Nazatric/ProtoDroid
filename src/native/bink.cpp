// FFmpeg-backed Bink 1.x compatibility layer for the 32-bit guest. The game renders through registered YUV planes;
// the CPU copy exports and the legacy sound entry points are kept for the rest of the binkw32 ABI as well.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#ifdef PROTORE_BINK_TEST
#include "tests/sdl_stub.h"
#else
#include <SDL2/SDL.h>
#endif

#include "bink_abi.h"
#include "bink_blit.h"
#include "bink_media.h"
#include "guest.h"

namespace native {
namespace {

using Clock = std::chrono::steady_clock;
using namespace bink;

constexpr size_t kMaxTracks = 32;
constexpr uint32_t kGuestAllocation = 0x400;
constexpr int kAudioRate = 48000;
constexpr auto kDecodeBudget = std::chrono::milliseconds(12);
constexpr auto kAudioBudget = std::chrono::milliseconds(4);
constexpr auto kLockBudget = std::chrono::milliseconds(2);
std::atomic<uint32_t> g_output_rate{static_cast<uint32_t>(kAudioRate)};
uint32_t OutputRate() { return g_output_rate.load(std::memory_order_relaxed); }

std::string AVError(int e) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(e, text, sizeof(text));
    return text;
}

struct AudioRing {
    std::vector<float> data;
    size_t head = 0, frames = 0, capacity = 0;

    void Reset(size_t rate = kAudioRate) {
        capacity = static_cast<size_t>(rate) * 2;  // two seconds, bounded even for malformed packet streams
        data.assign(capacity * 2, 0.0f);
        head = frames = 0;
    }
    void Clear() { head = frames = 0; }
    float At(size_t frame, unsigned channel) const {
        return data[((head + frame) % capacity) * 2 + channel];
    }
    void Push(const float* src, size_t count) {
        if (!capacity) Reset();
        if (count >= capacity) {
            src += (count - capacity) * 2;
            count = capacity;
            Clear();
        }
        if (frames + count > capacity) {
            size_t drop = frames + count - capacity;
            head = (head + drop) % capacity;
            frames -= drop;
        }
        for (size_t i = 0; i < count; ++i) {
            data[((head + frames + i) % capacity) * 2] = src[i * 2];
            data[((head + frames + i) % capacity) * 2 + 1] = src[i * 2 + 1];
        }
        frames += count;
    }
    void Consume(size_t count) {
        count = std::min(count, frames);
        if (!capacity) return;
        head = (head + count) % capacity;
        frames -= count;
        if (!frames) head = 0;
    }
};

struct Movie;

struct Track {
    AVStream* stream = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* swr = nullptr;
    SwrContext* legacy_swr = nullptr;
    AVFrame* frame = nullptr;
    uint32_t id = 0;
    int stream_index = -1;
    bool enabled = false;
    bool legacy_open = false;
    int volume = 32768;
    int pan = 0;
    std::vector<uint32_t> mix_bins;
    std::vector<int> mix_volumes;
    AudioRing ring;
    std::vector<float> converted;
    std::vector<uint8_t> legacy_converted;
    std::deque<std::vector<uint8_t>> legacy_frames;
    size_t legacy_bytes = 0;
    uint32_t max_size = 0;
    uint64_t decoded_packets = 0;

    ~Track() {
        if (legacy_swr) swr_free(&legacy_swr);
        if (swr) swr_free(&swr);
        if (frame) av_frame_free(&frame);
        if (codec) avcodec_free_context(&codec);
    }
};

struct AvioState {
    Source* source = nullptr;
    int64_t position = 0;
};

void FreeAvio(AVIOContext*& avio) {
    if (!avio) return;
    av_freep(&avio->buffer);  // custom AVIO buffers are caller-owned, even when avformat_close_input is used
    avio_context_free(&avio);
}

int ReadPacket(void* opaque, uint8_t* dst, int bytes) {
    auto* io = static_cast<AvioState*>(opaque);
    if (!io || !io->source || bytes <= 0 || io->position < 0) return AVERROR(EIO);
    int64_t n = io->source->Read(static_cast<uint64_t>(io->position), dst, static_cast<size_t>(bytes));
    if (n > 0) io->position += n;
    return n < 0 ? AVERROR(EIO) : static_cast<int>(n);
}

int64_t SeekPacket(void* opaque, int64_t offset, int whence) {
    auto* io = static_cast<AvioState*>(opaque);
    if (!io || !io->source) return AVERROR(EIO);
    int64_t size = io->source->Size();
    if (whence == AVSEEK_SIZE) return size;
    int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = io->position; break;
        case SEEK_END: base = size; break;
        default: return AVERROR(EINVAL);
    }
    if (size < 0 || offset < -base || offset > size - base) return AVERROR(EINVAL);
    io->position = base + offset;
    return io->position;
}

struct Movie {
    std::timed_mutex mutex;
    uint32_t guest = 0;
    std::unique_ptr<Source> source;
    AvioState* avio_state = nullptr;
    AVIOContext* avio = nullptr;
    AVFormatContext* format = nullptr;
    AVCodecContext* video_codec = nullptr;
    AVFrame* video_frame = nullptr;
    AVFrame* presentation_frame = nullptr;  // last complete requested frame, retained across later decode deferrals
    AVPacket* read_packet = nullptr;
    AVStream* video_stream = nullptr;
    int video_index = -1;
    std::deque<AVPacket*> video_packets;
    std::vector<std::unique_ptr<Track>> tracks;
    std::vector<uint32_t> header_max_sizes;
    uint32_t flags = 0;
    uint32_t width = 0, height = 0, frames = 0;
    uint32_t decoded_position = 0;  // number of frames decoded from the current demux position
    uint32_t registered_buffers = 0;
    bool read_eof = false, drain_sent = false, closed = false;
    bool video_on = true, sound_on = true;
    bool paused = false;
    bool audio_anchored = false;
    bool background_io = true;
    uint32_t pending_snd_size = 0;
    uint32_t current_packet_size = 0;
    uint32_t largest_packet = 0;
    uint64_t played_frames = 0, anchor_played = 0;
    uint32_t audio_rate = kAudioRate;
    uint64_t total_decodes = 0, skipped_frames = 0;
    uint64_t deferred_calls = 0;
    uint32_t plane_copy_logs = 0;
    uint64_t decode_us = 0, audio_decode_us = 0;
    Clock::time_point start = Clock::now();
    Clock::time_point anchor_mono = start;
    double paused_seconds = 0.0;
    std::string description;

    Public* Pub() { return Ptr<Public>(guest); }
    const Public* Pub() const { return Ptr<Public>(guest); }

    double FrameSeconds() const {
        const Public* p = Pub();
        return p && p->frame_rate && p->frame_rate_div ? double(p->frame_rate_div) / p->frame_rate : 1.0 / 30.0;
    }
    double MonotonicSeconds(Clock::time_point now) const {
        return std::chrono::duration<double>(now - start).count();
    }
    double MediaSeconds(Clock::time_point now) const {
        if (paused) return paused_seconds;
        double mono = std::max(0.0, MonotonicSeconds(now));
        if (!audio_anchored || !played_frames) return mono;
        double audio = double(anchor_played) / std::max(1u, audio_rate) +
                       std::chrono::duration<double>(now - anchor_mono).count();
        // Audio is the low-jitter clock while it agrees with wall time. A device stall, underrun or Bluetooth route
        // change must not make video race arbitrarily far ahead or freeze behind an invalid audio timestamp.
        return std::abs(audio - mono) > 0.25 ? mono : audio;
    }
    void ClearAudioClock() {
        audio_anchored = false;
        played_frames = anchor_played = 0;
        anchor_mono = Clock::now();
    }
};

std::mutex g_registry_mutex;
std::unordered_map<uint32_t, std::shared_ptr<Movie>> g_movies;
std::unordered_map<uint32_t, std::pair<std::shared_ptr<Movie>, size_t>> g_open_tracks;
std::vector<uint32_t> g_selected_tracks;
bool g_selected_tracks_set = false;
uint32_t g_sound_system_open = 0, g_sound_system_param = 0;
uint32_t g_force_rate = 0, g_force_rate_div = 1;
uint32_t g_io_size = 64 * 1024;
uint32_t g_custom_io_open = 0;
bool g_simulate = false;

struct AudioOutput {
    std::once_flag once;
    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec {};
    std::vector<float> mix;
    std::vector<std::vector<float>> bins;
};
AudioOutput g_audio;

void UpdateDeviceState() {
    if (!g_audio.device) return;
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    SDL_PauseAudioDevice(g_audio.device, g_movies.empty() ? 1 : 0);
}

void SDLCALL AudioCallback(void*, Uint8* stream, int length) {
    if (length <= 0) return;
    memset(stream, 0, static_cast<size_t>(length));
    size_t frames = static_cast<size_t>(length) / (sizeof(float) * 2);
    if (g_audio.mix.size() < frames * 2) g_audio.mix.resize(frames * 2);
    std::fill(g_audio.mix.begin(), g_audio.mix.begin() + frames * 2, 0.0f);
    if (g_audio.bins.size() != 8) g_audio.bins.resize(8);
    for (auto& bin : g_audio.bins) {
        if (bin.size() < frames * 2) bin.resize(frames * 2);
        std::fill(bin.begin(), bin.begin() + frames * 2, 0.0f);
    }

    std::vector<std::shared_ptr<Movie>> movies;
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        for (const auto& entry : g_movies) movies.push_back(entry.second);
    }
    for (const auto& movie : movies) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        if (movie->closed || movie->paused) continue;
        size_t consumed_max = 0;
        for (auto& track_ptr : movie->tracks) {
            Track& track = *track_ptr;
            if (!track.enabled || !track.ring.frames) continue;
            size_t count = std::min(frames, track.ring.frames);
            float base_gain = track.volume / 32768.0f;
            float pan = std::clamp(track.pan / 32768.0f, -1.0f, 1.0f);
            float gain_l = base_gain * (pan > 0 ? 1.0f - pan : 1.0f);
            float gain_r = base_gain * (pan < 0 ? 1.0f + pan : 1.0f);
            if (!movie->sound_on) gain_l = gain_r = 0.0f;
            if (track.mix_bins.empty()) {
                for (size_t i = 0; i < count; ++i) {
                    g_audio.mix[i * 2] += track.ring.At(i, 0) * gain_l;
                    g_audio.mix[i * 2 + 1] += track.ring.At(i, 1) * gain_r;
                }
            } else {
                for (size_t b = 0; b < track.mix_bins.size() && b < 8; ++b) {
                    unsigned bin = track.mix_bins[b] & 7;
                    float bin_gain = b < track.mix_volumes.size() ? track.mix_volumes[b] / 32768.0f : 1.0f;
                    for (size_t i = 0; i < count; ++i) {
                        g_audio.bins[bin][i * 2] += track.ring.At(i, 0) * gain_l * bin_gain;
                        g_audio.bins[bin][i * 2 + 1] += track.ring.At(i, 1) * gain_r * bin_gain;
                    }
                }
            }
            track.ring.Consume(count);
            consumed_max = std::max(consumed_max, count);
        }
        if (consumed_max) {
            movie->played_frames += consumed_max;
            movie->anchor_played = movie->played_frames;
            movie->anchor_mono = Clock::now();
            movie->audio_anchored = true;
        }
    }
    for (const auto& bin : g_audio.bins)
        for (size_t i = 0; i < frames * 2; ++i) g_audio.mix[i] += bin[i];
    auto* out = reinterpret_cast<float*>(stream);
    for (size_t i = 0; i < frames * 2; ++i) out[i] = std::clamp(g_audio.mix[i], -1.0f, 1.0f);
}

void EnsureAudioOutput() {
    std::call_once(g_audio.once, [] {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            Log("[bink] SDL audio unavailable: %s; using monotonic video timing\n", SDL_GetError());
            return;
        }
        SDL_AudioSpec want {};
        want.freq = kAudioRate;
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = AudioCallback;
        g_audio.device = SDL_OpenAudioDevice(nullptr, 0, &want, &g_audio.spec, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
        if (!g_audio.device) {
            Log("[bink] SDL_OpenAudioDevice failed: %s; using monotonic video timing\n", SDL_GetError());
            return;
        }
        if (g_audio.spec.format != AUDIO_F32SYS || g_audio.spec.channels != 2) {
            Log("[bink] unexpected SDL audio format; disabling movie mixing\n");
            SDL_CloseAudioDevice(g_audio.device);
            g_audio.device = 0;
            return;
        }
        if (g_audio.spec.freq <= 0 || g_audio.spec.freq > 768000) {
            Log("[bink] SDL granted an invalid audio rate %d Hz; disabling movie audio\n", g_audio.spec.freq);
            SDL_CloseAudioDevice(g_audio.device);
            g_audio.device = 0;
            return;
        }
        g_output_rate.store(static_cast<uint32_t>(g_audio.spec.freq), std::memory_order_relaxed);
        Log("[bink] SDL movie audio: %d Hz, stereo float\n", g_audio.spec.freq);
        SDL_PauseAudioDevice(g_audio.device, 1);
    });
}

std::shared_ptr<Movie> FindMovie(uint32_t handle) {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = g_movies.find(handle);
    return it == g_movies.end() ? nullptr : it->second;
}

Track* TrackAtIndex(const std::shared_ptr<Movie>& movie, uint32_t index, size_t* index_out = nullptr) {
    if (!movie || index >= movie->tracks.size()) return nullptr;
    if (index_out) *index_out = index;
    return movie->tracks[index].get();
}

Track* FindTrack(const std::shared_ptr<Movie>& movie, uint32_t id_or_index, size_t* index_out = nullptr) {
    if (!movie) return nullptr;
    for (size_t i = 0; i < movie->tracks.size(); ++i) {
        if (movie->tracks[i]->id == id_or_index) {
            if (index_out) *index_out = i;
            return movie->tracks[i].get();
        }
    }
    return nullptr;
}

bool TrackSelected(const Track& track, size_t index) {
    if (!g_selected_tracks_set) return index == 0;
    for (uint32_t id : g_selected_tracks)
        if (id == track.id) return true;
    return false;
}

void ApplyTrackSelection(const std::shared_ptr<Movie>& movie) {
    std::lock_guard<std::timed_mutex> lock(movie->mutex);
    for (size_t i = 0; i < movie->tracks.size(); ++i) {
        Track& track = *movie->tracks[i];
        track.enabled = TrackSelected(track, i);
        if (!track.enabled) track.ring.Clear();
    }
}

void FreeVideoPackets(Movie& movie) {
    for (AVPacket* p : movie.video_packets) av_packet_free(&p);
    movie.video_packets.clear();
}

bool EnsureResampler(Track& track) {
    if (track.swr || !track.codec) return track.swr != nullptr;
    uint32_t output_rate = OutputRate();
    AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout in = {};
    av_channel_layout_copy(&in, &track.codec->ch_layout);
    if (!in.nb_channels) av_channel_layout_default(&in, 1);
    int r = swr_alloc_set_opts2(&track.swr, &out, AV_SAMPLE_FMT_FLT, output_rate, &in,
                                track.codec->sample_fmt, track.codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&in);
    if (r < 0 || !track.swr) return false;
    r = swr_init(track.swr);
    if (r < 0) {
        swr_free(&track.swr);
        return false;
    }
    track.ring.Reset(output_rate);
    return true;
}

bool EnsureLegacyResampler(Track& track) {
    if (track.legacy_swr || !track.codec) return track.legacy_swr != nullptr;
    AVChannelLayout layout = {};
    av_channel_layout_copy(&layout, &track.codec->ch_layout);
    if (!layout.nb_channels) av_channel_layout_default(&layout, 1);
    int rate = std::max(1, track.codec->sample_rate);
    int r = swr_alloc_set_opts2(&track.legacy_swr, &layout, AV_SAMPLE_FMT_S16, rate, &layout,
                                track.codec->sample_fmt, rate, 0, nullptr);
    av_channel_layout_uninit(&layout);
    if (r < 0 || !track.legacy_swr) return false;
    if (swr_init(track.legacy_swr) < 0) {
        swr_free(&track.legacy_swr);
        return false;
    }
    return true;
}

void QueueLegacyPcm(Track& track, const uint8_t* data, size_t bytes) {
    if (!bytes) return;
    constexpr size_t kMaxLegacyBytes = 16 * 1024 * 1024;
    track.legacy_frames.emplace_back(data, data + bytes);
    track.legacy_bytes += bytes;
    while (track.legacy_bytes > kMaxLegacyBytes && !track.legacy_frames.empty()) {
        track.legacy_bytes -= track.legacy_frames.front().size();
        track.legacy_frames.pop_front();
    }
}

void DecodeAudioFrame(Movie& movie, Track& track) {
    (void)movie;
    if (!track.frame || !track.frame->nb_samples) return;
    int rate = track.frame->sample_rate ? track.frame->sample_rate : track.codec->sample_rate;
    if (rate <= 0) return;
    const uint8_t** in = const_cast<const uint8_t**>(track.frame->extended_data);
    if (EnsureResampler(track)) {
        uint32_t output_rate = OutputRate();
        int out_samples = static_cast<int>(av_rescale_rnd(track.frame->nb_samples, output_rate, rate, AV_ROUND_UP));
        if (out_samples > 0 && int64_t(out_samples) <= int64_t(output_rate) * 4) {
            track.converted.resize(static_cast<size_t>(out_samples) * 2);
            uint8_t* out[] = {reinterpret_cast<uint8_t*>(track.converted.data())};
            int got = swr_convert(track.swr, out, out_samples, in, track.frame->nb_samples);
            if (got > 0 && track.enabled && g_audio.device) track.ring.Push(track.converted.data(), got);
        }
    }
    if (track.legacy_open && EnsureLegacyResampler(track)) {
        int channels = std::max(1, track.codec->ch_layout.nb_channels);
        int out_samples = track.frame->nb_samples + 32;
        track.legacy_converted.resize(static_cast<size_t>(out_samples) * channels * sizeof(int16_t));
        uint8_t* out[] = {track.legacy_converted.data()};
        int got = swr_convert(track.legacy_swr, out, out_samples, in, track.frame->nb_samples);
        if (got > 0) QueueLegacyPcm(track, track.legacy_converted.data(),
                                    static_cast<size_t>(got) * channels * sizeof(int16_t));
    }
    ++track.decoded_packets;
}

void DecodeAudioPacket(Movie& movie, Track& track, AVPacket* packet) {
    if (!track.codec || (!track.enabled && !track.legacy_open)) return;
    auto t0 = Clock::now();
    int r = avcodec_send_packet(track.codec, packet);
    if (r >= 0) {
        while ((r = avcodec_receive_frame(track.codec, track.frame)) >= 0) {
            DecodeAudioFrame(movie, track);
            av_frame_unref(track.frame);
        }
    }
    movie.audio_decode_us += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count());
}

// Reads only as far as necessary. Video packets are retained in decode order; disabled audio packets are discarded.
bool PumpOnePacket(Movie& movie) {
    if (movie.read_eof) return false;
    int r = av_read_frame(movie.format, movie.read_packet);
    if (r < 0) {
        // A temporarily unavailable source is not EOF: retain the demux/decoder state and let the caller's deadline
        // turn repeated EAGAIN results into a deferred frame instead of spinning forever.
        if (r != AVERROR(EAGAIN)) movie.read_eof = true;
        return false;
    }
    if (movie.read_packet->stream_index == movie.video_index) {
        AVPacket* p = av_packet_alloc();
        if (!p) {
            av_packet_unref(movie.read_packet);
            movie.read_eof = true;
            return false;
        }
        av_packet_move_ref(p, movie.read_packet);
        movie.largest_packet = std::max<uint32_t>(movie.largest_packet, p->size);
        movie.video_packets.push_back(p);
    } else {
        for (auto& track : movie.tracks) {
            if (track->stream_index == movie.read_packet->stream_index) {
                movie.pending_snd_size += static_cast<uint32_t>(movie.read_packet->size);
                DecodeAudioPacket(movie, *track, movie.read_packet);
                break;
            }
        }
    }
    av_packet_unref(movie.read_packet);
    return true;
}

// 1 = decoded a frame, 0 = fatal/EOF, -1 = the foreground work budget expired. FFmpeg state is deliberately kept
// intact in the deferred case, so the next DoFrame resumes at the same packet/decoder boundary.
int DecodeOneVideo(Movie& movie, Clock::time_point deadline) {
    for (unsigned guard = 0; guard < 4096; ++guard) {
        if (Clock::now() >= deadline) return -1;
        int r = avcodec_receive_frame(movie.video_codec, movie.video_frame);
        if (r == 0) return 1;
        if (r != AVERROR(EAGAIN) && r != AVERROR_EOF) return 0;
        if (r == AVERROR_EOF) return 0;
        if (movie.video_packets.empty()) {
            if (movie.read_eof) {
                if (movie.drain_sent) return 0;
                movie.drain_sent = true;
                r = avcodec_send_packet(movie.video_codec, nullptr);
                if (r < 0 && r != AVERROR_EOF) return 0;
                continue;
            }
            if (!PumpOnePacket(movie) && !movie.read_eof) std::this_thread::yield();
            continue;
        }
        AVPacket* p = movie.video_packets.front();
        movie.video_packets.pop_front();
        movie.current_packet_size = static_cast<uint32_t>(p->size);
        r = avcodec_send_packet(movie.video_codec, p);
        av_packet_free(&p);
        if (r < 0) return 0;
    }
    return 0;
}

bool RewindLocked(Movie& movie, uint32_t target_frame) {
    FreeVideoPackets(movie);
    if (movie.format && av_seek_frame(movie.format, -1, 0, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(movie.video_codec);
    for (auto& track : movie.tracks) {
        if (track->codec) avcodec_flush_buffers(track->codec);
        if (track->swr) swr_init(track->swr);
        if (track->legacy_swr) swr_init(track->legacy_swr);
        track->ring.Clear();
        track->legacy_frames.clear();
        track->legacy_bytes = 0;
    }
    movie.read_eof = movie.drain_sent = false;
    movie.decoded_position = 0;
    movie.pending_snd_size = 0;
    movie.ClearAudioClock();
    movie.start = Clock::now() - std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>((target_frame > 0 ? target_frame - 1 : 0) * movie.FrameSeconds()));
    return true;
}

void EnsureAudioLeadLocked(Movie& movie, Clock::time_point deadline) {
    if (!g_audio.device || movie.paused || !movie.sound_on || !movie.background_io) return;
    size_t target = OutputRate() / 4;  // 250 ms: enough to absorb decode and Android audio scheduling jitter
    bool needs = false;
    for (auto& track : movie.tracks)
        if (track->enabled && track->ring.frames < target) needs = true;
    for (unsigned steps = 0; needs && !movie.read_eof && steps < 512 && movie.video_packets.size() < 128 &&
                            Clock::now() < deadline; ++steps) {
        if (!PumpOnePacket(movie) && !movie.read_eof) std::this_thread::yield();
        needs = false;
        for (auto& track : movie.tracks)
            if (track->enabled && track->ring.frames < target) needs = true;
    }
}

std::vector<uint32_t> ReadHeaderMaxSizes(Source& source) {
    std::vector<uint32_t> result;
    int64_t source_size = source.Size();
    if (source_size < 48) return result;
    size_t bytes = static_cast<size_t>(std::min<int64_t>(source_size, 128 * 1024));
    std::vector<uint8_t> data(bytes);
    if (source.Read(0, data.data(), data.size()) != static_cast<int64_t>(data.size())) return result;
    for (size_t offset = 0; offset + 48 <= data.size(); offset += 512) {
        const uint8_t* h = data.data() + offset;
        if (!(h[0] == 'B' && h[1] == 'I' && h[2] == 'K')) continue;
        uint32_t tracks = AV_RL32(h + 40);
        if (tracks > 256) return result;
        uint32_t tag = AV_RL32(h);
        bool extra = ((tag & 0xffffff) == AV_RL32("BIK") && ((tag >> 24) % 0xff) == 'k') ||
                     ((tag & 0xffffff) == AV_RL32("KB2") && (((tag >> 24) % 0xff) == 'i' ||
                      ((tag >> 24) % 0xff) == 'j' || ((tag >> 24) % 0xff) == 'k'));
        size_t at = offset + 44 + (extra ? 4 : 0);
        if (at > data.size() || uint64_t(tracks) * 4 > data.size() - at) return result;
        result.reserve(tracks);
        for (uint32_t i = 0; i < tracks; ++i) result.push_back(AV_RL32(data.data() + at + i * 4));
        return result;
    }
    return result;
}

void FillPublic(Movie& movie) {
    Public* p = movie.Pub();
    p->width = movie.width;
    p->height = movie.height;
    p->frames = movie.frames;
    p->frame_num = 1;
    p->last_frame_num = 0;
    p->read_error = 0;
    p->open_flags = movie.flags;
    p->size = static_cast<uint32_t>(std::max<int64_t>(0, movie.source->Size()));
    p->frame_size = p->snd_size = 0;
    p->frame_change_percent = 0;
    p->num_rects = 0;
    p->frame_buffers = 0;
    p->num_tracks = static_cast<int32_t>(movie.tracks.size());
    p->largest_frame_size = 0;
    p->internal_frames = movie.frames;
    p->highest_1sec_rate = 0;
    p->highest_1sec_frame = 0;
    p->paused = 0;
    p->bink_type = 0;
    if (movie.video_stream && movie.video_stream->codecpar->extradata &&
        movie.video_stream->codecpar->extradata_size >= 4)
        p->bink_type = AV_RL32(movie.video_stream->codecpar->extradata);
    AVRational rate = movie.video_stream ? movie.video_stream->avg_frame_rate : AVRational{0, 1};
    if (g_force_rate && (movie.flags & kFrameRate)) rate = {static_cast<int>(g_force_rate), static_cast<int>(g_force_rate_div)};
    if (rate.num <= 0 || rate.den <= 0) rate = {30, 1};
    p->frame_rate = static_cast<uint32_t>(rate.num);
    p->frame_rate_div = static_cast<uint32_t>(rate.den);
    p->mask_plane = p->mask_pitch = p->mask_length = 0;
    p->async_mask_plane = p->in_use_mask_plane = p->last_mask_plane = 0;
}

uint32_t OpenMovie(const char* name, uint32_t flags) {
    if ((flags & kFromMemory) && (flags & kFileHandle)) return 0;
    std::string error;
    std::unique_ptr<Source> source;
    if (flags & kFromMemory) {
        source = OpenMemorySource(name, flags, &error);
    } else if ((flags & kFileHandle) || !g_custom_io_open) {
        source = OpenFileSource(name, flags, &error);
    } else {
        // Resolve real loose/RCF media directly even when the game installs BINKIO. Generic FFmpeg reads must not
        // make thousands of synchronous guest ReadHeader calls; only synthetic/unresolvable names need callbacks.
        source = OpenFileSource(name, flags, &error);
        if (!source) {
            Log("[bink] host input unavailable for %s (%s); using guest BINKIO fallback\n", name ? name : "<null>",
                error.c_str());
            source = OpenCallbackSource(g_custom_io_open, name, flags, g_io_size, g_simulate, &error);
        }
    }
    if (!source) {
        Log("[bink] open failed for %s: %s\n", (flags & (kFromMemory | kFileHandle)) ? "<non-name input>" : name ? name : "<null>",
            error.c_str());
        return 0;
    }
    EnsureAudioOutput();
    auto movie = std::make_shared<Movie>();
    movie->audio_rate = OutputRate();
    movie->source = std::move(source);
    movie->header_max_sizes = ReadHeaderMaxSizes(*movie->source);
    movie->flags = flags;
    movie->description = movie->source->Description();
    movie->guest = HeapAlloc(kGuestAllocation);
    if (!movie->guest) return 0;
    memset(movie->Pub(), 0, kGuestAllocation);
    movie->format = avformat_alloc_context();
    movie->avio_state = new AvioState{movie->source.get(), 0};
    size_t preferred_io = movie->source->PreferredAVIOBufferSize();
    int io_size = static_cast<int>(preferred_io ? preferred_io
                                                : std::clamp<uint32_t>(g_io_size, 4096, 1024 * 1024));
    auto* io_buffer = static_cast<unsigned char*>(av_malloc(io_size));
    movie->avio = avio_alloc_context(io_buffer, io_size, 0, movie->avio_state, ReadPacket, nullptr, SeekPacket);
    if (!movie->format || !movie->avio) {
        HeapFree(movie->guest);
        delete movie->avio_state;
        if (movie->avio) FreeAvio(movie->avio);
        else av_free(io_buffer);
        if (movie->format) avformat_free_context(movie->format);
        return 0;
    }
    Log("[bink] %s input %s: %lld bytes, AVIO %d KiB\n", movie->source->HostDirect() ? "host direct" : "bounded",
        movie->description.c_str(), static_cast<long long>(movie->source->Size()), io_size / 1024);
    movie->avio->seekable = AVIO_SEEKABLE_NORMAL;
    movie->format->pb = movie->avio;
    movie->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    const AVInputFormat* input = av_find_input_format("bink");
    int r = avformat_open_input(&movie->format, "", const_cast<AVInputFormat*>(input), nullptr);
    if (r < 0) {
        Log("[bink] demux failed: %s\n", AVError(r).c_str());
        movie->closed = true;
        // The destructor below owns all partially initialized FFmpeg state.
    } else {
        for (unsigned i = 0; i < movie->format->nb_streams; ++i) {
            AVStream* stream = movie->format->streams[i];
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && movie->video_index < 0) {
                movie->video_index = static_cast<int>(i);
                movie->video_stream = stream;
            }
        }
        if (movie->video_index < 0) r = AVERROR_STREAM_NOT_FOUND;
    }
    if (r >= 0) {
        const AVCodec* codec = avcodec_find_decoder(movie->video_stream->codecpar->codec_id);
        movie->video_codec = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (movie->video_codec) {
            unsigned cores = std::thread::hardware_concurrency();
            movie->video_codec->thread_count = static_cast<int>(cores ? std::clamp(cores, 1u, 4u) : 1u);
            movie->video_codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
            r = avcodec_parameters_to_context(movie->video_codec, movie->video_stream->codecpar);
            if (r >= 0) r = avcodec_open2(movie->video_codec, codec, nullptr);
        } else r = AVERROR_DECODER_NOT_FOUND;
    }
    if (r >= 0) {
        movie->width = static_cast<uint32_t>(movie->video_codec->width);
        movie->height = static_cast<uint32_t>(movie->video_codec->height);
        movie->frames = movie->format->nb_streams > static_cast<unsigned>(movie->video_index)
                            ? static_cast<uint32_t>(movie->video_stream->nb_frames) : 0;
        if (!movie->frames && movie->video_stream->duration > 0 && movie->video_stream->avg_frame_rate.num > 0)
            movie->frames = static_cast<uint32_t>(av_rescale_q(movie->video_stream->duration,
                        movie->video_stream->time_base, av_inv_q(movie->video_stream->avg_frame_rate)));
        if (!movie->width || !movie->height || !movie->frames || movie->width > 16384 || movie->height > 16384 ||
            uint64_t(movie->width) * movie->height > 268435456ull || movie->frames > 10000000)
            r = AVERROR_INVALIDDATA;
    }
    if (r >= 0) {
        movie->video_frame = av_frame_alloc();
        movie->presentation_frame = av_frame_alloc();
        movie->read_packet = av_packet_alloc();
        if (!movie->video_frame || !movie->presentation_frame || !movie->read_packet) r = AVERROR(ENOMEM);
    }
    if (r >= 0) {
        for (unsigned i = 0; i < movie->format->nb_streams && movie->tracks.size() < kMaxTracks; ++i) {
            AVStream* stream = movie->format->streams[i];
            if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec) continue;
            auto track = std::make_unique<Track>();
            track->stream = stream;
            track->stream_index = static_cast<int>(i);
            track->id = stream->id >= 0 ? static_cast<uint32_t>(stream->id) : i;
            track->codec = avcodec_alloc_context3(codec);
            track->frame = av_frame_alloc();
            if (!track->codec || !track->frame || avcodec_parameters_to_context(track->codec, stream->codecpar) < 0 ||
                avcodec_open2(track->codec, codec, nullptr) < 0) continue;
            track->codec->thread_count = 1;
            size_t audio_index = movie->tracks.size();
            uint32_t channels = std::max(1, track->codec->ch_layout.nb_channels);
            uint64_t fallback = uint64_t(std::max(1, track->codec->sample_rate)) * channels * 2 * 2;
            track->max_size = audio_index < movie->header_max_sizes.size() && movie->header_max_sizes[audio_index]
                                  ? movie->header_max_sizes[audio_index]
                                  : static_cast<uint32_t>(std::clamp<uint64_t>(fallback, 4096, 16 * 1024 * 1024));
            track->ring.Reset(OutputRate());
            movie->tracks.push_back(std::move(track));
        }
        FillPublic(*movie);
        movie->paused = false;
        movie->start = Clock::now();
        for (size_t i = 0; i < movie->tracks.size(); ++i) movie->tracks[i]->enabled = TrackSelected(*movie->tracks[i], i);
    }
    if (r < 0) {
        Log("[bink] decoder setup failed for %s: %s\n", movie->description.c_str(), AVError(r).c_str());
        // Explicitly tear down here; this also covers avformat_open_input's ownership rules on failure.
        FreeVideoPackets(*movie);
        if (movie->read_packet) av_packet_free(&movie->read_packet);
        if (movie->video_frame) av_frame_free(&movie->video_frame);
        if (movie->presentation_frame) av_frame_free(&movie->presentation_frame);
        if (movie->video_codec) avcodec_free_context(&movie->video_codec);
        movie->tracks.clear();
        if (movie->format) avformat_close_input(&movie->format);
        FreeAvio(movie->avio);
        delete movie->avio_state;
        HeapFree(movie->guest);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        g_movies[movie->guest] = movie;
    }
    UpdateDeviceState();
    Log("[bink] opened %s: %ux%u, %u frames, %zu audio tracks, video threads=%d type=0x%x active=0x%x\n",
        movie->description.c_str(), movie->width, movie->height, movie->frames, movie->tracks.size(),
        movie->video_codec->thread_count, movie->video_codec->thread_type, movie->video_codec->active_thread_type);
    return movie->guest;
}

void DestroyMovie(const std::shared_ptr<Movie>& movie) {
    if (!movie) return;
    {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        movie->closed = true;
        FreeVideoPackets(*movie);
        if (movie->read_packet) av_packet_free(&movie->read_packet);
        if (movie->video_frame) av_frame_free(&movie->video_frame);
        if (movie->presentation_frame) av_frame_free(&movie->presentation_frame);
        if (movie->video_codec) avcodec_free_context(&movie->video_codec);
        movie->tracks.clear();
        if (movie->format) avformat_close_input(&movie->format);
        FreeAvio(movie->avio);
        delete movie->avio_state;
        movie->avio_state = nullptr;
    }
    HeapFree(movie->guest);
    movie->guest = 0;
}

void LogFrameBufferSets(Movie& movie, const char* context) {
    const Public* p = movie.Pub();
    const auto* b = Ptr<FrameBuffers>(movie.registered_buffers);
    if (!p || !b) return;
    Log("[bink] fbsets %s buffers=%08x frame=%u active=%u total=%d YA=%ux%u cRcB=%ux%u\n", context,
        movie.registered_buffers, p->frame_num, b->frame_num, b->total_frames, b->ya_width, b->ya_height,
        b->crcb_width, b->crcb_height);
    for (unsigned index = 0; index < 2; ++index) {
        const FramePlaneSet& s = b->frames[index];
        Log("[bink] fbset[%u]: Y=(alloc=%u buf=%08x pitch=%d) cR=(alloc=%u buf=%08x pitch=%d) "
            "cB=(alloc=%u buf=%08x pitch=%d) A=(alloc=%u buf=%08x pitch=%d)\n", index,
            s.y.allocate, s.y.buffer, static_cast<int32_t>(s.y.pitch), s.cr.allocate, s.cr.buffer,
            static_cast<int32_t>(s.cr.pitch), s.cb.allocate, s.cb.buffer, static_cast<int32_t>(s.cb.pitch),
            s.alpha.allocate, s.alpha.buffer, static_cast<int32_t>(s.alpha.pitch));
    }
}

void LogPlaneCopy(Movie& movie, const AVFrame& frame, const FrameBuffers& buffers) {
    if (movie.plane_copy_logs >= 3 || !movie.Pub() || movie.Pub()->frame_num > 3) return;
    ++movie.plane_copy_logs;
    LogFrameBufferSets(movie, "copy");
    unsigned alpha = frame.data[3] ? frame.data[3][0] : 255;
    Log("[bink] plane copy frame %u active=%u total=%d src Y/Cb/Cr/A=%u/%u/%u/%u\n",
        movie.Pub()->frame_num, buffers.frame_num, buffers.total_frames,
        frame.data[0][0], frame.data[1][0], frame.data[2][0], alpha);
    for (unsigned index = 0; index < static_cast<unsigned>(buffers.total_frames); ++index) {
        const FramePlaneSet& p = buffers.frames[index];
        if (!p.y.buffer && !p.cr.buffer && !p.cb.buffer && !p.alpha.buffer) {
            Log("[bink]  set %u empty\n", index);
            continue;
        }
        int a = p.alpha.buffer ? *Ptr<uint8_t>(p.alpha.buffer) : -1;
        Log("[bink]  set %u%s Y=%08x/%d=%u Cb=%08x/%d=%u Cr=%08x/%d=%u A=%08x/%d=%d\n", index,
            index == buffers.frame_num ? " (locked)" : "", p.y.buffer, static_cast<int32_t>(p.y.pitch),
            *Ptr<uint8_t>(p.y.buffer), p.cb.buffer, static_cast<int32_t>(p.cb.pitch), *Ptr<uint8_t>(p.cb.buffer),
            p.cr.buffer, static_cast<int32_t>(p.cr.pitch), *Ptr<uint8_t>(p.cr.buffer), p.alpha.buffer,
            static_cast<int32_t>(p.alpha.pitch), a);
    }
}

bool UploadRegisteredLocked(Movie& movie, const AVFrame* frame, bool decoded) {
    if (!movie.registered_buffers) return true;
    Public* p = movie.Pub();
    auto* buffers = Ptr<FrameBuffers>(movie.registered_buffers);
    if (!p || !buffers || !CopyRegisteredFrame(frame, *p, *buffers)) {
        Log("[bink] rejected registered plane upload: buffers=%08x active=%u total=%d frame=%u\n",
            movie.registered_buffers, buffers ? buffers->frame_num : 0, buffers ? buffers->total_frames : 0,
            p ? p->frame_num : 0);
        LogFrameBufferSets(movie, "rejected");
        return false;
    }
    if (decoded && frame) LogPlaneCopy(movie, *frame, *buffers);
    return true;
}

// 1 = presented, 0 = failed, -1 = deferred. A deferred frame does not clear last_frame_num, frame_size or rectangles:
// the guest can unlock/represent the previous complete registered planes while decoding resumes on a later call.
int DecodeCurrentFrame(Movie& movie, Clock::time_point deadline) {
    Public* p = movie.Pub();
    if (!p || movie.closed || !movie.video_on) return 0;
    if (movie.decoded_position > p->frame_num) {
        if (!RewindLocked(movie, p->frame_num)) return 0;
    }
    auto t0 = Clock::now();
    unsigned guard = 0;
    while (movie.decoded_position < p->frame_num && guard++ <= movie.frames + 1) {
        if (Clock::now() >= deadline) return -1;
        int decoded = DecodeOneVideo(movie, deadline);
        if (decoded < 0) return -1;
        if (!decoded) {
            p->read_error = 1;
            return 0;
        }
        ++movie.decoded_position;
        // Only the requested frame is exposed; skipped frames are deliberately not copied into the registered set.
        if (movie.decoded_position < p->frame_num) av_frame_unref(movie.video_frame);
    }
    if (!movie.decoded_position || movie.decoded_position != p->frame_num) return 0;
    av_frame_unref(movie.presentation_frame);
    if (av_frame_ref(movie.presentation_frame, movie.video_frame) < 0) {
        p->read_error = 1;
        return 0;
    }
    if (!UploadRegisteredLocked(movie, movie.video_frame, true)) {
        p->read_error = 1;
        return 0;
    }
    // Publish metadata only after the live guest planes contain the same complete presentation.
    p->last_frame_num = p->frame_num;
    p->frame_size = movie.current_packet_size;
    p->snd_size = movie.pending_snd_size;
    movie.pending_snd_size = 0;
    p->largest_frame_size = movie.largest_packet;
    p->frame_change_percent = 100;
    p->num_rects = 1;
    p->frame_rects[0] = {0, 0, static_cast<int32_t>(movie.width), static_cast<int32_t>(movie.height)};
    movie.total_decodes++;
    movie.decode_us += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count());
    return 1;
}

thread_local std::string tl_bink_error = "no error";
thread_local uint32_t tl_bink_error_addr = 0;
uint32_t ErrorAddress() {
    if (!tl_bink_error_addr) tl_bink_error_addr = HeapAlloc(1024);
    if (tl_bink_error_addr) {
        strncpy(Str(tl_bink_error_addr), tl_bink_error.c_str(), 1023);
        Str(tl_bink_error_addr)[1023] = 0;
    }
    return tl_bink_error_addr;
}

int RunDoFrame(Movie& movie) {
    std::unique_lock<std::timed_mutex> lock(movie.mutex, std::defer_lock);
    if (!lock.try_lock_for(kLockBudget)) return -1;  // never turn a busy decoder into a guest-thread deadlock
    Public* p = movie.Pub();
    if (!p) return 0;
    // Frame 1 must complete before the guest's first UnlockRect. Later frames use a short foreground budget and can
    // safely present the retained frame while the guest performs its own bounded NextFrame/DoFrame retry loop.
    Clock::time_point deadline = p->frame_num == 1 ? Clock::time_point::max() : Clock::now() + kDecodeBudget;
    EnsureAudioLeadLocked(movie, std::min(deadline, Clock::now() + kAudioBudget));
    int result = DecodeCurrentFrame(movie, deadline);
    if (result < 0) {
        const AVFrame* retained = movie.presentation_frame && movie.presentation_frame->data[0]
                                      ? movie.presentation_frame : nullptr;
        if (!UploadRegisteredLocked(movie, retained, false)) {
            p->read_error = 1;
            return 0;
        }
        ++movie.deferred_calls;
        ++movie.skipped_frames;
        if (movie.deferred_calls == 1 || movie.deferred_calls % 128 == 0)
            Log("[bink] deferred frame %u after %llu call(s); uploaded %s planes\n", p->frame_num,
                static_cast<unsigned long long>(movie.deferred_calls), retained ? "retained" : "neutral");
        return -1;
    }
    movie.deferred_calls = 0;
    return result;
}

void SleepUntilFrame(Movie& movie) {
    Clock::time_point due;
    {
        std::unique_lock<std::timed_mutex> lock(movie.mutex, std::defer_lock);
        if (!lock.try_lock_for(kLockBudget) || movie.paused || movie.closed) return;
        double seconds = movie.Pub()->frame_num * movie.FrameSeconds();
        double delay = seconds - movie.MediaSeconds(Clock::now());
        if (delay <= 0) return;
        // With zero audio tracks, no selected track, or no audio device, MediaSeconds uses the monotonic clock. Pace
        // those movies too, and avoid sub-millisecond early-return spins around a frame boundary.
        double bounded = std::clamp(delay, 0.001, 0.1);
        due = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(bounded));
    }
    if (due > Clock::now()) std::this_thread::sleep_until(due);
}

#define B(name) void b_##name(Ctx* c)

B(BinkOpen) {
    uint32_t address = Arg(c, 0), flags = Arg(c, 1);
    const char* input = (flags & (kFromMemory | kFileHandle))
                            ? reinterpret_cast<const char*>(static_cast<uintptr_t>(address))
                            : address ? Str(address) : nullptr;
    uint32_t result = OpenMovie(input, flags);
    if (!result) tl_bink_error = "BinkOpen could not open or decode the movie";
    Return(c, result, 2);
}
B(BinkClose) {
    uint32_t handle = Arg(c, 0);
    std::shared_ptr<Movie> movie;
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        auto it = g_movies.find(handle);
        if (it != g_movies.end()) {
            movie = it->second;
            g_movies.erase(it);
        }
        for (auto it = g_open_tracks.begin(); it != g_open_tracks.end();) {
            if (it->second.first.get() == movie.get()) {
                HeapFree(it->first);
                it = g_open_tracks.erase(it);
            } else ++it;
        }
    }
    DestroyMovie(movie);
    UpdateDeviceState();
    Return(c, 0, 1);
}
B(BinkDoFrame) {
    auto movie = FindMovie(Arg(c, 0));
    int result = movie ? RunDoFrame(*movie) : 0;
    // Prototype's state-5 caller loops on nonzero, calling NextFrame and DoFrame again before UnlockRect.
    Return(c, result > 0 ? 0 : 1, 1);
}
B(BinkNextFrame) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        Public* p = movie->Pub();
        // A deferred DoFrame returns nonzero and the guest calls NextFrame before retrying while textures stay locked.
        // Advance normally here; holding the failed frame would repeat it until the guest's 20-iteration bound.
        movie->deferred_calls = 0;
        if (p->frame_num < movie->decoded_position) movie->skipped_frames += movie->decoded_position - p->frame_num;
        if (p->frame_num >= movie->frames) {
            p->frame_num = 1;
            RewindLocked(*movie, 1);
        } else {
            ++p->frame_num;
        }
        if (movie->registered_buffers) {
            auto* b = Ptr<FrameBuffers>(movie->registered_buffers);
            if (b->total_frames >= 1 && b->total_frames <= 2) {
                uint32_t total = static_cast<uint32_t>(b->total_frames);
                uint32_t current = b->frame_num < total ? b->frame_num : 0;
                b->frame_num = (current + 1) % total;
            }
        }
    }
    Return(c, 0, 1);
}
B(BinkWait) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) SleepUntilFrame(*movie);
    Return(c, 0, 1);
}
B(BinkPause) {
    auto movie = FindMovie(Arg(c, 0));
    int old = 0;
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        old = movie->paused ? 1 : 0;
        bool pause = Arg(c, 1) != 0;
        if (pause != movie->paused) {
            if (pause) {
                movie->paused_seconds = movie->MediaSeconds(Clock::now());
                movie->paused = true;
            } else {
                double held = movie->paused_seconds;
                movie->paused = false;
                movie->start = Clock::now() - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(held));
                movie->ClearAudioClock();
            }
        }
        movie->Pub()->paused = movie->paused ? 1 : 0;
    }
    Return(c, old, 2);
}
B(BinkGetFrameBuffersInfo) {
    auto movie = FindMovie(Arg(c, 0));
    uint32_t out = Arg(c, 1);
    if (movie && out) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        auto* b = Ptr<FrameBuffers>(out);
        memset(b, 0, sizeof(*b));
        b->total_frames = 2;
        b->ya_width = movie->width;
        b->ya_height = movie->height;
        b->crcb_width = (movie->width + 1) / 2;
        b->crcb_height = (movie->height + 1) / 2;
        const bool alpha = movie->video_codec && movie->video_codec->pix_fmt == AV_PIX_FMT_YUVA420P &&
                           (movie->flags & kAlpha);
        for (auto& set : b->frames) {
            // 0x105a0804 tests Allocate before creating each D3D9 texture. Leaving these zero produces null
            // LockRect pointers and guarantees that every later registered upload is rejected.
            set.y.allocate = set.cr.allocate = set.cb.allocate = 1;
            set.alpha.allocate = alpha ? 1u : 0u;
        }
    }
    Return(c, 0, 2);
}
B(BinkRegisterFrameBuffers) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        uint32_t buffers = Arg(c, 1);
        movie->registered_buffers = buffers;
        movie->Pub()->frame_buffers = buffers;
        // Respect the guest's actual active index and initialize every valid set before its first UnlockRect.
        if (buffers) (void)UploadRegisteredLocked(*movie, nullptr, false);
    }
    Return(c, 0, 2);
}

B(BinkSetSoundTrack) {
    uint32_t total = Arg(c, 0), address = Arg(c, 1);
    std::vector<std::shared_ptr<Movie>> movies;
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        g_selected_tracks.clear();
        g_selected_tracks_set = true;
        if (address) {
            // BinkSetSoundTrack takes the complete guest array. Do not impose a small fixed track-count limit:
            // selection lists may be built by the engine in batches larger than the number of streams in one movie.
            if (uint64_t(total) * 4 > 0x100000000ull - address) {
                tl_bink_error = "BinkSetSoundTrack array crosses guest memory";
                total = 0;
            }
            if (total) g_selected_tracks.reserve(total);
            for (uint32_t i = 0; i < total; ++i) {
                uint64_t at = uint64_t(address) + uint64_t(i) * 4;
                if (at > 0xfffffffcu) { tl_bink_error = "BinkSetSoundTrack array crosses guest memory"; break; }
                g_selected_tracks.push_back(R32(static_cast<uint32_t>(at)));
            }
        }
        for (auto& m : g_movies) movies.push_back(m.second);
    }
    for (auto& movie : movies) ApplyTrackSelection(movie);
    Return(c, 0, 2);
}
B(BinkSetSoundSystem) {
    g_sound_system_open = Arg(c, 0);
    g_sound_system_param = Arg(c, 1);
    EnsureAudioOutput();
    Return(c, 1, 2);  // video remains usable with the monotonic clock when no audio device exists
}
B(BinkOpenDirectSound) { EnsureAudioOutput(); Return(c, g_audio.device ? ImportThunk("binkw32.dll", "_BinkSoundOpen@24") : 0, 1); }
B(BinkOpenMiles) { EnsureAudioOutput(); Return(c, g_audio.device ? ImportThunk("binkw32.dll", "_BinkSoundOpen@24") : 0, 1); }
B(BinkOpenWaveOut) { EnsureAudioOutput(); Return(c, g_audio.device ? ImportThunk("binkw32.dll", "_BinkSoundOpen@24") : 0, 1); }
B(BinkSoundOpen) { Return(c, g_audio.device ? 1u : 0u, 6); }

B(BinkSetVolume) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        if (Track* t = FindTrack(movie, Arg(c, 1))) t->volume = std::clamp<int>(static_cast<int>(Arg(c, 2)), 0, 32768);
    }
    Return(c, 0, 3);
}
B(BinkSetPan) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        if (Track* t = FindTrack(movie, Arg(c, 1))) t->pan = std::clamp(static_cast<int>(Arg(c, 1 + 1)), -32768, 32768);
    }
    Return(c, 0, 3);
}
B(BinkSetMixBins) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        if (Track* t = FindTrack(movie, Arg(c, 1))) {
            uint32_t address = Arg(c, 2), total = std::min(Arg(c, 3), 64u);
            t->mix_bins.clear();
            t->mix_volumes.clear();
            if (address) for (uint32_t i = 0; i < total; ++i) t->mix_bins.push_back(R32(address + i * 4));
        }
    }
    Return(c, 0, 4);
}
B(BinkSetMixBinVolumes) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        if (Track* t = FindTrack(movie, Arg(c, 1))) {
            uint32_t bins = Arg(c, 2), volumes = Arg(c, 3), total = std::min(Arg(c, 4), 64u);
            t->mix_bins.clear(); t->mix_volumes.clear();
            for (uint32_t i = 0; i < total; ++i) {
                t->mix_bins.push_back(bins ? R32(bins + i * 4) : 0);
                t->mix_volumes.push_back(volumes ? static_cast<int>(R32(volumes + i * 4)) : 32768);
            }
        }
    }
    Return(c, 0, 5);
}
B(BinkSetSoundOnOff) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) { std::lock_guard<std::timed_mutex> lock(movie->mutex); movie->sound_on = Arg(c, 1) != 0; }
    Return(c, 0, 2);
}
B(BinkSetVideoOnOff) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) { std::lock_guard<std::timed_mutex> lock(movie->mutex); movie->video_on = Arg(c, 1) != 0; }
    Return(c, 0, 2);
}

B(BinkOpenTrack) {
    uint32_t result = 0;
    std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
    auto movie_it = g_movies.find(Arg(c, 0));
    if (movie_it != g_movies.end()) {
        auto movie = movie_it->second;
        std::lock_guard<std::timed_mutex> movie_lock(movie->mutex);
        size_t index = 0;
        Track* track = TrackAtIndex(movie, Arg(c, 1), &index);
        if (track) {
            uint32_t address = HeapAlloc(sizeof(TrackPublic));
            if (address) {
                auto* out = Ptr<TrackPublic>(address);
                out->frequency = track->codec->sample_rate;
                out->bits = 16;
                out->channels = track->codec->ch_layout.nb_channels;
                out->max_size = track->max_size;
                out->bink = movie->guest;
                out->sndcomp = 0;
                out->trackindex = static_cast<int32_t>(index);
                track->legacy_open = true;
                g_open_tracks[address] = {movie, index};
                result = address;
            }
        }
    }
    Return(c, result, 2);
}
B(BinkCloseTrack) {
    uint32_t address = Arg(c, 0);
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = g_open_tracks.find(address);
    if (it != g_open_tracks.end()) {
        std::lock_guard<std::timed_mutex> movie_lock(it->second.first->mutex);
        if (it->second.second < it->second.first->tracks.size()) {
            Track& track = *it->second.first->tracks[it->second.second];
            track.legacy_open = false;
            track.legacy_frames.clear();
            track.legacy_bytes = 0;
        }
        g_open_tracks.erase(it);
        HeapFree(address);
    }
    Return(c, 0, 1);
}
B(BinkGetTrackID) {
    auto movie = FindMovie(Arg(c, 0));
    uint32_t id = 0;
    if (movie) { std::lock_guard<std::timed_mutex> lock(movie->mutex); if (Track* t = TrackAtIndex(movie, Arg(c, 1))) id = t->id; }
    Return(c, id, 2);
}
B(BinkGetTrackType) {
    auto movie = FindMovie(Arg(c, 0));
    bool valid = false;
    if (movie) { std::lock_guard<std::timed_mutex> lock(movie->mutex); valid = TrackAtIndex(movie, Arg(c, 1)) != nullptr; }
    Return(c, valid ? 0u : 0xffffffffu, 2);
}
B(BinkGetTrackMaxSize) {
    auto movie = FindMovie(Arg(c, 0));
    uint32_t size = 0;
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        if (Track* t = TrackAtIndex(movie, Arg(c, 1))) size = t->max_size;
    }
    Return(c, size, 2);
}
B(BinkGetTrackData) {
    uint32_t address = Arg(c, 0), dest = Arg(c, 1), result = 0;
    std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
    auto it = g_open_tracks.find(address);
    if (it != g_open_tracks.end() && dest) {
        auto movie = it->second.first;
        std::lock_guard<std::timed_mutex> movie_lock(movie->mutex);
        if (it->second.second < movie->tracks.size()) {
            Track& track = *movie->tracks[it->second.second];
            auto* public_track = Ptr<TrackPublic>(address);
            if (track.legacy_open && !track.legacy_frames.empty() && public_track &&
                track.legacy_frames.front().size() <= public_track->max_size) {
                const auto& pcm = track.legacy_frames.front();
                memcpy(Ptr<void>(dest), pcm.data(), pcm.size());
                result = static_cast<uint32_t>(pcm.size());
                track.legacy_bytes -= pcm.size();
                track.legacy_frames.pop_front();
            }
        }
    }
    Return(c, result, 2);
}

B(BinkCopyToBuffer) {
    auto movie = FindMovie(Arg(c, 0));
    bool ok = false;
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        if (movie->decoded_position == movie->Pub()->frame_num && movie->video_frame->data[0])
            ok = CopyToBuffer(movie->video_frame, *movie->Pub(), Arg(c, 1), static_cast<int32_t>(Arg(c, 2)),
                              Arg(c, 3), Arg(c, 4), Arg(c, 5), Arg(c, 6));
    }
    Return(c, ok ? 0 : 0xffffffffu, 7);
}
B(BinkCopyToBufferRect) {
    auto movie = FindMovie(Arg(c, 0));
    bool ok = false;
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        if (movie->decoded_position == movie->Pub()->frame_num && movie->video_frame->data[0])
            ok = CopyRectToBuffer(movie->video_frame, *movie->Pub(), Arg(c, 1), static_cast<int32_t>(Arg(c, 2)),
                    Arg(c, 3), Arg(c, 4), Arg(c, 5), Arg(c, 6), Arg(c, 7), Arg(c, 8), Arg(c, 9), Arg(c, 10));
    }
    Return(c, ok ? 0 : 0xffffffffu, 11);
}
B(BinkGetRects) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) { std::lock_guard<std::timed_mutex> lock(movie->mutex); movie->Pub()->num_rects = 1; }
    Return(c, movie ? 1u : 0u, 2);
}

B(BinkGoto) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        uint32_t frame = std::clamp(Arg(c, 1), 1u, movie->frames);
        if (RewindLocked(*movie, frame)) movie->Pub()->frame_num = frame;
    }
    Return(c, 0, 3);
}
B(BinkGetKeyFrame) {
    auto movie = FindMovie(Arg(c, 0));
    Return(c, movie ? std::clamp(Arg(c, 1), 1u, movie->frames) : 0, 3);
}
B(BinkShouldSkip) {
    auto movie = FindMovie(Arg(c, 0));
    bool skip = false;
    if (movie && !g_simulate) {
        std::unique_lock<std::timed_mutex> lock(movie->mutex, std::defer_lock);
        if (lock.try_lock_for(kLockBudget))
            skip = !movie->paused && movie->MediaSeconds(Clock::now()) > movie->Pub()->frame_num * movie->FrameSeconds();
    }
    Return(c, skip ? 1u : 0u, 1);
}

B(BinkGetRealtime) {
    auto movie = FindMovie(Arg(c, 0));
    uint32_t out = Arg(c, 1);
    if (movie && out) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        auto* r = Ptr<Realtime>(out); memset(r, 0, sizeof(*r));
        r->frame_num = movie->Pub()->frame_num;
        r->frame_rate = movie->Pub()->frame_rate; r->frame_rate_div = movie->Pub()->frame_rate_div;
        r->frames = movie->Pub()->frame_num;
        r->frames_time = static_cast<uint32_t>(movie->MediaSeconds(Clock::now()) * 1000.0);
        r->frames_video_decomp_time = static_cast<uint32_t>(movie->decode_us / 1000);
        r->frames_audio_decomp_time = static_cast<uint32_t>(movie->audio_decode_us / 1000);
        r->frames_data_rate = static_cast<uint32_t>(movie->largest_packet * movie->Pub()->frame_rate /
            std::max(1u, movie->Pub()->frame_rate_div));
    }
    Return(c, 0, 3);
}
B(BinkGetSummary) {
    auto movie = FindMovie(Arg(c, 0));
    uint32_t out = Arg(c, 1);
    if (movie && out) {
        std::lock_guard<std::timed_mutex> lock(movie->mutex);
        auto* s = Ptr<Summary>(out); memset(s, 0, sizeof(*s));
        s->width = movie->width; s->height = movie->height;
        s->total_time = static_cast<uint32_t>(movie->MediaSeconds(Clock::now()) * 1000.0);
        s->file_frame_rate = movie->Pub()->frame_rate; s->file_frame_rate_div = movie->Pub()->frame_rate_div;
        s->frame_rate = s->file_frame_rate; s->frame_rate_div = s->file_frame_rate_div;
        s->total_frames = movie->frames; s->total_played_frames = movie->total_decodes;
        s->skipped_frames = movie->skipped_frames; s->total_video_decomp_time = movie->decode_us;
        s->total_audio_decomp_time = movie->audio_decode_us;
        s->average_frame_size = movie->total_decodes ? movie->largest_packet : 0;
    }
    Return(c, 0, 2);
}

B(BinkControlBackgroundIO) {
    // Suspension applies to service/prefill I/O, not to a foreground DoFrame that the guest explicitly requested.
    auto movie = FindMovie(Arg(c, 0));
    uint32_t control = Arg(c, 1);
    if (movie) {
        { std::lock_guard<std::timed_mutex> lock(movie->mutex); movie->background_io = control != 1; }
        movie->source->ControlBackgroundIO(control);
    }
    Return(c, 0, 2);
}
B(BinkControlPlatformFeatures) { Return(c, 0, 2); }
B(BinkService) {
    auto movie = FindMovie(Arg(c, 0));
    if (movie) {
        bool service = false;
        {
            std::unique_lock<std::timed_mutex> lock(movie->mutex, std::defer_lock);
            if (lock.try_lock_for(kLockBudget)) {
                EnsureAudioLeadLocked(*movie, Clock::now() + kAudioBudget);
                service = movie->background_io;
            }
        }
        if (service) movie->source->Service();
    }
    Return(c, 0, 1);
}
B(BinkSetFrameRate) { g_force_rate = Arg(c, 0); g_force_rate_div = std::max(1u, Arg(c, 1)); Return(c, 0, 2); }
B(BinkSetSimulate) { g_simulate = Arg(c, 0) != 0; Return(c, 0, 1); }
B(BinkSetIOSize) { g_io_size = std::clamp(Arg(c, 0), 4096u, 1024u * 1024u); Return(c, 0, 1); }
B(BinkSetMemory) { Return(c, 0, 2); }
B(BinkSetIO) { g_custom_io_open = Arg(c, 0); Return(c, 0, 1); }
B(BinkSetError) { tl_bink_error = Arg(c, 0) ? Str(Arg(c, 0)) : "no error"; Return(c, 0, 1); }
B(BinkGetError) { Return(c, ErrorAddress(), 0); }
B(BinkGetPalette) {
    if (uint32_t out = Arg(c, 0)) for (uint32_t i = 0; i < 256; ++i) W32(out + i * 4, PaletteColor(i));
    Return(c, 0, 1);
}

B(BinkDoFrameAsync) {
    // Keep the async ABI on the same bounded path. Plane publication remains on the calling guest thread, and the
    // wait export therefore never has to wait on a native worker touching the guest window.
    auto movie = FindMovie(Arg(c, 0));
    int result = movie ? RunDoFrame(*movie) : 0;
    Return(c, result > 0 ? 0 : 1, 3);
}
B(BinkDoFrameAsyncWait) { Return(c, 0, 2); }
B(BinkStartAsyncThread) { Return(c, 0, 2); }
B(BinkRequestStopAsyncThread) { Return(c, 1, 1); }
B(BinkWaitStopAsyncThread) { Return(c, 1, 1); }

// The legacy windowed buffer/cursor API is not used by Prototype. Fail cleanly rather than dereferencing a DirectDraw
// or HWND pointer in the Android process; all movie, plane, audio and CPU-copy entry points above remain functional.
B(BinkBufferBlit) { Return(c, 0, 3); }
B(BinkBufferCheckWinPos) { Return(c, 0, 3); }
B(BinkBufferClear) { Return(c, 0, 2); }
B(BinkBufferClose) { Return(c, 0, 1); }
B(BinkBufferGetDescription) { Return(c, 0, 1); }
B(BinkBufferGetError) { Return(c, ErrorAddress(), 0); }
B(BinkBufferLock) { Return(c, 0, 1); }
B(BinkBufferOpen) { tl_bink_error = "windowed Bink buffers are unavailable on Android"; Return(c, 0, 4); }
B(BinkBufferSetDirectDraw) { Return(c, 0, 2); }
B(BinkBufferSetHWND) { Return(c, 0, 2); }
B(BinkBufferSetOffset) { Return(c, 0, 3); }
B(BinkBufferSetResolution) { Return(c, 0, 3); }
B(BinkBufferSetScale) { Return(c, 0, 3); }
B(BinkBufferUnlock) { Return(c, 0, 1); }
B(BinkCheckCursor) { Return(c, 0, 5); }
B(BinkDDSurfaceType) { Return(c, kSurface32, 1); }
B(BinkDX8SurfaceType) { Return(c, kSurface32, 1); }
B(BinkDX9SurfaceType) { Return(c, kSurface32, 1); }
B(BinkIsSoftwareCursor) { Return(c, 0, 2); }
B(BinkLogoAddress) { Return(c, 0, 0); }
B(BinkRestoreCursor) { Return(c, 0, 1); }
B(RADTimerRead) {
    static const auto start = Clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    Return(c, static_cast<uint32_t>(ms), 0);
}

#undef B

#define E(name, args) {"binkw32.dll", "_" #name "@" #args, b_##name}
const ImportDef kBink[] = {
    E(BinkBufferBlit,12), E(BinkBufferCheckWinPos,12), E(BinkBufferClear,8), E(BinkBufferClose,4),
    E(BinkBufferGetDescription,4), E(BinkBufferGetError,0), E(BinkBufferLock,4), E(BinkBufferOpen,16),
    E(BinkBufferSetDirectDraw,8), E(BinkBufferSetHWND,8), E(BinkBufferSetOffset,12), E(BinkBufferSetResolution,12),
    E(BinkBufferSetScale,12), E(BinkBufferUnlock,4), E(BinkCheckCursor,20), E(BinkClose,4), E(BinkCloseTrack,4),
    E(BinkControlBackgroundIO,8), E(BinkControlPlatformFeatures,8), E(BinkCopyToBuffer,28), E(BinkCopyToBufferRect,44),
    E(BinkDDSurfaceType,4), E(BinkDX8SurfaceType,4), E(BinkDX9SurfaceType,4), E(BinkDoFrame,4),
    E(BinkDoFrameAsync,12), E(BinkDoFrameAsyncWait,8), E(BinkGetError,0), E(BinkGetFrameBuffersInfo,8),
    E(BinkGetKeyFrame,12), E(BinkGetPalette,4), E(BinkGetRealtime,12), E(BinkGetRects,8), E(BinkGetSummary,8),
    E(BinkGetTrackData,8), E(BinkGetTrackID,8), E(BinkGetTrackMaxSize,8), E(BinkGetTrackType,8), E(BinkGoto,12),
    E(BinkIsSoftwareCursor,8), E(BinkLogoAddress,0), E(BinkNextFrame,4), E(BinkOpen,8), E(BinkOpenDirectSound,4),
    E(BinkOpenMiles,4), E(BinkOpenTrack,8), E(BinkOpenWaveOut,4), E(BinkPause,8), E(BinkRegisterFrameBuffers,8),
    E(BinkRequestStopAsyncThread,4), E(BinkRestoreCursor,4), E(BinkService,4), E(BinkSetError,4), E(BinkSetFrameRate,8),
    E(BinkSetIO,4), E(BinkSetIOSize,4), E(BinkSetMemory,8), E(BinkSetMixBinVolumes,20), E(BinkSetMixBins,16),
    E(BinkSetPan,12), E(BinkSetSimulate,4), E(BinkSetSoundOnOff,8), E(BinkSetSoundSystem,8), E(BinkSetSoundTrack,8),
    E(BinkSetVideoOnOff,8), E(BinkSetVolume,12), E(BinkShouldSkip,4), E(BinkStartAsyncThread,8), E(BinkWait,4),
    E(BinkWaitStopAsyncThread,4), E(RADTimerRead,0), E(BinkSoundOpen,24),
};
#undef E
ImportRegistrar g_bink(kBink);

}  // namespace
}  // namespace native
