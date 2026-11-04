// Standalone production-callback fixture for balanced host/native-CI measurements. This is not an Android audio
// device, underrun, A/V-sync, or FPS test. BINK_PROFILE_SOURCE permits an exact candidate source without modifying
// the baseline checkout; every callback is checked against an independent wrapped scalar reference.
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

#define main bink_test_suite_main
#include "bink_test.cpp"
#undef main
#ifndef BINK_PROFILE_SOURCE
#define BINK_PROFILE_SOURCE "bink.cpp"
#endif
#include BINK_PROFILE_SOURCE

namespace {
uint64_t Fnva(const void* bytes, size_t size, uint64_t hash = 14695981039346656037ull) {
    const auto* data = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string Hex64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

struct Fixture {
    std::shared_ptr<native::Movie> movie = std::make_shared<native::Movie>();
    native::Track* track = nullptr;
    std::vector<float> output;
    std::vector<float> expected;
    size_t initial_head = 0;
    size_t initial_frames = 0;

    Fixture() {
        auto owned = std::make_unique<native::Track>();
        track = owned.get();
        movie->tracks.push_back(std::move(owned));
        movie->sound_on = true;
        native::g_movies[1] = movie;
    }
    ~Fixture() { native::g_movies.clear(); }

    void Configure(size_t rate, size_t callback_frames, bool wrap, bool bins,
                   int volume = 30000, int pan = -12000) {
        track->enabled = true;
        track->volume = volume;
        track->pan = pan;
        track->mix_bins = bins ? std::vector<uint32_t>{0} : std::vector<uint32_t>{};
        track->mix_volumes = bins ? std::vector<int>{16384} : std::vector<int>{};
        track->ring.Reset(static_cast<size_t>(rate));
        const size_t capacity = track->ring.capacity;
        initial_frames = std::max(callback_frames * 2, callback_frames);
        Check(initial_frames <= capacity, "fixture ring is too small");
        initial_head = wrap ? (capacity >= callback_frames / 2 ? capacity - callback_frames / 2 : 0)
                            : std::min<size_t>(128, capacity - initial_frames);
        for (size_t i = 0; i < initial_frames; ++i) {
            const size_t index = (initial_head + i) % capacity;
            const float left = static_cast<float>(static_cast<int>((i * 37 + 11) % 20001) - 10000) / 10000.0f;
            const float right = static_cast<float>(static_cast<int>((i * 53 + 17) % 20001) - 10000) / 10000.0f;
            track->ring.data[index * 2] = left;
            track->ring.data[index * 2 + 1] = right;
        }
        track->ring.head = initial_head;
        track->ring.frames = initial_frames;
        output.assign(callback_frames * 2, 0.0f);
        expected.assign(callback_frames * 2, 0.0f);
        const float base_gain = track->volume / 32768.0f;
        const float normalized_pan = std::clamp(track->pan / 32768.0f, -1.0f, 1.0f);
        const float gain_l = base_gain * (normalized_pan > 0 ? 1.0f - normalized_pan : 1.0f);
        const float gain_r = base_gain * (normalized_pan < 0 ? 1.0f + normalized_pan : 1.0f);
        for (size_t i = 0; i < callback_frames; ++i) {
            const size_t index = ((initial_head + i) % capacity) * 2;
            float left = track->ring.data[index] * gain_l;
            float right = track->ring.data[index + 1] * gain_r;
            if (bins) {
                left = left * (16384 / 32768.0f);
                right = right * (16384 / 32768.0f);
            }
            expected[i * 2] = std::clamp(left, -1.0f, 1.0f);
            expected[i * 2 + 1] = std::clamp(right, -1.0f, 1.0f);
        }
    }

    double Run(size_t callback_frames) {
        track->ring.head = initial_head;
        track->ring.frames = initial_frames;
        std::fill(output.begin(), output.end(), 0.0f);
        const auto start = std::chrono::steady_clock::now();
        native::AudioCallback(nullptr, reinterpret_cast<Uint8*>(output.data()),
                              static_cast<int>(output.size() * sizeof(float)));
        const double elapsed = std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - start).count();
        Check(memcmp(output.data(), expected.data(), output.size() * sizeof(float)) == 0,
              "audio callback output bits differ from independent wrapped reference");
        Check(track->ring.head == (initial_head + callback_frames) % track->ring.capacity &&
                  track->ring.frames == initial_frames - callback_frames,
              "audio callback consumed the wrong wrapped frame range");
        return elapsed;
    }
};
}

int main(int argc, char** argv) {
    try {
        Check(argc == 5, "usage: bink_audio_profile <iterations> <rate> <wrap|nowrap> <direct|bin>");
        const size_t iterations = std::stoul(argv[1]);
        const size_t rate = std::stoul(argv[2]);
        const std::string wrap_mode = argv[3], mix_mode = argv[4];
        Check(iterations >= 100 && (rate == 32000 || rate == 44100 || rate == 48000), "invalid iteration/rate");
        Check(wrap_mode == "wrap" || wrap_mode == "nowrap", "invalid wrap mode");
        Check(mix_mode == "direct" || mix_mode == "bin", "invalid mix mode");
        constexpr size_t kFrames = 1024;
        Fixture fixture;
        fixture.Configure(rate, kFrames, wrap_mode == "wrap", mix_mode == "bin");
        for (unsigned i = 0; i < 32; ++i) (void)fixture.Run(kFrames);
        std::vector<double> timings;
        timings.reserve(iterations);
        uint64_t digest = Fnva(fixture.expected.data(), fixture.expected.size() * sizeof(float));
        for (size_t i = 0; i < iterations; ++i) timings.push_back(fixture.Run(kFrames));
        const double total_ns = std::accumulate(timings.begin(), timings.end(), 0.0);
        std::cout << std::setprecision(17)
                  << "{\"schema\":1,\"iterations\":" << iterations << ",\"rate\":" << rate
                  << ",\"callback_frames\":" << kFrames << ",\"wrap\":\"" << wrap_mode
                  << "\",\"mix\":\"" << mix_mode << "\",\"total_ns\":" << total_ns
                  << ",\"mean_ns\":" << total_ns / iterations
                  << ",\"output_fnv1a64\":\"" << Hex64(digest) << "\",\"correctness_calls\":"
                  << iterations + 32 << ",\"timings_ns\":[";
        for (size_t i = 0; i < timings.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << timings[i];
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Bink audio profile failure: " << e.what() << "\n";
        return 1;
    }
}
