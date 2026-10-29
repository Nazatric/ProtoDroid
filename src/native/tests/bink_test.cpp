// Standalone production-code Bink tests against a minimal host FFmpeg; the sample stays outside the repository.
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "bink_abi.h"
#include "bink_blit.h"
#include "bink_pacing.h"
#include "guest.h"
#include "sdl_stub.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavcodec/avcodec.h>
}

uint8_t* RecompMemoryBase = nullptr;
volatile uint32_t RecompRestrictLo = 0, RecompRestrictHi = 0, RecompTestDetours = 0;

namespace native {
const char* g_game_dir = nullptr;
const char* g_data_dir = nullptr;
uint32_t g_engine_lo = 0, g_engine_hi = 0;

namespace {
std::map<std::string, ImportDef>& Imports() { static std::map<std::string, ImportDef> m; return m; }
std::unordered_map<uint32_t, int> g_test_handles;
using TestGuestFunction = std::function<uint32_t(const std::vector<uint32_t>&)>;
std::unordered_map<uint32_t, TestGuestFunction> g_test_guest_functions;
uint32_t g_next_heap = 0x100000;
constexpr uint32_t kHeapBytes = 512u << 20;

void CheckHost(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}
std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}

void Log(const char* fmt, ...) {
    if (!getenv("PROTODROID_BINK_TEST_VERBOSE")) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
[[noreturn]] void Fatal(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); abort();
}
[[noreturn]] void StartupFailure(const char* what) { throw std::runtime_error(what); }
uint32_t HeapAlloc(uint32_t size) {
    uint32_t at = (g_next_heap + 15) & ~15u;
    if (!size || at < 0x100000 || uint64_t(at) + size > 0x100000ull + kHeapBytes) return 0;
    g_next_heap = at + size;
    memset(Ptr<void>(at), 0, size);
    return at;
}
void HeapFree(uint32_t) {}
uint32_t HeapSize(uint32_t) { return 0; }
uint32_t HeapRealloc(uint32_t old, uint32_t size) { (void)old; return HeapAlloc(size); }
uint32_t PageAlloc(uint32_t size, uint32_t at) { (void)at; return HeapAlloc(size); }
void PageFree(uint32_t) {}
uint32_t GuestStrdup(const char* s) {
    uint32_t a = HeapAlloc(static_cast<uint32_t>(strlen(s) + 1));
    if (a) memcpy(Ptr<void>(a), s, strlen(s) + 1);
    return a;
}
uint32_t GuestCall(uint32_t va, std::initializer_list<uint32_t> args, uint32_t ecx, uint32_t*) {
    CheckHost(ecx == 0, "test guest callback unexpectedly used thiscall");
    auto it = g_test_guest_functions.find(va);
    CheckHost(it != g_test_guest_functions.end(), "call to unregistered test guest function");
    return it->second(std::vector<uint32_t>(args));
}
void RegisterImports(const ImportDef* defs, size_t count) {
    for (size_t i = 0; i < count; ++i) Imports()[std::string(defs[i].dll) + "!" + defs[i].name] = defs[i];
}
uint32_t ImportThunk(const char*, const char*) { return 0x90000000; }
bool DispatchImport(Ctx*, uint32_t) { return false; }
std::string HostPath(const char* win_path) {
    if (!win_path) return {};
    std::string s = win_path;
    std::replace(s.begin(), s.end(), '\\', '/');
    if (!s.empty() && s[0] == '/') return s;
    if (s.size() >= 2 && s[1] == ':') s.erase(0, 2);
    while (!s.empty() && s[0] == '/') s.erase(s.begin());
    if (Lower(s).compare(0, 5, "game/") == 0) s.erase(0, 5);
    std::filesystem::path current = g_game_dir ? g_game_dir : ".";
    size_t at = 0;
    while (at <= s.size()) {
        size_t end = s.find('/', at); if (end == std::string::npos) end = s.size();
        std::string part = s.substr(at, end - at);
        if (!part.empty() && part != ".") {
            std::error_code ec;
            if (!std::filesystem::exists(current / part, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(current, ec)) {
                    if (Lower(entry.path().filename().string()) == Lower(part)) { part = entry.path().filename().string(); break; }
                }
            }
            current /= part;
        }
        if (end == s.size()) break;
        at = end + 1;
    }
    return current.string();
}
int DuplicateFileHandle(uint32_t h) {
    auto it = g_test_handles.find(h);
    return it == g_test_handles.end() ? -1 : fcntl(it->second, F_DUPFD_CLOEXEC, 0);
}
std::string Setting(const char*, const char* def) { return def ? def : ""; }
int SettingInt(const char*, int def) { return def; }
void PerfHintStart(int) {}
void PerfHintAddThread(int) {}
void PerfHintTarget(int) {}
void PerfHintFrame(int64_t) {}
}

