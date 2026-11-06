// Exact-production XAudio2 bridge lifecycle regression. The fake backend models FAudio's one-master rule and
// DestroyVoiceSafeEXT dependent-send refusal; this is host correctness evidence, not audio-device timing proof.
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <sys/mman.h>

#ifndef FAKE_MASTER_REUSE
#define FAKE_MASTER_REUSE 1
#endif
#ifndef FAKE_LONG_QUANTUM
#define FAKE_LONG_QUANTUM 0
#endif
#ifndef XAUDIO2_SOURCE
#define XAUDIO2_SOURCE "../xaudio2.cpp"
#endif
#include XAUDIO2_SOURCE

uint8_t* RecompMemoryBase = nullptr;

struct FakeVoice;
struct FAudio {
    bool released = false;
    FAudioVoice* master = nullptr;
};
struct FAudioVoice {
    FakeVoice* state = nullptr;
};
struct FakeVoice {
    FAudioVoice* voice = nullptr;
    FAudio* audio = nullptr;
    unsigned kind = 0;  // 0 source, 1 submix, 2 mastering
    bool alive = true;
    uint32_t channels = 0, rate = 0;
    float volume = 1.0f, frequency = 1.0f;
    FAudioVoiceCallback* callback = nullptr;
    void* current_context = nullptr;
    uint32_t queued = 0;
    std::vector<FAudioVoice*> sends;
};

namespace {
constexpr uint32_t kStack = 0x8000;
constexpr uint32_t kGuestSize = 64u * 1024u * 1024u;
uint32_t guest_next = 0x10000;
uint32_t thunk_next = 0x50000000;
unsigned checks = 0;

struct FakeState {
    unsigned engine_construct = 0, engine_release = 0;
    unsigned master_create = 0, master_destroy = 0;
    unsigned source_create = 0, source_destroy = 0, submix_create = 0, submix_destroy = 0;
    unsigned invalid_calls = 0, initialize = 0;
    uint32_t initialize_flags = 0;
    std::vector<FakeVoice*> all;
    FAudioVoice* last_source = nullptr;
    FAudioVoice* last_master = nullptr;
    unsigned destroy_refusals = 0, master_overwrites = 0;
};
FakeState fake;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "FAIL check %u: %s\n", checks + 1, message);
    std::exit(1);
}
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) fail(message);
}
uint32_t bits(float value) {
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
struct CallResult {
    uint32_t eax, esp, ret;
};
CallResult call(void (*fn)(Ctx*), std::initializer_list<uint32_t> args) {
    Ctx c{};
    c.esp = kStack;
    W32(kStack, 0xdeadbeef);
    size_t i = 0;
    for (uint32_t arg : args) W32(kStack + 4 + 4 * uint32_t(i++), arg);
    fn(&c);
    check(c.esp == kStack + 4 * (uint32_t(args.size()) + 1), "stdcall stack depth");
    check(c.ret_target == 0xdeadbeef, "guest return target");
    return {c.eax, c.esp, c.ret_target};
}
FakeVoice* state_of(FAudioVoice* voice) {
    if (!voice) return nullptr;
    for (FakeVoice* state : fake.all) {
        if (state->voice == voice) {
            if (!state->alive) ++fake.invalid_calls;
            return state;
        }
    }
    ++fake.invalid_calls;
    return nullptr;
}
unsigned live_fake_voices() {
    return unsigned(std::count_if(fake.all.begin(), fake.all.end(), [](const FakeVoice* state) { return state->alive; }));
}
unsigned live_guest_objects() {
    return unsigned(std::count_if(native::g_objects.begin(), native::g_objects.end(), [](const native::Object* object) {
        return object != nullptr;
    }));
}
uint32_t create_master(uint32_t engine, uint32_t channels, uint32_t rate, uint32_t* hr = nullptr) {
    uint32_t out = native::HeapAlloc(4);
    W32(out, 0xa5a5a5a5);
    CallResult result = call(native::CreateMasteringVoice, {engine, out, channels, rate, 0, 0, 0});
    if (hr) *hr = result.eax;
    return R32(out);
}
uint32_t create_source(uint32_t engine, uint32_t format, uint32_t* hr = nullptr) {
    uint32_t out = native::HeapAlloc(4);
    W32(out, 0xa5a5a5a5);
    CallResult result = call(native::CreateSourceVoice, {engine, out, format, 0, bits(1.0f), 0, 0, 0});
    if (hr) *hr = result.eax;
    return R32(out);
}
void destroy_voice(uint32_t voice) {
    check(call(native::DestroyVoice, {voice}).eax == 0, "destroy voice result");
}
void check_tombstone(uint32_t guest) {
    uint32_t index = R32(guest + 4);
    check(R32(guest) == 0, "destroyed guest vtable is null");
    check(index < native::g_objects.size() && native::g_objects[index] == nullptr,
          "destroyed guest slot is a null tombstone");
}
void set_fake_sends(FakeVoice* state, const FAudioVoiceSends* sends) {
    state->sends.clear();
    if (sends) {
        for (uint32_t i = 0; i < sends->SendCount; ++i) state->sends.push_back(sends->pSends[i].pOutputVoice);
    } else if (state->audio->master) {
        state->sends.push_back(state->audio->master);
    }
}
bool has_sender(FAudioVoice* target) {
    for (FakeVoice* state : fake.all) {
        if (!state->alive || state->voice == target) continue;
        if (std::find(state->sends.begin(), state->sends.end(), target) != state->sends.end()) return true;
    }
    return false;
}
void destroy_fake_voice(FAudioVoice* voice) {
    FakeVoice* state = state_of(voice);
    if (!state || !state->alive) return;
    if (state->audio->master == voice) state->audio->master = nullptr;
    state->alive = false;
    state->callback = nullptr;
    state->voice = nullptr;
    if (state->kind == 2) ++fake.master_destroy;
    else if (state->kind == 0) ++fake.source_destroy;
    else ++fake.submix_destroy;
    delete voice;
}
}  // namespace