namespace {
using namespace native;
using namespace native::bink;

void Check(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
uint32_t Call(const char* name, int args, std::initializer_list<uint32_t> values) {
    Check(static_cast<int>(values.size()) == args, std::string("bad test argument count for ") + name);
    std::string key = std::string("binkw32.dll!_") + name + "@" + std::to_string(args * 4);
    auto it = Imports().find(key);
    Check(it != Imports().end(), "missing import " + key);
    Ctx c {};
    c.esp = 0x400000;
    W32(c.esp, 0xdeadbeef);
    int i = 0; for (uint32_t value : values) W32(c.esp + 4 + 4 * i++, value);
    it->second.fn(&c);
    Check(c.ret_target == 0xdeadbeef && c.esp == uint32_t(0x400000 + 4 + 4 * args), "stdcall cleanup mismatch for " + key);
    return c.eax;
}
uint32_t OpenName(const std::string& name, uint32_t flags = 0) {
    uint32_t address = GuestStrdup(name.c_str());
    return Call("BinkOpen", 2, {address, flags});
}
uint32_t OpenRaw(uint32_t address, uint32_t flags) { return Call("BinkOpen", 2, {address, flags}); }
void Close(uint32_t h) { if (h) Call("BinkClose", 1, {h}); }
void Decode(uint32_t h) {
    // The real guest treats nonzero as a bounded NextFrame/DoFrame retry. This helper retries the same requested frame
    // so registered and CPU-copy tests can wait for one exact image without advancing past it.
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        uint32_t result = Call("BinkDoFrame", 1, {h});
        Check(result <= 1, "BinkDoFrame returned an invalid status");
        auto* p = Ptr<Public>(h);
        if (!result) {
            Check(p->last_frame_num == p->frame_num, "successful BinkDoFrame did not publish its frame");
            return;
        }
        Check(p->last_frame_num != p->frame_num, "fatal BinkDoFrame looked like a completed presentation");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(false, "BinkDoFrame made no progress after repeated deferred calls");
}
void Next(uint32_t h) { Call("BinkNextFrame", 1, {h}); }

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void Put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 255); }
void Set32(std::vector<uint8_t>& b, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b.at(at + i) = (v >> (8 * i)) & 255; }
uint32_t HashName(std::string name) {
    if (!name.empty() && name.front() == '\\') name.erase(name.begin());
    uint32_t h = 0;
    for (unsigned char byte : name) {
        int c = byte >= 128 ? int(byte) - 256 : int(byte);
        if (c < 0x61) c += 0x20;
        h = h * 31 + static_cast<uint32_t>(c);
    }
    return h;
}

void WriteRCF(const std::filesystem::path& path, const std::string& name, const std::vector<uint8_t>& data,
              bool hashed, bool wide = false) {
    std::vector<uint8_t> out(0x3c, 0);
    memcpy(out.data(), "ATG CORE CEMENT LIBRARY", 24);
    out[0x20] = 2; out[0x21] = wide ? 0 : 1; out[0x22] = 0; out[0x23] = 1;
    uint32_t data_offset = static_cast<uint32_t>(out.size());
    out.insert(out.end(), data.begin(), data.end());
    uint32_t dir_offset = static_cast<uint32_t>(out.size());
    Put32(out, hashed ? HashName(name) : 0); Put32(out, data_offset);
    if (wide) {
        Put32(out, static_cast<uint32_t>(data.size() + 32));  // legacy entry_size, not the range size
        Put32(out, static_cast<uint32_t>(data.size()));       // value_c: range size used by the engine
        Put32(out, 0);                                        // value_10
    } else {
        Put32(out, static_cast<uint32_t>(data.size()));
    }
    uint32_t names_offset = static_cast<uint32_t>(out.size());
    Put32(out, 0); Put32(out, 0);  // name table alignment/reserved
    Put32(out, 0); Put32(out, 0); Put32(out, 0); Put32(out, static_cast<uint32_t>(name.size() + 1));
    out.insert(out.end(), name.begin(), name.end()); out.push_back(0); out.insert(out.end(), 3, 0);
    Set32(out, 0x24, dir_offset); Set32(out, 0x28, wide ? 20 : 12); Set32(out, 0x2c, names_offset);
    Set32(out, 0x30, static_cast<uint32_t>(out.size() - names_offset)); Set32(out, 0x38, 1);
    std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(out.data()), out.size());
    Check(f.good(), "could not write RCF fixture");
}

void CheckPublic(uint32_t h) {
    auto* p = Ptr<Public>(h);
    Check(p && p->width == 1920 && p->height == 1080 && p->frames == 291, "unexpected Bink public header");
    Check(p->frame_num == 1 && p->num_tracks == 1 && p->frame_rate == 25 && p->frame_rate_div == 1,
          "unexpected frame rate/track metadata");
    Check(p->size > 1000000, "missing bounded Bink size");
}

void CheckOpenDecodeClose(uint32_t h, bool check_host_backlog = false) {
    Check(h != 0, "BinkOpen returned null");
    CheckPublic(h);
    Decode(h);
    if (check_host_backlog) {
        Call("BinkGoto", 3, {h, 50, 0});
        uint32_t status = Call("BinkDoFrame", 1, {h});
        auto* p = Ptr<Public>(h);
        Check(status == 0, "host/memory-backed deep decode status=" + std::to_string(status) +
              " read_error=" + std::to_string(p->read_error) + " frame=" + std::to_string(p->frame_num) +
              " last=" + std::to_string(p->last_frame_num));
        Check(p->last_frame_num == 50, "host/memory-backed deep decode did not finish synchronously");
    }
    Close(h);
}