namespace native {
uint32_t HeapAlloc(uint32_t size) {
    uint32_t result = (guest_next + 15u) & ~15u;
    guest_next = result + std::max(1u, size);
    if (guest_next >= kGuestSize) std::abort();
    return result;
}
void HeapFree(uint32_t) {}
uint32_t HeapSize(uint32_t) { return 0; }
uint32_t HeapRealloc(uint32_t, uint32_t) { return 0; }
void Log(const char*, ...) {}
int SettingInt(const char* key, int def) {
    if (std::strcmp(key, "audio_master_reuse") == 0) return FAKE_MASTER_REUSE;
    if (std::strcmp(key, "audio_stats") == 0) return 0;
    if (std::strcmp(key, "audio_quantum_1024") == 0) return FAKE_LONG_QUANTUM;
    return def;
}
GuestThread* AttachThread(uint32_t) { return nullptr; }
void PerfHintAddThread(int) {}
uint32_t GuestCall(uint32_t, std::initializer_list<uint32_t>, uint32_t, uint32_t* edx_out) {
    if (edx_out) *edx_out = 0;
    return 0;
}
void RegisterImports(const ImportDef*, size_t) {}
uint32_t ImportThunk(const char*, const char*) { return thunk_next += 4; }
}  // namespace native

extern "C" {
uint32_t FAudioCOMConstructEXT(FAudio** out, uint8_t version) {
    check(out && version == 3, "FAudio COM construct ABI");
    ++fake.engine_construct;
    *out = new FAudio{};
    return 0;
}
uint32_t FAudio_Release(FAudio* audio) {
    check(audio && !audio->released, "FAudio release once");
    ++fake.engine_release;
    for (FakeVoice* state : fake.all) {
        if (state->alive && state->audio == audio) {
            state->alive = false;
            state->callback = nullptr;
            delete state->voice;
            state->voice = nullptr;
        }
    }
    audio->master = nullptr;
    audio->released = true;
    delete audio;
    return 0;
}
uint32_t FAudio_GetDeviceCount(FAudio*, uint32_t* count) {
    *count = 1;
    return 0;
}
uint32_t FAudio_GetDeviceDetails(FAudio*, uint32_t, FAudioDeviceDetails* details) {
    std::memset(details, 0, sizeof(*details));
    return 0;
}
uint32_t FAudio_Initialize(FAudio*, uint32_t flags, FAudioProcessor) {
    ++fake.initialize;
    fake.initialize_flags = flags;
    return 0;
}
uint32_t FAudio_RegisterForCallbacks(FAudio*, FAudioEngineCallback*) { return 0; }
void FAudio_UnregisterForCallbacks(FAudio*, FAudioEngineCallback*) {}
uint32_t FAudio_CreateMasteringVoice(FAudio* audio, FAudioMasteringVoice** out, uint32_t channels,
                                     uint32_t rate, uint32_t, uint32_t, const FAudioEffectChain*) {
    if (audio->master) ++fake.master_overwrites;
    auto* voice = new FAudioVoice{};
    auto* state = new FakeVoice{};
    state->voice = voice;
    state->audio = audio;
    state->kind = 2;
    state->channels = channels;
    state->rate = rate;
    voice->state = state;
    fake.all.push_back(state);
    fake.last_master = voice;
    audio->master = voice;
    ++fake.master_create;
    *out = voice;
    return 0;
}
uint32_t FAudio_CreateSourceVoice(FAudio* audio, FAudioSourceVoice** out, const FAudioWaveFormatEx* format,
                                  uint32_t, float, FAudioVoiceCallback* callback, const FAudioVoiceSends* sends,
                                  const FAudioEffectChain*) {
    auto* voice = new FAudioVoice{};
    auto* state = new FakeVoice{};
    state->voice = voice;
    state->audio = audio;
    state->kind = 0;
    state->channels = format->nChannels;
    state->rate = format->nSamplesPerSec;
    state->callback = callback;
    set_fake_sends(state, sends);
    voice->state = state;
    fake.all.push_back(state);
    fake.last_source = voice;
    ++fake.source_create;
    *out = voice;
    return 0;
}
uint32_t FAudio_CreateSubmixVoice(FAudio* audio, FAudioSubmixVoice** out, uint32_t channels, uint32_t rate,
                                  uint32_t, uint32_t, const FAudioVoiceSends* sends, const FAudioEffectChain*) {
    auto* voice = new FAudioVoice{};
    auto* state = new FakeVoice{};
    state->voice = voice;
    state->audio = audio;
    state->kind = 1;
    state->channels = channels;
    state->rate = rate;
    set_fake_sends(state, sends);
    voice->state = state;
    fake.all.push_back(state);
    ++fake.submix_create;
    *out = voice;
    return 0;
}
uint32_t FAudio_StartEngine(FAudio*) { return 0; }
void FAudio_StopEngine(FAudio*) {}
uint32_t FAudio_CommitOperationSet(FAudio*, uint32_t) { return 0; }
void FAudioVoice_GetVoiceDetails(FAudioVoice* voice, FAudioVoiceDetails* details) {
    FakeVoice* state = state_of(voice);
    if (!state) return;
    details->CreationFlags = 0;
    details->ActiveFlags = 0;
    details->InputChannels = state->channels;
    details->InputSampleRate = state->rate;
}
uint32_t FAudioVoice_SetOutputVoices(FAudioVoice* voice, const FAudioVoiceSends* sends) {
    FakeVoice* state = state_of(voice);
    if (!state || !state->alive || state->kind == 2) return FAUDIO_E_INVALID_CALL;
    set_fake_sends(state, sends);
    return 0;
}
uint32_t FAudioVoice_SetFilterParameters(FAudioVoice*, const FAudioFilterParameters*, uint32_t) { return 0; }
void FAudioVoice_GetFilterParameters(FAudioVoice*, FAudioFilterParameters* parameters) {
    std::memset(parameters, 0, sizeof(*parameters));
}
uint32_t FAudioVoice_SetVolume(FAudioVoice* voice, float volume, uint32_t) {
    FakeVoice* state = state_of(voice);
    if (!state || !state->alive) return FAUDIO_E_INVALID_CALL;
    state->volume = volume;
    return 0;
}
void FAudioVoice_GetVolume(FAudioVoice* voice, float* volume) {
    FakeVoice* state = state_of(voice);
    *volume = state ? state->volume : 0.0f;
}
uint32_t FAudioVoice_SetChannelVolumes(FAudioVoice*, uint32_t, const float*, uint32_t) { return 0; }
void FAudioVoice_GetChannelVolumes(FAudioVoice*, uint32_t channels, float* volumes) {
    for (uint32_t i = 0; i < channels; ++i) volumes[i] = 1.0f;
}
uint32_t FAudioVoice_SetOutputMatrix(FAudioVoice*, FAudioVoice*, uint32_t, uint32_t, const float*, uint32_t) {
    return 0;
}
void FAudioVoice_GetOutputMatrix(FAudioVoice*, FAudioVoice*, uint32_t source, uint32_t destination, float* matrix) {
    std::memset(matrix, 0, sizeof(float) * source * destination);
}
uint32_t FAudioVoice_DestroyVoiceSafeEXT(FAudioVoice* voice) {
    FakeVoice* state = state_of(voice);
    if (!state || !state->alive) return FAUDIO_E_INVALID_CALL;
    if (has_sender(voice)) {
        ++fake.destroy_refusals;
        return 0x80004005u;
    }
    destroy_fake_voice(voice);
    return 0;
}
void FAudioVoice_DestroyVoice(FAudioVoice* voice) {
    (void)FAudioVoice_DestroyVoiceSafeEXT(voice);
}
uint32_t FAudioSourceVoice_Start(FAudioSourceVoice*, uint32_t, uint32_t) { return 0; }
uint32_t FAudioSourceVoice_Stop(FAudioSourceVoice*, uint32_t, uint32_t) { return 0; }
uint32_t FAudioSourceVoice_SubmitSourceBuffer(FAudioSourceVoice* voice, const FAudioBuffer* buffer,
                                              const FAudioBufferWMA*) {
    FakeVoice* state = state_of(voice);
    if (!state || !state->alive) return FAUDIO_E_INVALID_CALL;
    state->current_context = buffer->pContext;
    ++state->queued;
    return 0;
}
uint32_t FAudioSourceVoice_FlushSourceBuffers(FAudioSourceVoice* voice) {
    FakeVoice* state = state_of(voice);
    if (state) state->queued = 0;
    return 0;
}
uint32_t FAudioSourceVoice_Discontinuity(FAudioSourceVoice*) { return 0; }
uint32_t FAudioSourceVoice_ExitLoop(FAudioSourceVoice*, uint32_t) { return 0; }
void FAudioSourceVoice_GetState(FAudioSourceVoice* voice, FAudioVoiceState* state_out, uint32_t) {
    FakeVoice* state = state_of(voice);
    state_out->pCurrentBufferContext = state ? state->current_context : nullptr;
    state_out->BuffersQueued = state ? state->queued : 0;
    state_out->SamplesPlayed = 0;
}
uint32_t FAudioSourceVoice_SetFrequencyRatio(FAudioSourceVoice* voice, float ratio, uint32_t) {
    FakeVoice* state = state_of(voice);
    if (state) state->frequency = ratio;
    return 0;
}
void FAudioSourceVoice_GetFrequencyRatio(FAudioSourceVoice* voice, float* ratio) {
    FakeVoice* state = state_of(voice);
    *ratio = state ? state->frequency : 0.0f;
}
}  // extern "C"