uint32_t RegisterTestFrameBuffers(uint32_t h) {
    uint32_t fb_address = HeapAlloc(sizeof(FrameBuffers));
    Call("BinkGetFrameBuffersInfo", 2, {h, fb_address});
    auto* fb = Ptr<FrameBuffers>(fb_address);
    Check(fb->total_frames == 2 && fb->ya_width == 1920 && fb->crcb_width == 960, "framebuffer info ABI");
    for (const auto& set : fb->frames)
        Check(set.y.allocate == 1 && set.cr.allocate == 1 && set.cb.allocate == 1 &&
                  set.alpha.allocate == fb->frames[0].alpha.allocate,
              "BinkGetFrameBuffersInfo did not populate allocation flags");
    auto allocate_plane = [](Plane& p, uint32_t width, uint32_t height, bool negative) {
        uint32_t stride = width + 32;
        uint32_t raw = HeapAlloc(stride * height + 64);
        uint32_t base = raw + 32;
        memset(Ptr<void>(raw), 0xcd, stride * height + 64);
        p.allocate = 1; p.pitch = negative ? static_cast<uint32_t>(-static_cast<int>(stride)) : stride;
        p.buffer = negative ? base + (height - 1) * stride : base;
    };
    for (auto& set : fb->frames) {
        allocate_plane(set.y, fb->ya_width, fb->ya_height, true);
        allocate_plane(set.cr, fb->crcb_width, fb->crcb_height, true);
        allocate_plane(set.cb, fb->crcb_width, fb->crcb_height, true);
        if (set.alpha.allocate) allocate_plane(set.alpha, fb->ya_width, fb->ya_height, true);
    }
    Call("BinkRegisterFrameBuffers", 2, {h, fb_address});
    return fb_address;
}

void CheckNeutralPlanes(const FrameBuffers& fb) {
    for (const auto& set : fb.frames) {
        Check(*Ptr<uint8_t>(set.y.buffer) == 16 && *Ptr<uint8_t>(set.cr.buffer) == 128 &&
                  *Ptr<uint8_t>(set.cb.buffer) == 128 &&
                  (!set.alpha.buffer || *Ptr<uint8_t>(set.alpha.buffer) == 255),
              "neutral registered planes are not Y=16/Cb=Cr=128/A=255");
    }
}

void CheckPlaneCopyVariants() {
    AVFrame* frame = av_frame_alloc();
    Check(frame != nullptr, "could not allocate plane-variant frame");
    frame->format = AV_PIX_FMT_YUVA420P;
    frame->width = frame->height = 4;
    Check(av_frame_get_buffer(frame, 16) == 0, "could not allocate YUVA test planes");
    for (uint32_t row = 0; row < 4; ++row) {
        memset(frame->data[0] + row * frame->linesize[0], 20 + row, 4);
        memset(frame->data[3] + row * frame->linesize[3], 200 + row, 4);
    }
    for (uint32_t row = 0; row < 2; ++row) {
        memset(frame->data[1] + row * frame->linesize[1], 110 + row, 2);
        memset(frame->data[2] + row * frame->linesize[2], 120 + row, 2);
    }
    Public pub {};
    pub.width = pub.height = 4;
    FrameBuffers fb {};
    fb.total_frames = 2;
    fb.ya_width = fb.ya_height = 4;
    fb.crcb_width = fb.crcb_height = 2;
    auto allocate = [](Plane& p, uint32_t width, uint32_t height) {
        p.allocate = 1;
        p.pitch = width;
        p.buffer = HeapAlloc(width * height);
        memset(Ptr<void>(p.buffer), 0, width * height);
    };
    auto& only = fb.frames[0];
    allocate(only.y, 4, 4); allocate(only.cr, 2, 2); allocate(only.cb, 2, 2); allocate(only.alpha, 4, 4);
    // Set 1 is completely empty, as an unlocked/unallocated inactive set may be. Four-plane copy must still succeed.
    Check(CopyRegisteredFrame(frame, pub, fb), "four-plane copy rejected an empty inactive set");
    Check(*Ptr<uint8_t>(only.y.buffer) == 20 && *Ptr<uint8_t>(only.cb.buffer) == 110 &&
              *Ptr<uint8_t>(only.cr.buffer) == 120 && *Ptr<uint8_t>(only.alpha.buffer) == 200,
          "four-plane Y/Cb/Cr/A ordering");
    fb.frame_num = 1;  // active set is empty; the populated alternate set remains a valid presentation target
    *Ptr<uint8_t>(only.y.buffer) = 0;
    Check(CopyRegisteredFrame(frame, pub, fb) && *Ptr<uint8_t>(only.y.buffer) == 20,
          "copy did not use a populated set when the active set was empty");

    // Three-plane movies have no alpha destination and must not require one.
    only.alpha = {};
    frame->data[3] = nullptr;
    Check(CopyRegisteredFrame(frame, pub, fb), "three-plane copy required an alpha destination");
    FrameBuffers empty = fb;
    empty.frames[0] = {};
    empty.frames[1] = {};
    Check(!CopyRegisteredFrame(frame, pub, empty), "copy accepted two completely empty plane sets");
    av_frame_free(&frame);
}

void CheckContiguousPlaneCopy() {
    uint8_t y[16], cb[4], cr[4];
    for (uint32_t i = 0; i < 16; ++i) y[i] = static_cast<uint8_t>(20 + i);
    for (uint32_t i = 0; i < 4; ++i) { cb[i] = static_cast<uint8_t>(100 + i); cr[i] = static_cast<uint8_t>(120 + i); }
    AVFrame frame {};
    frame.data[0] = y; frame.data[1] = cb; frame.data[2] = cr;
    frame.linesize[0] = 4; frame.linesize[1] = frame.linesize[2] = 2;
    Public pub {};
    pub.width = pub.height = 4;
    FrameBuffers fb {};
    fb.total_frames = 1; fb.ya_width = fb.ya_height = 4; fb.crcb_width = fb.crcb_height = 2;
    uint32_t raw_y = HeapAlloc(24), raw_cb = HeapAlloc(12), raw_cr = HeapAlloc(12);
    memset(Ptr<void>(raw_y), 0xcd, 24); memset(Ptr<void>(raw_cb), 0xcd, 12); memset(Ptr<void>(raw_cr), 0xcd, 12);
    Plane& out_y = fb.frames[0].y; Plane& out_cb = fb.frames[0].cb; Plane& out_cr = fb.frames[0].cr;
    out_y = {1, raw_y + 4, 4}; out_cb = {1, raw_cb + 4, 2}; out_cr = {1, raw_cr + 4, 2};
    Check(CopyRegisteredFrame(&frame, pub, fb), "tightly packed positive-pitch planes were rejected");
    Check(memcmp(Ptr<void>(raw_y + 4), y, 16) == 0 && memcmp(Ptr<void>(raw_cb + 4), cb, 4) == 0 &&
              memcmp(Ptr<void>(raw_cr + 4), cr, 4) == 0,
          "contiguous positive-pitch copy changed plane order or contents");

    frame.data[0] = y + 12; frame.data[1] = cb + 2; frame.data[2] = cr + 2;
    frame.linesize[0] = -4; frame.linesize[1] = frame.linesize[2] = -2;
    out_y.buffer = raw_y + 16; out_y.pitch = static_cast<uint32_t>(-4);
    out_cb.buffer = raw_cb + 6; out_cb.pitch = static_cast<uint32_t>(-2);
    out_cr.buffer = raw_cr + 6; out_cr.pitch = static_cast<uint32_t>(-2);
    Check(CopyRegisteredFrame(&frame, pub, fb), "tightly packed bottom-up planes were rejected");
    Check(memcmp(Ptr<void>(raw_y + 4), y, 16) == 0 && memcmp(Ptr<void>(raw_cb + 4), cb, 4) == 0 &&
              memcmp(Ptr<void>(raw_cr + 4), cr, 4) == 0,
          "contiguous bottom-up copy did not use the lowest physical row");
    Check(CopyRegisteredFrame(nullptr, pub, fb), "tightly packed neutral fill was rejected");
    for (uint32_t i = 0; i < 16; ++i) Check(Ptr<uint8_t>(raw_y + 4)[i] == 16, "neutral Y fill was incomplete");
    for (uint32_t i = 0; i < 4; ++i) {
        Check(Ptr<uint8_t>(raw_cb + 4)[i] == 128 && Ptr<uint8_t>(raw_cr + 4)[i] == 128,
              "neutral chroma fill was incomplete");
    }
    for (uint32_t i = 0; i < 4; ++i) {
        Check(Ptr<uint8_t>(raw_y)[i] == 0xcd && Ptr<uint8_t>(raw_y + 20)[i] == 0xcd &&
                  Ptr<uint8_t>(raw_cb)[i] == 0xcd && Ptr<uint8_t>(raw_cb + 8)[i] == 0xcd &&
                  Ptr<uint8_t>(raw_cr)[i] == 0xcd && Ptr<uint8_t>(raw_cr + 8)[i] == 0xcd,
              "contiguous plane copy crossed its allocation guard");
    }
}