int main() {
    void* mapping = mmap(reinterpret_cast<void*>(uintptr_t(RECOMP_GUEST_BASE)), kGuestSize,
                         PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (mapping == MAP_FAILED) {
        std::perror("mmap fixed guest window");
        return 2;
    }
    RecompMemoryBase = static_cast<uint8_t*>(mapping);
    std::memset(mapping, 0, kGuestSize);

    const uint32_t clsid = native::HeapAlloc(16), engine_out = native::HeapAlloc(4);
    const uint8_t id[16] = {0x7a, 0x63, 0x5e, 0x4c, 0xc7, 0x16, 0xe3, 0x4d,
                            0x9c, 0x46, 0x5e, 0xd2, 0x21, 0x81, 0x96, 0x2d};
    std::memcpy(native::Ptr<void>(clsid), id, sizeof(id));
    uint32_t hr = 0;
    check(native::CreateXAudio2(clsid, engine_out, &hr) && hr == 0, "engine construction");
    const uint32_t engine = R32(engine_out);
    check(call(native::Initialize, {engine, 0, 0}).eax == 0, "engine initialize");
    check(fake.initialize_flags == (FAKE_LONG_QUANTUM ? FAUDIO_1024_QUANTUM : 0), "quantum flag preserved");

    const uint32_t format = native::HeapAlloc(sizeof(FAudioWaveFormatEx));
    auto* wave = native::Ptr<FAudioWaveFormatEx>(format);
    wave->wFormatTag = FAUDIO_FORMAT_PCM;
    wave->nChannels = 2;
    wave->nSamplesPerSec = 48000;
    wave->nAvgBytesPerSec = 192000;
    wave->nBlockAlign = 4;
    wave->wBitsPerSample = 16;
    wave->cbSize = 0;

    uint32_t master = create_master(engine, 2, 48000, &hr);
    check(hr == 0 && master != 0, "initial master");
    FAudioVoice* old_real_master = fake.last_master;
    uint32_t source = create_source(engine, format, &hr);
    check(hr == 0 && source != 0, "source with default master send");
    FakeVoice* source_state = state_of(fake.last_source);
    check(source_state && source_state->sends.size() == 1 && source_state->sends[0] == old_real_master,
          "default send targets real master");
    destroy_voice(master);
    check_tombstone(master);
    check(state_of(old_real_master) && state_of(old_real_master)->alive,
          "logical master destroy leaves real output while source remains");

    uint32_t changed = create_master(engine, 2, 44100, &hr);
    check(changed == 0 && hr == 0x80004005u, "dependent-send replacement returns refusal");
    const unsigned expected_refusals = FAKE_MASTER_REUSE ? 1u : 2u;
    check(fake.destroy_refusals == expected_refusals && fake.master_overwrites == 0,
          "failed destroy never overwrites the live master");
    check(live_fake_voices() == 2 && state_of(old_real_master)->alive,
          "old master and source survive refused replacement");

    const uint32_t no_sends = native::HeapAlloc(8);
    W32(no_sends, 0);
    W32(no_sends + 4, 0);
    check(call(native::SetOutputVoices, {source, no_sends}).eax == 0, "detach dependent source sends");
    changed = create_master(engine, 2, 44100, &hr);
    check(changed != 0 && hr == 0, "replacement succeeds after sender detach");
    check(fake.master_overwrites == 0 && fake.master_destroy == 1,
          "old master destroyed exactly once before replacement");
    check(source_state->sends.empty(), "source remains alive with detached sends");
    check(fake.last_master != old_real_master, "new real master has a distinct identity");
    destroy_voice(source);
    destroy_voice(changed);
    check(fake.source_destroy == 1, "source destruction balanced");
    check(fake.master_destroy == (FAKE_MASTER_REUSE ? 1u : 2u), "master destruction balanced for reuse mode");

    check(call(native::EngineRelease, {engine}).eax == 0, "engine release");
    check(live_fake_voices() == 0 && live_guest_objects() == 0, "engine cleanup balanced");
    check(fake.engine_release == 1 && fake.invalid_calls == 0, "no invalid backend calls");
    std::printf("PASS xaudio2 checks=%u reuse=%d long_quantum=%d destroy_refusals=%u master_overwrites=%u "
                "master_create=%u master_destroy=%u source_destroy=%u engine_release=%u\n",
                checks, FAKE_MASTER_REUSE, FAKE_LONG_QUANTUM, fake.destroy_refusals, fake.master_overwrites,
                fake.master_create, fake.master_destroy, fake.source_destroy, fake.engine_release);
    for (FakeVoice* state : fake.all) delete state;
    return 0;
}