void CheckRegisteredAndCopies() {
    uint32_t h = OpenName("c:\\GaMe\\movies\\intro.bik");
    Check(h != 0, "loose open failed"); CheckPublic(h);
    uint32_t fb_address = RegisterTestFrameBuffers(h);
    auto* fb = Ptr<FrameBuffers>(fb_address);
    CheckNeutralPlanes(*fb);
    FrameBuffers invalid = *fb;
    invalid.frame_num = 2;
    Check(!CopyRegisteredFrame(nullptr, *Ptr<Public>(h), invalid), "invalid active plane-set index was accepted");
    CheckNeutralPlanes(*fb);
    Decode(h);
    Check(fb->frame_num == 0 && Ptr<Public>(h)->last_frame_num == 1, "first registered frame decode");
    auto* p = Ptr<Public>(h);
    std::vector<uint8_t> before_registration(p->width);
    const int32_t first_y = static_cast<int32_t>(fb->frames[0].y.buffer);
    memcpy(before_registration.data(), Ptr<void>(static_cast<uint32_t>(first_y)), p->width);
    Call("BinkRegisterFrameBuffers", 2, {h, fb_address});
    Check(memcmp(before_registration.data(), Ptr<void>(static_cast<uint32_t>(first_y)), p->width) == 0 &&
              p->last_frame_num == 1,
          "buffer re-registration erased a valid decoded presentation");
    uint32_t yv_pitch = p->width, yv_size = yv_pitch * p->height + 2 * (yv_pitch / 2) * (p->height / 2);
    uint32_t yv = HeapAlloc(yv_size);
    Check(Call("BinkCopyToBuffer", 7, {h, yv, yv_pitch, p->height, 0, 0, kSurfaceYV12}) == 0, "YV12 copy export");
    for (auto& set : fb->frames) {
        for (uint32_t row = 0; row < p->height; ++row) {
            int32_t address = static_cast<int32_t>(set.y.buffer) + static_cast<int32_t>(row) * static_cast<int32_t>(set.y.pitch);
            Check(memcmp(Ptr<void>(static_cast<uint32_t>(address)), Ptr<void>(yv + row * yv_pitch), p->width) == 0,
                  "registered Y plane differs from CPU YV12 in a non-active set");
        }
        for (uint32_t row = 0; row < p->height / 2; ++row) {
            uint32_t cr_address = static_cast<uint32_t>(static_cast<int32_t>(set.cr.buffer) + row * static_cast<int32_t>(set.cr.pitch));
            uint32_t cb_address = static_cast<uint32_t>(static_cast<int32_t>(set.cb.buffer) + row * static_cast<int32_t>(set.cb.pitch));
            Check(memcmp(Ptr<void>(cr_address), Ptr<void>(yv + yv_pitch * p->height + row * (yv_pitch / 2)), p->width / 2) == 0,
                  "registered Cr plane ordering/pitch");
            Check(memcmp(Ptr<void>(cb_address), Ptr<void>(yv + yv_pitch * p->height + (p->height / 2) * (yv_pitch / 2) + row * (yv_pitch / 2)), p->width / 2) == 0,
                  "registered Cb plane ordering/pitch");
        }
    }
    // Move a live plane without re-registering. DoFrame must re-read the guest structure, not cache its first pointer.
    Plane& live_y = fb->frames[0].y;
    live_y.buffer -= 16;
    int32_t live_lowest = static_cast<int32_t>(live_y.buffer) +
                          static_cast<int32_t>(p->height - 1) * static_cast<int32_t>(live_y.pitch);
    memset(Ptr<void>(static_cast<uint32_t>(live_lowest)), 0, (p->width + 32) * p->height);
    Check(Call("BinkDoFrame", 1, {h}) == 0, "live-pointer re-present failed");
    for (uint32_t row = 0; row < p->height; ++row) {
        uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(live_y.buffer) +
                                                row * static_cast<int32_t>(live_y.pitch));
        Check(memcmp(Ptr<void>(address), Ptr<void>(yv + row * yv_pitch), p->width) == 0,
              "DoFrame used a stale registered plane pointer");
    }
    uint32_t rgb_stride = p->width * 4 + 32, rgb_raw = HeapAlloc(rgb_stride * p->height + 64);
    uint32_t rgb_base = rgb_raw + 32;
    memset(Ptr<void>(rgb_raw), 0xcd, rgb_stride * p->height + 64);
    uint32_t rgb = rgb_base + (p->height - 1) * rgb_stride;
    Check(Call("BinkCopyToBufferRect", 11, {h, rgb, static_cast<uint32_t>(-static_cast<int>(rgb_stride)), p->height,
        0, 0, 0, 0, p->width, p->height, kSurface32A}) == 0, "negative-pitch RGB Rect copy");
    for (uint32_t i = 0; i < 32; ++i) {
        Check(Ptr<uint8_t>(rgb_base - 32)[i] == 0xcd, "RGB copy wrote before destination");
        Check(Ptr<uint8_t>(rgb_base + rgb_stride * p->height)[i] == 0xcd, "RGB copy wrote after destination");
    }
    Next(h); Check(p->frame_num == 2 && fb->frame_num == 1, "NextFrame did not advance buffer index"); Decode(h);
    p->frame_num = p->frames;  // exercise wrap without decoding the entire sample in this unit test
    Next(h); Check(p->frame_num == 1 && fb->frame_num == 0, "NextFrame end wrap"); Decode(h);
    Call("BinkPause", 2, {h, 1});
    auto before = std::chrono::steady_clock::now(); Call("BinkWait", 1, {h});
    Check(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count() < 100,
          "paused BinkWait slept unexpectedly");
    Call("BinkPause", 2, {h, 0});
    const uint8_t retained = *Ptr<uint8_t>(fb->frames[0].y.buffer);
    Close(h);
    Check(*Ptr<uint8_t>(fb->frames[0].y.buffer) == retained, "BinkClose overwrote a valid registered plane");
}

void CheckMonotonicWaitPacing() {
    Check(BinkMenuWaitUnlocked(1, 0), "default-on setting did not unlock a literal zero-track movie");
    Check(!BinkMenuWaitUnlocked(0, 0), "unlock_menu_fps=0 still unlocked a zero-track movie");
    Check(!BinkMenuWaitUnlocked(1, 1), "an audio-track movie was treated as a silent menu movie");
    Check(BinkMenuWaitUnlocked(-1, 0), "a nonzero enabled setting was treated as disabled");
    Check(BinkWaitPollResult(1.0 / 30.0, true, 0) == 1, "unlocked silent movie did not skip an early menu poll");
    Check(BinkWaitPollResult(0.0, true, 0) == 0, "unlocked silent movie blocked a due video frame");
    Check(BinkWaitPollResult(-0.001, true, 0) == 0, "overdue unlocked movie did not run its video frame");
    Check(BinkWaitPollResult(1.0 / 30.0, false, 0) == 0, "disabled menu unlock changed the blocking ABI result");
    Check(BinkWaitPollResult(1.0 / 30.0, true, 1) == 0, "audio-backed movie entered the menu-only fast path");

    Call("BinkSetSoundTrack", 2, {0, 0});  // no selected/audio clock: monotonic timing with an audio-track movie
    uint32_t h = OpenName("movies\\intro.bik");
    Check(h != 0, "monotonic-wait movie open failed");
    auto before = std::chrono::steady_clock::now();
    Call("BinkWait", 1, {h});
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - before).count();
    Check(elapsed >= 20 && elapsed < 500, "movie without an audio clock spun or waited without a bound");
    Close(h);
    uint32_t selection = HeapAlloc(4);
    W32(selection, 0);
    Call("BinkSetSoundTrack", 2, {1, selection});
}

void CheckAudioAndTrackApi() {
    uint32_t h = OpenName("movies\\intro.bik");
    Check(h != 0, "audio test open failed");
    Check(Call("BinkGetTrackID", 2, {h, 0}) == 0 && Call("BinkGetTrackType", 2, {h, 0}) == 0,
          "track type/ID lookup");
    Check(Call("BinkGetTrackMaxSize", 2, {h, 0}) >= 4096, "track max size");
    uint32_t track = Call("BinkOpenTrack", 2, {h, 0});
    Check(track && Ptr<TrackPublic>(track)->frequency == 44100 && Ptr<TrackPublic>(track)->channels == 2,
          "BinkOpenTrack public layout");
    Decode(h);
    uint32_t pcm = HeapAlloc(Ptr<TrackPublic>(track)->max_size);
    uint32_t pcm_bytes = Call("BinkGetTrackData", 2, {track, pcm});
    Check(pcm_bytes > 0 && pcm_bytes <= Ptr<TrackPublic>(track)->max_size,
          "legacy BinkGetTrackData returned no PCM");
    bool nonzero = false;
    for (int i = 0; i < 12; ++i) { SDLTestPumpAudio(1024); nonzero |= SDLTestOutputNonzero(); }
    Check(nonzero, "selected Bink audio track produced only silence");
    Call("BinkSetVolume", 3, {h, 0, 0});
    Decode(h); SDLTestPumpAudio(1024);
    Check(!SDLTestOutputNonzero(), "zero volume did not mute Bink audio");
    Call("BinkSetVolume", 3, {h, 0, 32768});
    uint32_t bins = HeapAlloc(8), volumes = HeapAlloc(8); W32(bins, 0); W32(volumes, 16384);
    Call("BinkSetMixBinVolumes", 5, {h, 0, bins, volumes, 1});
    Call("BinkSetPan", 3, {h, 0, static_cast<uint32_t>(-32768)});
    Next(h); Decode(h); SDLTestPumpAudio(1024);
    Call("BinkPause", 2, {h, 1}); SDLTestPumpAudio(1024);
    Check(!SDLTestOutputNonzero(), "paused movie audio was not silent");
    Call("BinkPause", 2, {h, 0});
    Call("BinkCloseTrack", 1, {track});
    Close(h);
    Check(Call("BinkGetTrackData", 2, {track, pcm}) == 0, "closed legacy track remained usable");
}

void CheckCustomIO(const std::vector<uint8_t>& bytes) {
    constexpr uint32_t kOpen = 0x91000000, kRead = 0x91000100, kReadFrame = 0x91000200;
    constexpr uint32_t kGetSize = 0x91000300, kSetInfo = 0x91000400, kIdle = 0x91000500;
    constexpr uint32_t kClose = 0x91000600, kBackground = 0x91000700;
    int opens = 0, header_reads = 0, frame_reads = 0, idles = 0, closes = 0, background = 0;
    bool info_set = false, slow_reads = false;
    uint32_t expected_io = 0;

    auto read = [&](const std::vector<uint32_t>& a, bool frame) -> uint32_t {
        Check(a.size() == (frame ? 5u : 4u), "custom read callback argument count");
        uint32_t io = a[0], offset = a[frame ? 2 : 1], dest = a[frame ? 3 : 2], size = a[frame ? 4 : 3];
        Check(io == expected_io && (!frame || info_set), "custom read arrived before SetInfo");
        Check(frame || size <= 16 * 1024, "custom AVIO read exceeded the synchronous guest path");
        if (slow_reads && !frame) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Check(offset <= bytes.size(), "custom read offset exceeds fixture");
        uint32_t count = static_cast<uint32_t>(std::min<size_t>(size, bytes.size() - offset));
        memcpy(Ptr<void>(dest), bytes.data() + offset, count);
        if (frame) ++frame_reads; else ++header_reads;
        return count;
    };
    g_test_guest_functions[kOpen] = [&](const std::vector<uint32_t>& a) {
        Check(a.size() == 3 && !strcmp(Str(a[1]), "movies\\synthetic.bik"), "custom opener filename/arguments");
        expected_io = a[0]; ++opens;
        W32(expected_io + 0x00, kRead); W32(expected_io + 0x04, kReadFrame);
        W32(expected_io + 0x08, kGetSize); W32(expected_io + 0x0c, kSetInfo);
        W32(expected_io + 0x10, kIdle); W32(expected_io + 0x14, kClose); W32(expected_io + 0x18, kBackground);
        return 1;
    };
    g_test_guest_functions[kRead] = [&](const std::vector<uint32_t>& a) { return read(a, false); };
    g_test_guest_functions[kReadFrame] = [&](const std::vector<uint32_t>& a) {
        // Reproduce the device contract: this entry point is stateful and must not be used for generic AVIO. Returning
        // a read error makes any regression fail the decode instead of silently passing with synthetic frame data.
        ++frame_reads;
        Check(a.size() == 5 && a[0] == expected_io, "unexpected custom ReadFrame invocation");
        W32(expected_io + 0x20, 1);
        return 0;
    };
    g_test_guest_functions[kGetSize] = [&](const std::vector<uint32_t>& a) {
        Check(a.size() == 2 && a[0] == expected_io, "GetBufferSize arguments");
        return (a[1] + 0x3fffu) & ~0x3fffu;
    };
    g_test_guest_functions[kSetInfo] = [&](const std::vector<uint32_t>& a) {
        Check(a.size() == 5 && a[0] == expected_io, "SetInfo arguments");
        Check((a[1] & 0x3fffu) == 0 && a[2] >= 65536 && a[3] == bytes.size() && a[4] == 0,
              "SetInfo alignment/size/file size");
        info_set = true; return 0;
    };
    g_test_guest_functions[kIdle] = [&](const std::vector<uint32_t>& a) {
        Check(a.size() == 1 && a[0] == expected_io, "Idle arguments"); ++idles; return 0;
    };
    g_test_guest_functions[kClose] = [&](const std::vector<uint32_t>& a) {
        Check(a.size() == 1 && a[0] == expected_io && info_set, "Close arguments/order"); ++closes; return 0;
    };
    g_test_guest_functions[kBackground] = [&](const std::vector<uint32_t>& a) {
        Check(a.size() == 2 && a[0] == expected_io, "BGControl arguments"); ++background; return 0;
    };

    Call("BinkSetIO", 1, {kOpen});
    CheckOpenDecodeClose(OpenName("movies\\intro.bik"), true);   // loose host fast path
    CheckOpenDecodeClose(OpenName("video\\intro.bik"), true);    // uncompressed RCF-slice host fast path
    Check(opens == 0 && header_reads == 0 && frame_reads == 0 && closes == 0,
          "host-resolvable custom-I/O movies entered guest callbacks");

    uint32_t h = OpenName("movies\\synthetic.bik");
    Check(h != 0, "synthetic custom-I/O fallback BinkOpen failed"); CheckPublic(h);
    uint32_t fb_address = RegisterTestFrameBuffers(h);
    auto* fb = Ptr<FrameBuffers>(fb_address);
    Call("BinkGoto", 3, {h, 1, 0});
    slow_reads = true;
    Check(Call("BinkDoFrame", 1, {h}) == 0 && Ptr<Public>(h)->last_frame_num == 1,
          "frame 1 deferred instead of completing synchronously");
    slow_reads = false;
    auto snapshot = [&]() {
        std::vector<std::vector<uint8_t>> samples;
        for (auto& set : fb->frames) {
            for (Plane* plane : {&set.y, &set.cr, &set.cb, &set.alpha}) {
                if (!plane->buffer) continue;
                uint8_t* at = Ptr<uint8_t>(plane->buffer);
                samples.emplace_back(at, at + 16);
            }
        }
        return samples;
    };
    auto retained = snapshot();
    for (auto& set : fb->frames)
        for (Plane* plane : {&set.y, &set.cr, &set.cb, &set.alpha})
            if (plane->buffer) memset(Ptr<void>(plane->buffer), 0, 16);
    Call("BinkGoto", 3, {h, 100, 0});  // force a real decode backlog, not just data already buffered during open
    auto* public_movie = Ptr<Public>(h);
    public_movie->last_frame_num = 77;  // sentinels: a deferred call must retain the last complete presentation
    public_movie->frame_size = 123;
    public_movie->num_rects = 1;
    slow_reads = true;
    auto before = std::chrono::steady_clock::now();
    Check(Call("BinkDoFrame", 1, {h}) == 1, "slow custom read did not return the guest's deferred status");
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count();
    Check(elapsed < 250, "slow custom read monopolized BinkDoFrame");
    Check(snapshot() == retained, "deferred decode did not restore retained Y/Cb/Cr/A into both live plane sets");
    slow_reads = false;
    uint32_t summary_address = HeapAlloc(sizeof(Summary));
    Call("BinkGetSummary", 2, {h, summary_address});
    Check(Ptr<Summary>(summary_address)->skipped_frames > 0, "slow-read fixture did not exercise deferred decoding");
    Check(public_movie->last_frame_num == 77 && public_movie->frame_size == 123 && public_movie->num_rects == 1 &&
              public_movie->read_error == 0,
          "deferred decoding discarded the previous presentation");
    Next(h);
    Check(public_movie->frame_num == 101 && fb->frame_num == 1, "deferred NextFrame did not advance normally");
    Decode(h);
    Next(h);
    Check(public_movie->frame_num == 102, "completed frame did not advance after a deferred retry");
    Decode(h);
    Call("BinkGoto", 3, {h, 200, 0});
    slow_reads = true;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        Check(Call("BinkDoFrame", 1, {h}) <= 1, "persistent slow decode returned an invalid status");
        Next(h);
    }
    slow_reads = false;
    Check(public_movie->frame_num == 204, "deferred retries did not have a bounded skip-forward path");
    Call("BinkControlBackgroundIO", 2, {h, 1});
    Call("BinkService", 1, {h});  // suspended: must not call guest Idle
    Call("BinkControlBackgroundIO", 2, {h, 0});
    Call("BinkService", 1, {h});
    Close(h);
    Check(opens == 1 && closes == 1 && header_reads > 0 && frame_reads == 0 && info_set && idles == 1 && background == 2,
          "custom BINKIO callback coverage/lifecycle; FFmpeg must not enter stateful ReadFrame");
    Call("BinkSetIO", 1, {0});
    for (uint32_t address : {kOpen, kRead, kReadFrame, kGetSize, kSetInfo, kIdle, kClose, kBackground})
        g_test_guest_functions.erase(address);
    CheckOpenDecodeClose(OpenName("movies\\intro.bik"));  // built-in loose-file fallback after restoring the default
}

}  // namespace

using namespace native;

int main(int argc, char** argv) {
    try {
        Check(argc >= 3, "usage: bink_test <sample.bik> <fixture-dir>");
        std::filesystem::path sample = argv[1], root = argv[2];
        std::filesystem::remove_all(root); std::filesystem::create_directories(root / "Movies");
        std::filesystem::copy_file(sample, root / "Movies" / "Intro.BIK");
        auto bytes = ReadBytes(sample);
        Check(bytes.size() > 1000000, "sample is unexpectedly small");
        std::vector<uint8_t> compressed(16 + compressBound(bytes.size()));
        memcpy(compressed.data(), "RZ\0\0", 4);
        Set32(compressed, 8, static_cast<uint32_t>(bytes.size()));
        uLongf compressed_size = static_cast<uLongf>(compressed.size() - 16);
        Check(compress2(compressed.data() + 16, &compressed_size, bytes.data(), bytes.size(), 1) == Z_OK,
              "could not create RZ fixture");
        compressed.resize(16 + compressed_size);
        WriteRCF(root / "Movies.RCF", "Video\\Intro.bik", bytes, true);
        WriteRCF(root / "WOI.RCF", "Video\\Compressed.bik.rz", compressed, false);
        WriteRCF(root / "Wide.RCF", "Video\\Wide.bik", bytes, true, true);
        std::ofstream(root / "Other.RCF", std::ios::binary).write("not an archive", 14);

        void* base = mmap(reinterpret_cast<void*>(kGuestBase), 0x100000000ull, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        Check(base == reinterpret_cast<void*>(kGuestBase), "could not reserve guest test window");
        void* heap = mmap(reinterpret_cast<void*>(kGuestBase + 0x100000), kHeapBytes, PROT_READ | PROT_WRITE,
                          MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        Check(heap == reinterpret_cast<void*>(kGuestBase + 0x100000), "could not map guest test heap");
        g_game_dir = root.c_str();
        if (const char* rate = getenv("BINK_TEST_AUDIO_RATE")) g_sdl_test_forced_rate = atoi(rate);
        av_log_set_level(AV_LOG_ERROR);
        Check(avcodec_find_decoder(AV_CODEC_ID_BINKAUDIO_RDFT) && avcodec_find_decoder(AV_CODEC_ID_BINKAUDIO_DCT),
              "minimal FFmpeg is missing a Bink audio decoder");
        uint32_t ids = HeapAlloc(4); W32(ids, 0); Call("BinkSetSoundTrack", 2, {1, ids});

        CheckOpenDecodeClose(OpenName("c:\\GaMe\\movies\\intro.bik"));
        CheckOpenDecodeClose(OpenName("video\\intro.bik"));
        CheckOpenDecodeClose(OpenName("video/compressed.BIK"));
        CheckOpenDecodeClose(OpenName("video\\wide.bik"));
        uint32_t memory = HeapAlloc(static_cast<uint32_t>(bytes.size()));
        memcpy(Ptr<void>(memory), bytes.data(), bytes.size());
        CheckOpenDecodeClose(OpenRaw(memory, kFromMemory), true);
        int fd = open((root / "Movies" / "Intro.BIK").c_str(), O_RDONLY | O_CLOEXEC);
        Check(fd >= 0, "could not open file-handle fixture");
        g_test_handles[0x100] = fd;
        CheckOpenDecodeClose(OpenRaw(0x100, kFileHandle));
        close(fd); g_test_handles.erase(0x100);
        CheckCustomIO(bytes);

        CheckPlaneCopyVariants();
        CheckContiguousPlaneCopy();
        CheckRegisteredAndCopies();
        CheckMonotonicWaitPacing();
        uint32_t selection = HeapAlloc(300 * 4);
        for (uint32_t i = 0; i < 300; ++i) W32(selection + i * 4, 0x1000 + i);
        W32(selection + 299 * 4, 0);  // the real ID is deliberately beyond the former 256-entry truncation
        Call("BinkSetSoundTrack", 2, {300, selection});
        CheckAudioAndTrackApi();
        uint32_t palette = HeapAlloc(1024); Call("BinkGetPalette", 1, {palette});
        Check(R32(palette) == 0 && R32(palette + 4) == (36u << 16) && R32(palette + 64 * 4) == 85,
              "Bink 8-bit palette values");
        std::cout << "Bink tests passed: loose/RCF/RZ/memory/file-handle/custom-IO input, decoder, ABI, planes, CPU copies, wrap, audio controls\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Bink test failure: " << e.what() << "\n";
        return 1;
    }
}
