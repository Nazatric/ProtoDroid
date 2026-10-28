// D3D9 for the native host: the engine's COM calls are forwarded to DXVK (D3D9 on Vulkan, built natively).
//
// Guest objects are 16-byte blocks in guest memory: [0] guest vtable (thunk addresses), [4] index of the host
// object. Method handlers are generated from d3d9.h (tools/gen_d3d9_bridge.py -> d3d9_gen.inc); this file holds the
// object table and the methods whose arguments need conversion: structures with window handles (present
// parameters) and Lock/Unlock, where the host's mapped pointer lies outside guest memory and a guest shadow buffer
// is copied in and out.
#include <SDL2/SDL.h>
#include <d3d9.h>

#include <unistd.h>
#ifdef __ANDROID__
#include <jni.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "win.h"
#include "d3d9_dxt.h"
#include "d3d9_state.h"

namespace native {

SDL_Window* g_sdl_window = nullptr;
uint32_t HostWindow();
bool BinkHasOpenMovie();
void ClientSize(int* w, int* h);     // user32.cpp
void SetClientSize(int w, int h);

namespace {

struct MethodDef {
    const char* name;
    ImportFn fn;
};
struct InterfaceDef {
    const char* name;
    const MethodDef* methods;
    size_t count;
};

std::recursive_mutex g_mutex;
// index -> host object, read without a lock by every bridged call: fixed chunks that never move, appended to
// under g_mutex.
class HostTable {
public:
    void* Get(uint32_t i) const {
        return chunks_[i >> kBits].load(std::memory_order_acquire)[i & kMask].load(std::memory_order_acquire);
    }
    void Set(uint32_t i, void* p) {
        chunks_[i >> kBits].load(std::memory_order_relaxed)[i & kMask].store(p, std::memory_order_release);
    }
    uint32_t Append(void* p) {
        uint32_t i = size_;
        if ((i >> kBits) >= kChunks) Fatal("d3d9: too many objects");
        if (!chunks_[i >> kBits].load(std::memory_order_relaxed)) {
            chunks_[i >> kBits].store(new std::atomic<void*>[kMask + 1](), std::memory_order_release);
        }
        Set(i, p);
        ++size_;
        return i;
    }

private:
    static constexpr uint32_t kBits = 14, kMask = (1u << kBits) - 1, kChunks = 4096;
    std::atomic<std::atomic<void*>*> chunks_[kChunks] = {};
    uint32_t size_ = 0;
};
HostTable g_hosts;
// host object -> guest object. Child surfaces die with their texture without a Release through the bridge, so an
// entry can be stale: a new object at the same address with another interface must not reuse it.
std::unordered_map<void*, uint32_t> g_guest_of;
std::map<std::string, uint32_t> g_vtables;       // interface name -> guest vtable

template <typename T>
T* Host(uint32_t guest) {
    return static_cast<T*>(g_hosts.Get(R32(guest + 4)));
}

template <typename T>
T* Unwrap(uint32_t guest) {
    return guest ? Host<T>(guest) : nullptr;
}

uint32_t WrapAs(void* host, const char* iface) {
    if (!host) return 0;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto vt = g_vtables.find(iface);
    if (vt == g_vtables.end()) Fatal("d3d9: no vtable for %s", iface);
    auto it = g_guest_of.find(host);
    if (it != g_guest_of.end() && R32(it->second) == vt->second) return it->second;
    uint32_t g = HeapAlloc(16);
    W32(g, vt->second);
    W32(g + 4, g_hosts.Append(host));
    g_guest_of[host] = g;
    return g;
}

void Forget(uint32_t guest) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    uint32_t index = R32(guest + 4);
    auto it = g_guest_of.find(g_hosts.Get(index));
    if (it != g_guest_of.end() && it->second == guest) g_guest_of.erase(it);
    g_hosts.Set(index, nullptr);
    W32(guest, 0);
    // The guest block is not reused: a stale guest pointer then faults on a null vtable instead of aliasing.
}

uint32_t Wrap(IDirect3D9* p) { return WrapAs(p, "IDirect3D9"); }
uint32_t Wrap(IDirect3DDevice9* p) { return WrapAs(p, "IDirect3DDevice9"); }
uint32_t Wrap(IDirect3DSwapChain9* p) { return WrapAs(p, "IDirect3DSwapChain9"); }
uint32_t Wrap(IDirect3DSurface9* p) { return WrapAs(p, "IDirect3DSurface9"); }
uint32_t Wrap(IDirect3DVolume9* p) { return WrapAs(p, "IDirect3DVolume9"); }
uint32_t Wrap(IDirect3DVertexBuffer9* p) { return WrapAs(p, "IDirect3DVertexBuffer9"); }
uint32_t Wrap(IDirect3DIndexBuffer9* p) { return WrapAs(p, "IDirect3DIndexBuffer9"); }
uint32_t Wrap(IDirect3DTexture9* p) { return WrapAs(p, "IDirect3DTexture9"); }
uint32_t Wrap(IDirect3DCubeTexture9* p) { return WrapAs(p, "IDirect3DCubeTexture9"); }
uint32_t Wrap(IDirect3DVolumeTexture9* p) { return WrapAs(p, "IDirect3DVolumeTexture9"); }
uint32_t Wrap(IDirect3DVertexDeclaration9* p) { return WrapAs(p, "IDirect3DVertexDeclaration9"); }
uint32_t Wrap(IDirect3DVertexShader9* p) { return WrapAs(p, "IDirect3DVertexShader9"); }
uint32_t Wrap(IDirect3DPixelShader9* p) { return WrapAs(p, "IDirect3DPixelShader9"); }
uint32_t Wrap(IDirect3DStateBlock9* p) { return WrapAs(p, "IDirect3DStateBlock9"); }
uint32_t Wrap(IDirect3DQuery9* p) { return WrapAs(p, "IDirect3DQuery9"); }
uint32_t Wrap(IDirect3DBaseTexture9* p) {
    if (!p) return 0;
    switch (p->GetType()) {
        case D3DRTYPE_CUBETEXTURE: return WrapAs(p, "IDirect3DCubeTexture9");
        case D3DRTYPE_VOLUMETEXTURE: return WrapAs(p, "IDirect3DVolumeTexture9");
        default: return WrapAs(p, "IDirect3DTexture9");
    }
}
[[maybe_unused]] uint32_t Wrap(IDirect3DResource9* p) {
    if (!p) return 0;
    switch (p->GetType()) {
        case D3DRTYPE_SURFACE: return WrapAs(p, "IDirect3DSurface9");
        case D3DRTYPE_VOLUME: return WrapAs(p, "IDirect3DVolume9");
        case D3DRTYPE_VERTEXBUFFER: return WrapAs(p, "IDirect3DVertexBuffer9");
        case D3DRTYPE_INDEXBUFFER: return WrapAs(p, "IDirect3DIndexBuffer9");
        default: return Wrap(static_cast<IDirect3DBaseTexture9*>(p));
    }
}

HWND HostHwnd(uint32_t guest_hwnd) { return guest_hwnd ? reinterpret_cast<HWND>(g_sdl_window) : nullptr; }

void ReturnFStd(Ctx* c, float v, int args) {
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = v;
    c->ret_target = Pop32(c);
    c->esp += 4 * args;
}

void ForgetResource(void* host);  // lock shadows and texture adaptation state of a released object

// Vertex and index buffers whose last reference the engine dropped live on for a few frames: the engine sometimes
// still locks a buffer right after releasing it, which on Windows works (the object outlives its last public
// reference while the device uses it) but would hit the bridge's null vtable here. The bridge keeps the engine's
// last reference and drops it at a later Present (or before Reset, which needs default-pool buffers gone).
struct Grave {
    uint32_t guest;
    IUnknown* host;
    uint32_t frame;
};
std::vector<Grave> g_graves;
uint32_t g_present_count = 0;
constexpr uint32_t kGraveFrames = 3;

ULONG RefCount(IUnknown* p) {
    p->AddRef();
    return p->Release();
}

void DrainGraves(bool all) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    size_t kept = 0;
    for (const Grave& gr : g_graves) {
        if (!all && g_present_count - gr.frame < kGraveFrames) {
            g_graves[kept++] = gr;
            continue;
        }
        if (RefCount(gr.host) == 1) {
            ForgetResource(gr.host);
            Forget(gr.guest);
        }
        gr.host->Release();  // revived by the engine meanwhile: its own references keep it
    }
    g_graves.resize(kept);
}

template <typename T>
void ReleaseImpl(Ctx* c) {
    uint32_t g = Arg(c, 0);
    T* host = Host<T>(g);
    if constexpr (std::is_same_v<T, IDirect3DVertexBuffer9> || std::is_same_v<T, IDirect3DIndexBuffer9>) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        for (const Grave& gr : g_graves) {
            if (gr.host == host && RefCount(host) == 1) {  // released again while already pending
                Return(c, 0, 1);
                return;
            }
        }
        if (RefCount(host) == 1) {
            g_graves.push_back({g, host, g_present_count});
            Return(c, 0, 1);
            return;
        }
    }
    ULONG r = host->Release();
    if (r == 0) {
        ForgetResource(host);
        Forget(g);
    }
    Return(c, r, 1);
}

template <typename T>
void QueryInterfaceImpl(Ctx* c) {
    uint32_t g = Arg(c, 0), out = Arg(c, 2);
    const GUID& riid = *Ptr<GUID>(Arg(c, 1));
    if (riid == __uuidof(IUnknown) || riid == __uuidof(T)) {
        Host<T>(g)->AddRef();
        W32(out, g);
        Return(c, S_OK, 3);
        return;
    }
    Log("d3d9: QueryInterface for an unsupported interface");
    W32(out, 0);
    Return(c, E_NOINTERFACE, 3);
}

// ---- present parameters: 56 bytes in guest memory, hDeviceWindow at +28 ----
D3DPRESENT_PARAMETERS ToHost(uint32_t g) {
    D3DPRESENT_PARAMETERS p = {};
    p.BackBufferWidth = R32(g + 0);
    p.BackBufferHeight = R32(g + 4);
    p.BackBufferFormat = D3DFORMAT(R32(g + 8));
    p.BackBufferCount = R32(g + 12);
    p.MultiSampleType = D3DMULTISAMPLE_TYPE(R32(g + 16));
    p.MultiSampleQuality = R32(g + 20);
    p.SwapEffect = D3DSWAPEFFECT(R32(g + 24));
    p.hDeviceWindow = HostHwnd(R32(g + 28) ? R32(g + 28) : HostWindow());
    p.Windowed = R32(g + 32);
    p.EnableAutoDepthStencil = R32(g + 36);
    p.AutoDepthStencilFormat = D3DFORMAT(R32(g + 40));
    p.Flags = R32(g + 44);
    p.FullScreen_RefreshRateInHz = R32(g + 48);
    p.PresentationInterval = R32(g + 52);
    // Android: the SDL window already covers the screen and exclusive fullscreen has no meaning there.
#ifdef __ANDROID__
    const bool windowed = true;
#else
    const bool windowed = getenv("PROTORE_WINDOWED") != nullptr;
#endif
    if (windowed) {
        p.Windowed = TRUE;
        p.FullScreen_RefreshRateInHz = 0;
    }
#ifdef __ANDROID__
    // Mali G720 workaround (Issue 1): allow forcing FIFO present mode to avoid MAILBOX double-recreation race.
    // protore.ini: present_mode = fifo | mailbox, or present_fifo = 1, or vsync = 1/0
    {
        std::string pm = Setting("present_mode", "");
        int fifo = SettingInt("present_fifo", 0);
        int vsync = SettingInt("vsync", -1);
        if (pm == "fifo" || pm == "FIFO" || pm == "vsync" || fifo == 1 || vsync == 1) {
            p.PresentationInterval = 1;  // D3DPRESENT_INTERVAL_ONE -> FIFO
        } else if (pm == "mailbox" || pm == "immediate" || vsync == 0) {
            p.PresentationInterval = 0x80000000u;  // D3DPRESENT_INTERVAL_IMMEDIATE -> MAILBOX if available
        }
    }
#endif
    return p;
}

void ToGuest(const D3DPRESENT_PARAMETERS& p, uint32_t g) {
    W32(g + 0, p.BackBufferWidth);
    W32(g + 4, p.BackBufferHeight);
    W32(g + 8, p.BackBufferFormat);
    W32(g + 12, p.BackBufferCount);
    W32(g + 16, p.MultiSampleType);
    W32(g + 20, p.MultiSampleQuality);
    W32(g + 24, p.SwapEffect);
    W32(g + 28, HostWindow());
    W32(g + 36, p.EnableAutoDepthStencil);
    W32(g + 40, p.AutoDepthStencilFormat);
    W32(g + 44, p.Flags);
    W32(g + 52, p.PresentationInterval);
}

// ---- lock shadows ----
struct Shadow {
    uint32_t guest = 0, capacity = 0, size = 0;
    void* host = nullptr;
    bool write_back = false;
};
std::map<std::pair<void*, uint32_t>, Shadow> g_shadows;  // (object, subresource) -> shadow

bool IsBlockCompressed(D3DFORMAT f) {
    return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5 ||
           f == D3DFORMAT(MAKEFOURCC('A', 'T', 'I', '1')) || f == D3DFORMAT(MAKEFOURCC('A', 'T', 'I', '2'));
}

// Maps the host's locked memory into a guest buffer: copies it in unless the lock discards or never overwrites
// (the guest then only writes), and remembers to copy it back on unlock unless read-only.
uint32_t ShadowLock(void* object, uint32_t sub, void* host, uint32_t size, DWORD flags) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    Shadow& s = g_shadows[{object, sub}];
    if (s.capacity < size) {
        if (s.guest) HeapFree(s.guest);
        s.guest = HeapAlloc(size);
        s.capacity = size;
    }
    s.size = size;
    s.host = host;
    s.write_back = !(flags & D3DLOCK_READONLY);
    if (!(flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE))) memcpy(Ptr<void>(s.guest), host, size);
    return s.guest;
}

void ShadowUnlock(void* object, uint32_t sub) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto it = g_shadows.find({object, sub});
    if (it == g_shadows.end() || !it->second.host) return;
    Shadow& s = it->second;
    if (s.write_back) memcpy(s.host, Ptr<void>(s.guest), s.size);
    s.host = nullptr;
}

void ForgetShadows(void* object) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    for (auto it = g_shadows.lower_bound({object, 0}); it != g_shadows.end() && it->first.first == object;) {
        if (it->second.guest) HeapFree(it->second.guest);
        it = g_shadows.erase(it);
    }
}

uint32_t RectRows(const D3DSURFACE_DESC& d, const RECT* r) {
    uint32_t h = r ? uint32_t(r->bottom - r->top) : d.Height;
    return IsBlockCompressed(d.Format) ? (h + 3) / 4 : h;
}

// LockRect common part: D3DLOCKED_RECT in guest memory is {INT Pitch; void* pBits} (8 bytes).
void LockRectCommon(Ctx* c, void* object, uint32_t sub, HRESULT hr, const D3DLOCKED_RECT& lr,
                    const D3DSURFACE_DESC& desc, const RECT* rect, DWORD flags, uint32_t out, int args) {
    if (SUCCEEDED(hr)) {
        uint32_t size = uint32_t(lr.Pitch) * RectRows(desc, rect);
        W32(out, uint32_t(lr.Pitch));
        W32(out + 4, ShadowLock(object, sub, lr.pBits, size, flags));
    }
    Return(c, uint32_t(hr), args);
}

// ---- texture adaptation for mobile GPUs ----
// Invisible to the engine, which keeps seeing the textures it asked for:
//   - mip skipping (setting texture_skip_mips): a large mipmapped texture is created `skip` levels smaller; the
//     guest's top levels go to a scratch buffer and are dropped. Halves texture resolution per level, quarters
//     memory and bandwidth.
//   - DXT decoding (setting decode_dxt, automatic when the GPU has no BC formats, e.g. Mali): DXT1/3/5 textures are
//     created as A8R8G8B8 and decoded on unlock.
struct TexAdapt {
    D3DFORMAT format;   // what the guest asked for
    UINT width, height;
    UINT skip;          // guest level L is host level L - skip; levels below skip are dropped
    bool decode;        // host texture is decode_format, guest writes DXT blocks
    D3DFORMAT decode_format;  // A8R8G8B8, or A1R5G5B5 for DXT1 (half the memory, keeps its 1-bit alpha)
};
std::unordered_map<void*, TexAdapt> g_adapt;  // host texture -> adaptation, only for adapted textures
int g_skip_mips = -1;                          // resolved on first texture
UINT g_skip_min_size = 512;                    // only textures at least this large (larger side) lose levels
bool g_decode_dxt = false;
bool g_decode_16bit = false;  // decode_16bit: DXT1 -> A1R5G5B5 (Android default when the GPU samples it)

bool IsDxt(D3DFORMAT f) {
    return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5;
}

// Bytes per 4x4 block (DXT) or per pixel (plain formats the adaptation handles); 0 = not adaptable.
UINT BlockBytes(D3DFORMAT f) {
    switch (f) {
        case D3DFMT_DXT1: return 8;
        case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5: return 16;
        case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
        default: return 0;
    }
}

// Pitch and row count of a guest level (or of `rect` within it) in the guest's format.
void GuestLevelLayout(D3DFORMAT f, UINT w, UINT h, const RECT* rect, UINT* pitch, UINT* rows) {
    UINT rh = rect ? UINT(rect->bottom - rect->top) : h;
    if (IsDxt(f)) {
        *pitch = ((w + 3) / 4) * BlockBytes(f);
        *rows = (rh + 3) / 4;
    } else {
        *pitch = w * BlockBytes(f);
        *rows = rh;
    }
}

const TexAdapt* AdaptOf(void* host) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto it = g_adapt.find(host);
    return it == g_adapt.end() ? nullptr : &it->second;
}

// Decides how a texture of the guest's description is created on the host.
TexAdapt PlanTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT format) {
    if (g_skip_mips < 0) {
#ifdef __ANDROID__
        g_skip_mips = SettingInt("texture_skip_mips", 1);
#else
        g_skip_mips = SettingInt("texture_skip_mips", 0);
#endif
        g_skip_min_size = UINT(SettingInt("texture_skip_min_size", 512));
        Log("textures: skip %d mip level(s) of textures >= %u, decode DXT %s", g_skip_mips, g_skip_min_size,
            g_decode_dxt ? "on" : "off");
    }
    TexAdapt a{format, w, h, 0, false, D3DFMT_A8R8G8B8};
    bool plain = !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC));
    if (plain && BlockBytes(format) && levels != 1 && (w > h ? w : h) >= g_skip_min_size) {
        UINT skip = UINT(g_skip_mips);
        // Keep at least one level and 4x4 blocks on the host.
        while (skip && ((levels && skip >= levels) || (w >> skip) < 4 || (h >> skip) < 4)) --skip;
        a.skip = skip;
    }
    a.decode = plain && g_decode_dxt && IsDxt(format);
    if (a.decode && g_decode_16bit && format == D3DFMT_DXT1) a.decode_format = D3DFMT_A1R5G5B5;
    // Statistics: GPU memory of the top level as the guest asked for it and as created.
    static uint64_t count = 0, reduced = 0, decoded = 0, guest_bytes = 0, host_bytes = 0;
    UINT bb = BlockBytes(format);
    if (bb) {
        uint64_t px = uint64_t(w) * h;
        uint64_t g = IsDxt(format) ? px * bb / 16 : px * bb;
        uint64_t hpx = px >> (2 * a.skip);
        guest_bytes += g;
        host_bytes += a.decode ? hpx * (a.decode_format == D3DFMT_A1R5G5B5 ? 2 : 4) : g >> (2 * a.skip);
    }
    ++count;
    reduced += a.skip != 0;
    decoded += a.decode;
    if ((count & 1023) == 0) {
        Log("textures: %llu created, %llu reduced, %llu decoded; top levels %llu MiB -> %llu MiB",
            (unsigned long long)count, (unsigned long long)reduced, (unsigned long long)decoded,
            (unsigned long long)(guest_bytes >> 20), (unsigned long long)(host_bytes >> 20));
    }
    return a;
}

// ---- locks of adapted textures ----
struct AdaptedLock {
    uint32_t guest = 0, capacity = 0;  // guest buffer in the guest format
    UINT pitch = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN, decode_format = D3DFMT_A8R8G8B8;
    UINT width = 0, height = 0;         // region being written
    uint8_t* host = nullptr;            // locked host memory (decode target), null for dropped levels
    INT host_pitch = 0;
};
std::map<std::pair<void*, uint32_t>, AdaptedLock> g_adapted_locks;

// Locks guest level `level` of an adapted texture whose host level is locked through `lock_host`
// (HRESULT(UINT host_level, D3DLOCKED_RECT*)); writes D3DLOCKED_RECT to the guest.
template <typename LockHost>
HRESULT AdaptedLockRect(void* tex, uint32_t sub, const TexAdapt& a, UINT level, const RECT* rect, DWORD flags,
                        uint32_t out, LockHost lock_host) {
    UINT w = a.width >> level ? a.width >> level : 1, h = a.height >> level ? a.height >> level : 1;
    UINT pitch, rows;
    GuestLevelLayout(a.format, w, h, rect, &pitch, &rows);
    D3DLOCKED_RECT lr{};
    bool dropped = level < a.skip;
    if (!dropped) {
        HRESULT hr = lock_host(level - a.skip, &lr);
        if (FAILED(hr)) return hr;
        if (!a.decode) {  // same format and size on the host: plain shadow
            W32(out, uint32_t(lr.Pitch));
            W32(out + 4, ShadowLock(tex, sub, lr.pBits, uint32_t(lr.Pitch) * rows, flags));
            return D3D_OK;
        }
    }
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    AdaptedLock& l = g_adapted_locks[{tex, sub}];
    uint32_t size = pitch * rows;
    if (l.capacity < size) {
        if (l.guest) HeapFree(l.guest);
        l.guest = HeapAlloc(size);
        l.capacity = size;
    }
    l.pitch = pitch;
    l.format = a.format;
    l.decode_format = a.decode_format;
    l.width = rect ? UINT(rect->right - rect->left) : w;
    l.height = rect ? UINT(rect->bottom - rect->top) : h;
    l.host = dropped ? nullptr : static_cast<uint8_t*>(lr.pBits);
    l.host_pitch = lr.Pitch;
    W32(out, pitch);
    W32(out + 4, l.guest);
    return D3D_OK;
}

// Returns true when the lock was a decoded or dropped one (the host level, if any, must still be unlocked).
bool AdaptedUnlock(void* tex, uint32_t sub, bool* host_locked) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto it = g_adapted_locks.find({tex, sub});
    if (it == g_adapted_locks.end() || !it->second.guest) return false;
    AdaptedLock& l = it->second;
    *host_locked = l.host != nullptr;
    if (l.host) {
        DecodeDxt(l.format, Ptr<uint8_t>(l.guest), l.pitch, l.width, l.height, l.host, l.host_pitch, l.decode_format);
        l.host = nullptr;
    }
    return true;
}

void ForgetResource(void* host) {
    ForgetShadows(host);
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_adapt.erase(host);
    for (auto it = g_adapted_locks.lower_bound({host, 0}); it != g_adapted_locks.end() && it->first.first == host;) {
        if (it->second.guest) HeapFree(it->second.guest);
        it = g_adapted_locks.erase(it);
    }
}

// Guest view of a level description: guest size and format.
void AdaptDesc(const TexAdapt& a, UINT level, D3DSURFACE_DESC* d) {
    d->Format = a.format;
    d->Width = a.width >> level ? a.width >> level : 1;
    d->Height = a.height >> level ? a.height >> level : 1;
}


// ---- display modes ----
// With override_modes (default on Android) the engine sees modes of the render resolution instead of the screen's:
// the render size (render_height) first, then 540p/720p/900p/1080p at the screen's aspect ratio. Its default
// fullscreen choice, the back buffer and the window client area then agree, and touch coordinates map 1:1.
bool OverrideModes() {
#ifdef __ANDROID__
    static const bool on = SettingInt("override_modes", 1) != 0;
#else
    static const bool on = SettingInt("override_modes", 0) != 0;
#endif
    return on;
}

std::vector<D3DDISPLAYMODE> Modes() {
    static std::vector<D3DDISPLAYMODE> modes;
    if (modes.empty()) {
        int cw, ch;
        ClientSize(&cw, &ch);
        modes.push_back({UINT(cw), UINT(ch), 60, D3DFMT_X8R8G8B8});
        for (UINT h : {540u, 720u, 900u, 1080u}) {
            UINT w = (UINT(uint64_t(h) * UINT(cw) / UINT(ch)) + 4) & ~7u;
            if (h != UINT(ch)) modes.push_back({w, h, 60, D3DFMT_X8R8G8B8});
        }
    }
    return modes;
}

void WriteMode(uint32_t out, const D3DDISPLAYMODE& m) {
    W32(out, m.Width);
    W32(out + 4, m.Height);
    W32(out + 8, m.RefreshRate);
    W32(out + 12, uint32_t(m.Format));
}

// Minimap placement is exclusively a host D3D9 submission operation; no guest movie or engine object is accessed.
// Java/ini offsets use logical 1600x720 HUD pixels; zero/zero is the stock Bottom-Left.
std::mutex g_minimap_config_mutex;
std::atomic<bool> g_minimap_configured{false};
std::atomic<int> g_minimap_x{0}, g_minimap_y{0};
std::atomic<uint32_t> g_minimap_generation{0};

[[maybe_unused]] void SetPosition(int x, int y) {
    x = std::clamp(x, -2000, 2000);
    y = std::clamp(y, -2000, 2000);
    {
        std::lock_guard<std::mutex> lock(g_minimap_config_mutex);
        g_minimap_x.store(x, std::memory_order_relaxed);
        g_minimap_y.store(y, std::memory_order_relaxed);
        g_minimap_configured.store(true, std::memory_order_release);
        g_minimap_generation.fetch_add(1, std::memory_order_relaxed);
    }
    Log("minimap: live D3D9 draw offset %d,%d logical HUD px", x, y);
}

void EnsureMinimapConfigured() {
    if (g_minimap_configured.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(g_minimap_config_mutex);
    if (g_minimap_configured.load(std::memory_order_relaxed)) return;
    g_minimap_x.store(std::clamp(SettingInt("minimap_x", 0), -2000, 2000), std::memory_order_relaxed);
    g_minimap_y.store(std::clamp(SettingInt("minimap_y", 0), -2000, 2000), std::memory_order_relaxed);
    g_minimap_configured.store(true, std::memory_order_release);
    g_minimap_generation.fetch_add(1, std::memory_order_relaxed);
}

// Only three render states are shadowed for draw classification. StateBlock Apply refreshes them from DXVK; all
// other setters/state remain direct forwards. These counters describe the frame currently being submitted.
uint32_t g_backbuffer_width = 0, g_backbuffer_height = 0;
int g_depth_test = D3DZB_TRUE, g_depth_write = 1, g_stencil_enabled = 0;
bool g_frame_has_world = false, g_late_loading_quad = false;
uint32_t g_frame_draws = 0, g_depth_write_draws = 0;
uint32_t g_minimap_matches = 0, g_minimap_vs = 0, g_minimap_up = 0, g_minimap_stencil = 0;
uint32_t g_minimap_restored = 0, g_minimap_failed = 0;
uint32_t g_minimap_logged_generation = 0, g_minimap_wait_logged_generation = 0, g_minimap_last_log = 0;
D3D9NdcBounds g_minimap_last_bounds;

void D3D9TrackRenderState(DWORD type, DWORD value) {
    if (type == D3DRS_ZENABLE) g_depth_test = int(value);
    else if (type == D3DRS_ZWRITEENABLE) g_depth_write = int(value);
    else if (type == D3DRS_STENCILENABLE) g_stencil_enabled = int(value);
}

void D3D9RefreshDeviceState(IDirect3DDevice9* dev) {
    DWORD depth_test = 0, depth_write = 0, stencil = 0;
    if (dev && SUCCEEDED(dev->GetRenderState(D3DRS_ZENABLE, &depth_test)) &&
        SUCCEEDED(dev->GetRenderState(D3DRS_ZWRITEENABLE, &depth_write)) &&
        SUCCEEDED(dev->GetRenderState(D3DRS_STENCILENABLE, &stencil))) {
        g_depth_test = int(depth_test);
        g_depth_write = int(depth_write);
        g_stencil_enabled = int(stencil);
    } else {
        // Unknown is conservative for world classification. Bink and an independently detected quad can still mask.
        g_depth_test = g_depth_write = g_stencil_enabled = -1;
    }
}

void D3D9TrackDraw(IDirect3DDevice9* dev) {
    if (g_depth_test < 0 || g_depth_write < 0 || g_stencil_enabled < 0) D3D9RefreshDeviceState(dev);
    ++g_frame_draws;
    if (g_depth_write != 0) ++g_depth_write_draws;
    if (D3D9DepthDrawMayBeWorld(g_depth_test, g_depth_write)) g_frame_has_world = true;
}

void D3D9ResetFrameCounters() {
    g_frame_has_world = false;
    g_late_loading_quad = false;
    g_frame_draws = g_depth_write_draws = 0;
    g_minimap_matches = g_minimap_vs = g_minimap_up = g_minimap_stencil = 0;
    g_minimap_restored = g_minimap_failed = 0;
}

void D3D9ResetFrameState(uint32_t width, uint32_t height) {
    EnsureMinimapConfigured();
    g_backbuffer_width = width;
    g_backbuffer_height = height;
    g_depth_test = D3DZB_TRUE;
    g_depth_write = 1;
    g_stencil_enabled = 0;
    D3D9ResetFrameCounters();
}

uint32_t D3D9PrimitiveVertices(D3DPRIMITIVETYPE type, uint32_t primitives) {
    uint64_t vertices = 0;
    switch (type) {
        case D3DPT_POINTLIST: vertices = primitives; break;
        case D3DPT_LINELIST: vertices = uint64_t(primitives) * 2; break;
        case D3DPT_LINESTRIP: vertices = primitives ? uint64_t(primitives) + 1 : 0; break;
        case D3DPT_TRIANGLELIST: vertices = uint64_t(primitives) * 3; break;
        case D3DPT_TRIANGLESTRIP:
        case D3DPT_TRIANGLEFAN: vertices = primitives ? uint64_t(primitives) + 2 : 0; break;
        default: return 0;
    }
    return vertices <= 0xffffffffull ? uint32_t(vertices) : 0;
}

struct D3D9DrawRequest {
    D3DPRIMITIVETYPE type = D3DPT_POINTLIST;
    uint32_t primitive_count = 0, vertex_count = 0;
    int32_t base_vertex = 0;
    uint32_t start_vertex = 0, min_vertex = 0, num_vertices = 0, start_index = 0;
    bool indexed = false, up = false;
    const void* vertices = nullptr;
    const void* indices = nullptr;
    D3DFORMAT index_format = D3DFMT_INDEX16;
    uint32_t stride = 0;
};

struct D3D9PositionLayout {
    bool valid = false, pretransformed = false;
    uint32_t stream = 0, offset = 0, components = 0, bytes = 0;
};

bool D3D9GetPositionLayout(IDirect3DDevice9* dev, D3D9PositionLayout& out) {
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (FAILED(dev->GetVertexDeclaration(&declaration)) || !declaration) return false;
    UINT count = 0;
    HRESULT hr = declaration->GetDeclaration(nullptr, &count);
    D3DVERTEXELEMENT9 elements[32];
    if (SUCCEEDED(hr) && count >= 2 && count <= 32) {
        hr = declaration->GetDeclaration(elements, &count);
    } else {
        hr = D3DERR_INVALIDCALL;
    }
    declaration->Release();
    if (FAILED(hr)) return false;

    for (UINT i = 0; i < count; ++i) {
        const D3DVERTEXELEMENT9& e = elements[i];
        if (e.Stream == 0xff && e.Type == D3DDECLTYPE_UNUSED) break;
        if (e.UsageIndex != 0 || (e.Usage != D3DDECLUSAGE_POSITION && e.Usage != D3DDECLUSAGE_POSITIONT)) continue;
        const bool pretransformed = e.Usage == D3DDECLUSAGE_POSITIONT;
        uint32_t components = 0;
        if (e.Type == D3DDECLTYPE_FLOAT2 && !pretransformed) components = 2;
        else if (e.Type == D3DDECLTYPE_FLOAT3 && !pretransformed) components = 3;
        else if (e.Type == D3DDECLTYPE_FLOAT4) components = 4;
        if (!components || out.valid) return false;  // Missing or ambiguous position stream.
        out.valid = true;
        out.pretransformed = pretransformed;
        out.stream = e.Stream;
        out.offset = e.Offset;
        out.components = components;
        out.bytes = components * sizeof(float);
    }
    return out.valid;
}

struct D3D9VertexView {
    const uint8_t* data = nullptr;  // First vertex in the draw's declared MinVertexIndex/StartVertex range.
    uint32_t stride = 0;
    IDirect3DVertexBuffer9* locked_buffer = nullptr;

    void Release() {
        if (locked_buffer) {
            locked_buffer->Unlock();
            locked_buffer->Release();
            locked_buffer = nullptr;
        }
    }
};

bool D3D9AcquireVertexView(IDirect3DDevice9* dev, const D3D9DrawRequest& request,
                           const D3D9PositionLayout& layout, uint32_t first, uint32_t count,
                           D3D9VertexView& view) {
    if (!count || !layout.bytes) return false;
    if (request.up) {
        if (layout.stream != 0 || !request.vertices || !request.stride) return false;
        view.stride = request.stride;
        const uint64_t bytes = (uint64_t(first) + count) * view.stride;
        if (view.stride < layout.offset + layout.bytes || view.stride > 1024 || bytes > 4u * 1024u * 1024u)
            return false;
        view.data = static_cast<const uint8_t*>(request.vertices) + size_t(first) * view.stride;
        return true;
    }

    IDirect3DVertexBuffer9* buffer = nullptr;
    UINT stream_offset = 0, stride = 0;
    if (FAILED(dev->GetStreamSource(layout.stream, &buffer, &stream_offset, &stride)) || !buffer) {
        if (buffer) buffer->Release();
        return false;
    }
    D3DVERTEXBUFFER_DESC desc {};
    const uint64_t lock_offset = uint64_t(stream_offset) + uint64_t(first) * stride;
    const uint64_t lock_size = uint64_t(count - 1) * stride + layout.offset + layout.bytes;
    if (!stride || stride < layout.offset + layout.bytes || stride > 1024 ||
        FAILED(buffer->GetDesc(&desc)) || lock_offset > desc.Size || lock_size > desc.Size - lock_offset ||
        lock_offset > 0xffffffffull || lock_size > 0xffffffffull) {
        buffer->Release();
        return false;
    }
    void* data = nullptr;
    const HRESULT lock_hr = buffer->Lock(UINT(lock_offset), UINT(lock_size), &data, D3DLOCK_READONLY);
    if (FAILED(lock_hr) || !data) {
        if (SUCCEEDED(lock_hr)) buffer->Unlock();
        buffer->Release();
        return false;
    }
    view.data = static_cast<const uint8_t*>(data);
    view.stride = stride;
    view.locked_buffer = buffer;
    return true;
}

bool D3D9ComputeBounds(const D3D9VertexView& view, const D3D9PositionLayout& layout, uint32_t count,
                       const D3DVIEWPORT9& viewport, uint32_t backbuffer_width, uint32_t backbuffer_height,
                       const float constants[16], D3D9TransformLayout transform, D3D9NdcBounds& bounds) {
    if (!backbuffer_width || !backbuffer_height || !viewport.Width || !viewport.Height) return false;
    bounds = {1.0e30f, 1.0e30f, -1.0e30f, -1.0e30f};
    for (uint32_t i = 0; i < count; ++i) {
        float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const uint8_t* source = view.data + size_t(i) * view.stride + layout.offset;
        std::memcpy(position, source, layout.bytes);
        float screen_x = 0, screen_y = 0;
        if (layout.pretransformed) {
            screen_x = position[0];
            screen_y = position[1];
        } else {
            const D3D9ClipPosition clip = D3D9TransformPosition(constants, transform, position[0], position[1],
                                                                 position[2], position[3]);
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) ||
                std::fabs(clip.w) < 1.0e-8f) return false;
            const float ndc_x = clip.x / clip.w, ndc_y = clip.y / clip.w;
            screen_x = float(viewport.X) + (ndc_x + 1.0f) * 0.5f * float(viewport.Width);
            screen_y = float(viewport.Y) + (1.0f - ndc_y) * 0.5f * float(viewport.Height);
        }
        const float x = screen_x * (2.0f / float(backbuffer_width)) - 1.0f;
        const float y = 1.0f - screen_y * (2.0f / float(backbuffer_height));
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        bounds.min_x = std::min(bounds.min_x, x);
        bounds.max_x = std::max(bounds.max_x, x);
        bounds.min_y = std::min(bounds.min_y, y);
        bounds.max_y = std::max(bounds.max_y, y);
    }
    return count != 0;  // The minimap matcher also accepts finite point/line bounds; loading requires a real quad.
}

struct D3D9DrawAnalysis {
    bool minimap = false, loading_quad = false, pretransformed = false;
    D3D9TransformLayout transform = D3D9TransformLayout::Rows;
    D3D9PositionLayout layout;
    D3D9NdcBounds bounds;
    D3DVIEWPORT9 viewport {};
    float constants[16] = {};
    uint32_t first = 0, count = 0;
};

bool D3D9AnalyzeDraw(IDirect3DDevice9* dev, const D3D9DrawRequest& request, bool want_minimap,
                     bool want_loading_quad, D3D9DrawAnalysis& out) {
    out.first = request.indexed ? request.min_vertex : request.start_vertex;
    out.count = request.indexed ? request.num_vertices : request.vertex_count;
    if (!out.count || out.count > 4096 || !D3D9GetPositionLayout(dev, out.layout) ||
        FAILED(dev->GetViewport(&out.viewport))) return false;
    out.pretransformed = out.layout.pretransformed;

    if (!out.pretransformed) {
        IDirect3DVertexShader9* shader = nullptr;
        if (FAILED(dev->GetVertexShader(&shader)) || !shader) {
            if (shader) shader->Release();
            return false;  // Pure3D's verified submission path uploads c0..c3 and uses a vertex shader.
        }
        shader->Release();
        if (FAILED(dev->GetVertexShaderConstantF(0, out.constants, 4))) return false;
    }

    D3D9VertexView view;
    if (!D3D9AcquireVertexView(dev, request, out.layout, out.first, out.count, view)) return false;
    int minimap_matches = 0, loading_matches = 0;
    D3D9NdcBounds selected_bounds {}, first_bounds {};
    D3D9TransformLayout selected_transform = D3D9TransformLayout::Rows;
    const int layouts = out.pretransformed ? 1 : 2;
    for (int i = 0; i < layouts; ++i) {
        const D3D9TransformLayout transform = i == 0 ? D3D9TransformLayout::Rows : D3D9TransformLayout::Columns;
        D3D9NdcBounds bounds;
        if (!D3D9ComputeBounds(view, out.layout, out.count, out.viewport, g_backbuffer_width, g_backbuffer_height,
                               out.constants, transform, bounds)) continue;
        if (want_minimap && D3D9MinimapDrawMatch(bounds, g_depth_test, g_depth_write, g_stencil_enabled != 0)) {
            ++minimap_matches;
            if (minimap_matches == 1) first_bounds = bounds;
            selected_bounds = bounds;
            selected_transform = transform;
        }
        if (want_loading_quad && D3D9LateLoadingQuad(bounds, g_backbuffer_width, g_backbuffer_height,
                                                     request.vertex_count, true)) ++loading_matches;
    }
    view.Release();

    // One matching layout identifies the transform. Identity/symmetric matrices can match both; Pure3D's verified
    // m4x4 convention is row-output, so use the first (row) result instead of leaving part of the minimap behind.
    out.loading_quad = loading_matches != 0;
    out.minimap = minimap_matches != 0;
    if (minimap_matches > 1) {
        selected_bounds = first_bounds;
        selected_transform = D3D9TransformLayout::Rows;
    }
    out.bounds = selected_bounds;
    out.transform = selected_transform;
    return out.loading_quad || out.minimap;
}

struct D3D9DrawPreparation {
    IDirect3DDevice9* device = nullptr;
    std::vector<uint8_t> up_copy;
    const void* up_data = nullptr;
    float original_constants[16] = {};
    bool restore_constants = false, matched = false, shader = false;

    void Begin(IDirect3DDevice9* dev, const D3D9DrawRequest& request, bool post_world) {
        D3D9TrackDraw(dev);
        const bool triangle = request.type == D3DPT_TRIANGLELIST || request.type == D3DPT_TRIANGLESTRIP ||
                              request.type == D3DPT_TRIANGLEFAN;
        const int offset_x = g_minimap_x.load(std::memory_order_relaxed);
        const int offset_y = g_minimap_y.load(std::memory_order_relaxed);
        const bool depth_eligible = D3D9MinimapDepthEligible(g_depth_test, g_depth_write);
        const bool want_minimap = post_world && depth_eligible && (offset_x != 0 || offset_y != 0);
        // The late-quad trigger is independent of the depth-draw count. Only 4/6-vertex triangle quads are read;
        // minimap relocation may inspect all small post-world HUD primitives, including lines/points and stencil.
        const bool want_loading = triangle && depth_eligible && g_frame_draws > 1 &&
                                  (request.vertex_count == 4 || request.vertex_count == 6);
        if ((!want_minimap && !want_loading) || !request.vertex_count || request.vertex_count > 4096) return;

        D3D9DrawAnalysis analysis;
        if (!D3D9AnalyzeDraw(dev, request, want_minimap, want_loading, analysis)) return;
        if (analysis.loading_quad) g_late_loading_quad = true;
        if (!analysis.minimap) return;

        float dx = 0, dy = 0;
        D3D9MinimapOffsetNdc(offset_x, offset_y, dx, dy);
        if (analysis.pretransformed) {
            if (!request.up) {
                static bool logged = false;
                if (!logged) {
                    Log("minimap: pretransformed buffer-backed draw left untouched (no safe UP substitution)");
                    logged = true;
                }
                return;
            }
            const uint64_t vertices = uint64_t(analysis.first) + analysis.count;
            const uint64_t bytes = vertices * request.stride;
            if (!bytes || bytes > 4u * 1024u * 1024u) return;
            up_copy.resize(size_t(bytes));
            std::memcpy(up_copy.data(), request.vertices, size_t(bytes));
            const float pixel_dx = dx * float(g_backbuffer_width) * 0.5f;
            const float pixel_dy = -dy * float(g_backbuffer_height) * 0.5f;
            D3D9TranslatePretransformedVertices(up_copy.data() + size_t(analysis.first) * request.stride,
                                                analysis.count, request.stride, analysis.layout.offset,
                                                pixel_dx, pixel_dy);
            up_data = up_copy.data();
            matched = true;
            ++g_minimap_up;
        } else {
            // Convert full-backbuffer NDC delta to this viewport's clip-space delta, then add delta*clip.w.
            const float local_dx = dx * float(g_backbuffer_width) / float(analysis.viewport.Width);
            const float local_dy = dy * float(g_backbuffer_height) / float(analysis.viewport.Height);
            float translated[16];
            std::memcpy(original_constants, analysis.constants, sizeof(original_constants));
            D3D9TranslateConstants(original_constants, translated, analysis.transform, local_dx, local_dy);
            HRESULT hr = dev->SetVertexShaderConstantF(0, translated, 4);
            if (FAILED(hr)) {
                dev->SetVertexShaderConstantF(0, original_constants, 4);
                static bool logged = false;
                if (!logged) {
                    Log("minimap: temporary vertex-constant upload failed -> %08x", uint32_t(hr));
                    logged = true;
                }
                return;
            }
            device = dev;
            restore_constants = shader = matched = true;
            ++g_minimap_vs;
        }
        ++g_minimap_matches;
        g_minimap_last_bounds = analysis.bounds;
        if (g_stencil_enabled) ++g_minimap_stencil;
    }

    void Finish(HRESULT draw_hr) {
        HRESULT restore_hr = D3D_OK;
        if (restore_constants) {
            restore_hr = device->SetVertexShaderConstantF(0, original_constants, 4);
            restore_constants = false;
            if (SUCCEEDED(restore_hr)) ++g_minimap_restored;
        }
        if (matched && (FAILED(draw_hr) || FAILED(restore_hr))) ++g_minimap_failed;
    }
};

HRESULT D3D9SubmitDraw(IDirect3DDevice9* dev, const D3D9DrawRequest& request) {
    const bool post_world = g_frame_has_world;
    D3D9DrawPreparation preparation;
    preparation.Begin(dev, request, post_world);
    const void* up_vertices = preparation.up_data ? preparation.up_data : request.vertices;
    HRESULT hr = D3DERR_INVALIDCALL;
    if (request.up && request.indexed) {
        hr = dev->DrawIndexedPrimitiveUP(request.type, request.min_vertex, request.num_vertices,
                                         request.primitive_count, request.indices, request.index_format,
                                         up_vertices, request.stride);
    } else if (request.up) {
        hr = dev->DrawPrimitiveUP(request.type, request.primitive_count, up_vertices, request.stride);
    } else if (request.indexed) {
        hr = dev->DrawIndexedPrimitive(request.type, request.base_vertex, request.min_vertex, request.num_vertices,
                                       request.start_index, request.primitive_count);
    } else {
        hr = dev->DrawPrimitive(request.type, request.start_vertex, request.primitive_count);
    }
    preparation.Finish(hr);  // Before returning to the guest: no translated constant survives the draw.
    return hr;
}

void D3D9FrameDiagnostics() {
    const int x = g_minimap_x.load(std::memory_order_relaxed), y = g_minimap_y.load(std::memory_order_relaxed);
    if (!x && !y) return;
    const uint32_t generation = g_minimap_generation.load(std::memory_order_relaxed);
    if (g_minimap_matches && (generation != g_minimap_logged_generation ||
                              g_present_count - g_minimap_last_log >= 120)) {
        Log("minimap: D3D9 submission offset %d,%d matched %u draws (VS %u/restored %u, UP copies %u, stencil %u), failed %u; last bounds [%.3f,%.3f]x[%.3f,%.3f]",
            x, y, g_minimap_matches, g_minimap_vs, g_minimap_restored, g_minimap_up, g_minimap_stencil,
            g_minimap_failed, g_minimap_last_bounds.min_x, g_minimap_last_bounds.max_x,
            g_minimap_last_bounds.min_y, g_minimap_last_bounds.max_y);
        g_minimap_logged_generation = generation;
        g_minimap_last_log = g_present_count;
    } else if (!g_minimap_matches && generation != g_minimap_wait_logged_generation) {
        Log("minimap: waiting for matching post-world D3D9 HUD bounds; offset %d,%d", x, y);
        g_minimap_wait_logged_generation = generation;
    }
}

void MaskFrontendPillarboxes(IDirect3DDevice9* dev) {
    const uint32_t depth_draws = g_depth_write_draws;
    const bool late_quad = g_late_loading_quad;
    D3D9FrameDiagnostics();
    D3D9ResetFrameCounters();

    D3D9Pillarboxes bars;
    if (!D3D9WidePillarboxes(g_backbuffer_width, g_backbuffer_height, bars)) return;
    bool movie = false, movie_queried = false;
    bool mask = D3D9LoadingMaskRequired(false, depth_draws, late_quad);
    if (!mask) {
        // If another trigger already fired, a registry lock cannot change the result and is unnecessary.
        movie = BinkHasOpenMovie();
        movie_queried = true;
        mask = D3D9LoadingMaskRequired(movie, depth_draws, late_quad);
    }
    if (!mask) return;

    const uint32_t width = g_backbuffer_width, height = g_backbuffer_height;
    D3DRECT rects[2];
    DWORD count = 0;
    if (bars.left) rects[count++] = {0, 0, LONG(bars.left), LONG(height)};
    if (bars.right < width) rects[count++] = {LONG(bars.right), 0, LONG(width), LONG(height)};
    if (!count) return;

    // Clear immediately before Present. DXVK clips Clear to viewport/scissor state, so cover and restore both.
    D3DVIEWPORT9 old_viewport {};
    DWORD old_scissor = FALSE;
    if (FAILED(dev->GetViewport(&old_viewport)) ||
        FAILED(dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &old_scissor))) return;
    const D3DVIEWPORT9 full {0, 0, width, height, 0.0f, 1.0f};
    HRESULT hr = dev->SetViewport(&full);
    if (SUCCEEDED(hr)) hr = dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    if (SUCCEEDED(hr)) hr = dev->Clear(count, rects, D3DCLEAR_TARGET, 0xff000000u, 0.0f, 0);
    dev->SetViewport(&old_viewport);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, old_scissor);

    static uint32_t last_log = 0;
    if (g_present_count - last_log >= 120 || last_log == 0) {
        Log("loading: opaque #000000 pillarboxes %ux%u [0,%u) [%u,%u), movie=%s depth-draws=%u late-quad=%d -> %08x",
            width, height, bars.left, bars.right, width, movie_queried ? (movie ? "1" : "0") : "not-queried",
            depth_draws, late_quad ? 1 : 0, uint32_t(hr));
        last_log = g_present_count;
    }
}

// ---- Direct3DCreate9 ----
void x_Direct3DCreate9(Ctx* c) {
    IDirect3D9* d3d = Direct3DCreate9(Arg(c, 0));
    Log("Direct3DCreate9(%u) -> %p", Arg(c, 0), static_cast<void*>(d3d));
    // GPUs without BC formats (Mali, PowerVR) get DXT textures decoded on the CPU; decode_dxt=1/0 forces it.
    if (d3d) {
        bool bc = SUCCEEDED(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE,
                                                   D3DFMT_DXT5));
        g_decode_dxt = SettingInt("decode_dxt", bc ? 0 : 1) != 0;
        bool a1r5 = SUCCEEDED(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE,
                                                     D3DFMT_A1R5G5B5));
#ifdef __ANDROID__
        g_decode_16bit = a1r5 && SettingInt("decode_16bit", 1) != 0;
#else
        g_decode_16bit = a1r5 && SettingInt("decode_16bit", 0) != 0;
#endif
        Log("GPU BC (DXT) textures: %s, decoding %s%s", bc ? "supported" : "not supported", g_decode_dxt ? "on" : "off",
            g_decode_dxt && g_decode_16bit ? " (DXT1 as 16-bit)" : "");
    }
    Return(c, Wrap(d3d), 1);
}

}  // namespace

// ---------------- manual methods ----------------
#define M(iface, method) void Manual_##iface##_##method(Ctx* c)

M(IDirect3DDevice9, DrawPrimitive) {
    D3D9DrawRequest request;
    request.type = D3DPRIMITIVETYPE(Arg(c, 1));
    request.start_vertex = Arg(c, 2);
    request.primitive_count = Arg(c, 3);
    request.vertex_count = D3D9PrimitiveVertices(request.type, request.primitive_count);
    Return(c, uint32_t(D3D9SubmitDraw(Host<IDirect3DDevice9>(Arg(c, 0)), request)), 4);
}
M(IDirect3DDevice9, DrawIndexedPrimitive) {
    D3D9DrawRequest request;
    request.type = D3DPRIMITIVETYPE(Arg(c, 1));
    request.base_vertex = int32_t(Arg(c, 2));
    request.min_vertex = Arg(c, 3);
    request.num_vertices = Arg(c, 4);
    request.start_index = Arg(c, 5);
    request.primitive_count = Arg(c, 6);
    request.vertex_count = D3D9PrimitiveVertices(request.type, request.primitive_count);
    request.indexed = true;
    Return(c, uint32_t(D3D9SubmitDraw(Host<IDirect3DDevice9>(Arg(c, 0)), request)), 7);
}
M(IDirect3DDevice9, DrawPrimitiveUP) {
    D3D9DrawRequest request;
    request.type = D3DPRIMITIVETYPE(Arg(c, 1));
    request.primitive_count = Arg(c, 2);
    request.vertex_count = D3D9PrimitiveVertices(request.type, request.primitive_count);
    request.vertices = Ptr<void>(Arg(c, 3));
    request.stride = Arg(c, 4);
    request.up = true;
    Return(c, uint32_t(D3D9SubmitDraw(Host<IDirect3DDevice9>(Arg(c, 0)), request)), 5);
}
M(IDirect3DDevice9, DrawIndexedPrimitiveUP) {
    D3D9DrawRequest request;
    request.type = D3DPRIMITIVETYPE(Arg(c, 1));
    request.min_vertex = Arg(c, 2);
    request.num_vertices = Arg(c, 3);
    request.primitive_count = Arg(c, 4);
    request.indices = Ptr<void>(Arg(c, 5));
    request.index_format = D3DFORMAT(Arg(c, 6));
    request.vertices = Ptr<void>(Arg(c, 7));
    request.stride = Arg(c, 8);
    request.vertex_count = D3D9PrimitiveVertices(request.type, request.primitive_count);
    request.indexed = request.up = true;
    Return(c, uint32_t(D3D9SubmitDraw(Host<IDirect3DDevice9>(Arg(c, 0)), request)), 9);
}

M(IDirect3D9, GetAdapterMonitor) { Return(c, 1, 2); }
M(IDirect3D9, GetAdapterDisplayMode) {
    // (this, Adapter, pMode)
    if (!OverrideModes()) {
        Return(c, uint32_t(Host<IDirect3D9>(Arg(c, 0))->GetAdapterDisplayMode(Arg(c, 1), Ptr<D3DDISPLAYMODE>(Arg(c, 2)))),
               3);
        return;
    }
    WriteMode(Arg(c, 2), Modes()[0]);
    Return(c, D3D_OK, 3);
}
M(IDirect3D9, GetAdapterModeCount) {
    // (this, Adapter, Format)
    if (!OverrideModes()) {
        Return(c, Host<IDirect3D9>(Arg(c, 0))->GetAdapterModeCount(Arg(c, 1), D3DFORMAT(Arg(c, 2))), 3);
        return;
    }
    D3DFORMAT f = D3DFORMAT(Arg(c, 2));
    Return(c, f == D3DFMT_X8R8G8B8 || f == D3DFMT_A8R8G8B8 ? uint32_t(Modes().size()) : 0u, 3);
}
M(IDirect3D9, EnumAdapterModes) {
    // (this, Adapter, Format, Mode, pMode)
    if (!OverrideModes()) {
        Return(c, uint32_t(Host<IDirect3D9>(Arg(c, 0))->EnumAdapterModes(Arg(c, 1), D3DFORMAT(Arg(c, 2)), Arg(c, 3),
                                                                        Ptr<D3DDISPLAYMODE>(Arg(c, 4)))),
               5);
        return;
    }
    auto modes = Modes();
    if (Arg(c, 3) >= modes.size()) {
        Return(c, uint32_t(D3DERR_INVALIDCALL), 5);
        return;
    }
    D3DDISPLAYMODE m = modes[Arg(c, 3)];
    m.Format = D3DFORMAT(Arg(c, 2));
    WriteMode(Arg(c, 4), m);
    Return(c, D3D_OK, 5);
}
M(IDirect3DDevice9, GetDisplayMode) {
    // (this, iSwapChain, pMode)
    if (!OverrideModes()) {
        Return(c, uint32_t(Host<IDirect3DDevice9>(Arg(c, 0))->GetDisplayMode(Arg(c, 1),
                                                                            Ptr<D3DDISPLAYMODE>(Arg(c, 2)))),
               3);
        return;
    }
    WriteMode(Arg(c, 2), Modes()[0]);
    Return(c, D3D_OK, 3);
}

bool WaitForWindow(int seconds);  // below, with the surface state

M(IDirect3D9, CreateDevice) {
    D3DPRESENT_PARAMETERS pp = ToHost(Arg(c, 5));
    IDirect3DDevice9* dev = nullptr;
    // The Vulkan surface needs the window on screen: if the player switched to another app during startup, the
    // device would fail with a lost surface. Wait for the window, and retry a failure that happened while it was gone.
    HRESULT hr = D3DERR_INVALIDCALL;
    for (int attempt = 0; attempt < 5; ++attempt) {
        WaitForWindow(600);
        D3DPRESENT_PARAMETERS try_pp = pp;
        hr = Host<IDirect3D9>(Arg(c, 0))->CreateDevice(Arg(c, 1), D3DDEVTYPE(Arg(c, 2)), HostHwnd(Arg(c, 3)),
                                                     Arg(c, 4), &try_pp, &dev);
        if (SUCCEEDED(hr)) {
            pp = try_pp;
            break;
        }
        if (!WaitForWindow(0)) {
            Log("CreateDevice -> %08x while the window was away, retrying", uint32_t(hr));
            continue;
        }
        break;  // failed with the window on screen: a real driver problem
    }
    Log("CreateDevice %ux%u fmt %u windowed %u msaa %u/%u interval %u -> %08x", pp.BackBufferWidth,
        pp.BackBufferHeight, pp.BackBufferFormat, pp.Windowed, pp.MultiSampleType, pp.MultiSampleQuality,
        pp.PresentationInterval, uint32_t(hr));
    // The engine does not check: without a device it crashes on the next call. The DXVK log says why it failed.
    if (FAILED(hr)) {
        StartupFailure("The graphics driver could not create the D3D9 device (DXVK on Vulkan). "
                       "See app_process64_d3d9.log for the reason.");
    }
    if (SUCCEEDED(hr)) D3D9ResetFrameState(pp.BackBufferWidth, pp.BackBufferHeight);
    if (OverrideModes()) SetClientSize(int(pp.BackBufferWidth), int(pp.BackBufferHeight));
    ToGuest(pp, Arg(c, 5));
    W32(Arg(c, 6), Wrap(dev));
    Return(c, uint32_t(hr), 7);
}
M(IDirect3DDevice9, GetCreationParameters) {
    D3DDEVICE_CREATION_PARAMETERS p;
    HRESULT hr = Host<IDirect3DDevice9>(Arg(c, 0))->GetCreationParameters(&p);
    uint32_t g = Arg(c, 1);
    W32(g, p.AdapterOrdinal);
    W32(g + 4, p.DeviceType);
    W32(g + 8, HostWindow());
    W32(g + 12, p.BehaviorFlags);
    Return(c, uint32_t(hr), 2);
}
M(IDirect3DDevice9, SetCursorProperties) {
    Return(c, uint32_t(Host<IDirect3DDevice9>(Arg(c, 0))->SetCursorProperties(
                  Arg(c, 1), Arg(c, 2), Unwrap<IDirect3DSurface9>(Arg(c, 3)))),
           4);
}
M(IDirect3DDevice9, CreateAdditionalSwapChain) {
    D3DPRESENT_PARAMETERS pp = ToHost(Arg(c, 1));
    IDirect3DSwapChain9* sc = nullptr;
    HRESULT hr = Host<IDirect3DDevice9>(Arg(c, 0))->CreateAdditionalSwapChain(&pp, &sc);
    ToGuest(pp, Arg(c, 1));
    W32(Arg(c, 2), SUCCEEDED(hr) ? Wrap(sc) : 0);
    Return(c, uint32_t(hr), 3);
}
M(IDirect3DDevice9, Reset) {
    D3DPRESENT_PARAMETERS pp = ToHost(Arg(c, 1));
    DrainGraves(true);
    HRESULT hr = Host<IDirect3DDevice9>(Arg(c, 0))->Reset(&pp);
    Log("Reset %ux%u msaa %u/%u interval %u -> %08x", pp.BackBufferWidth, pp.BackBufferHeight, pp.MultiSampleType,
        pp.MultiSampleQuality, pp.PresentationInterval, uint32_t(hr));
    if (SUCCEEDED(hr)) {
        D3D9ResetFrameState(pp.BackBufferWidth, pp.BackBufferHeight);
        if (OverrideModes()) SetClientSize(int(pp.BackBufferWidth), int(pp.BackBufferHeight));
    }
    ToGuest(pp, Arg(c, 1));
    Return(c, uint32_t(hr), 2);
}

M(IDirect3DStateBlock9, Apply) {
    auto* block = Host<IDirect3DStateBlock9>(Arg(c, 0));
    const HRESULT hr = block->Apply();
    IDirect3DDevice9* dev = nullptr;
    if (SUCCEEDED(block->GetDevice(&dev)) && dev) {
        D3D9RefreshDeviceState(dev);  // Apply never suppresses or invalidates the loading-mask triggers.
        dev->Release();
    } else {
        g_depth_test = g_depth_write = g_stencil_enabled = -1;
    }
    Return(c, uint32_t(hr), 1);
}

// Setting shot=<frame>[,<frame>...] (PROTORE_SHOT): the back buffer of those presents is saved as shot_<frame>.ppm in
// the working directory (debugging without a desktop screenshot tool; on Android only the game's own frame).
void MaybeScreenshot(IDirect3DDevice9* dev) {
    static const std::string spec_setting = Setting("shot", "");  // protore.ini `shot = ...` works on Android
    static const char* spec = spec_setting.empty() ? nullptr : spec_setting.c_str();
    static uint32_t frame = 0;
    ++frame;
    // On demand: a file named shot_now in the working directory (adb shell touch ...) asks for one frame.
    bool now = (frame & 15) == 0 && access("shot_now", F_OK) == 0;
    if (now) unlink("shot_now");
    if (!spec && !now) return;
    bool want = now;
    for (const char* p = spec ? spec : ""; *p;) {
        char* end;
        if (strtoul(p, &end, 10) == frame) want = true;
        p = *end ? end + 1 : end;
    }
    if (!want) return;
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    IDirect3DSurface9* sys = nullptr;
    D3DLOCKED_RECT lr;
    if ((d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8R8G8B8) &&
        SUCCEEDED(dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
        SUCCEEDED(dev->GetRenderTargetData(bb, sys)) && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        std::string name = now ? std::string("shot_now.ppm") : "shot_" + std::to_string(frame) + ".ppm";
        if (FILE* f = fopen(name.c_str(), "wb")) {
            fprintf(f, "P6\n%u %u\n255\n", d.Width, d.Height);
            std::vector<uint8_t> row(3 * d.Width);
            for (uint32_t y = 0; y < d.Height; ++y) {
                const uint8_t* src = static_cast<const uint8_t*>(lr.pBits) + size_t(y) * size_t(lr.Pitch);
                for (uint32_t x = 0; x < d.Width; ++x) {
                    row[3 * x] = src[4 * x + 2];
                    row[3 * x + 1] = src[4 * x + 1];
                    row[3 * x + 2] = src[4 * x];
                }
                fwrite(row.data(), 1, row.size(), f);
            }
            fclose(f);
            Log("screenshot %s (%ux%u)", name.c_str(), d.Width, d.Height);
        }
        sys->UnlockRect();
    } else {
        Log("screenshot of frame %u failed (format %d)", frame, int(d.Format));
    }
    if (sys) sys->Release();
    bb->Release();
}

// The window can go away under the render thread (Android destroys the surface when the app leaves the screen):
// ProtoSurface.surfaceDestroyed waits for a present in flight and holds presents back until the surface is back,
// as presenting or recreating the swap chain on a destroyed window crashes in the Vulkan loader. A skipped present
// just drops the frame.
std::mutex g_present_mutex;
bool g_surface_gone = false;  // under g_present_mutex

template <typename F>
HRESULT GuardedPresent(F present) {
    std::unique_lock<std::mutex> lock(g_present_mutex);
    if (g_surface_gone) {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        return D3D_OK;
    }
    return present();
}

#ifdef __ANDROID__
// Waits up to `seconds` for the game's window (surface) to be on screen; true if it is.
bool WaitForWindow(int seconds) {
    for (int i = 0; i <= seconds * 10; ++i) {
        {
            std::lock_guard<std::mutex> lock(g_present_mutex);
            if (!g_surface_gone) return true;
        }
        if (i < seconds * 10) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

extern "C" JNIEXPORT void JNICALL Java_com_protore_game_ProtoSurface_nativeSurfaceGone(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_present_mutex);
    g_surface_gone = true;
}
extern "C" JNIEXPORT void JNICALL Java_com_protore_game_ProtoSurface_nativeSurfaceReady(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(g_present_mutex);
    g_surface_gone = false;
}
extern "C" JNIEXPORT void JNICALL Java_com_protore_game_TouchControls_nativeSetMinimapPosition(
    JNIEnv*, jclass, jint x, jint y) {
    native::SetPosition(int(x), int(y));
}
#endif

M(IDirect3DDevice9, Present) {
    ++g_present_count;
    if (!g_graves.empty()) DrainGraves(false);
    auto* dev = Host<IDirect3DDevice9>(Arg(c, 0));
    MaskFrontendPillarboxes(dev);
    MaybeScreenshot(dev);
    HRESULT hr =
        GuardedPresent([&] { return dev->Present(Ptr<RECT>(Arg(c, 1)), Ptr<RECT>(Arg(c, 2)), nullptr, nullptr); });
    Return(c, uint32_t(hr), 5);
}
// Clamp expensive filtering at the bridge as well as in DXVK. At 1x, anisotropic filtering is exactly linear
// filtering; using LINEAR also avoids DXVK enabling a sampler's anisotropy feature with maxAnisotropy = 1.
int MaxAnisotropy() {
    static const int value = [] {
#ifdef __ANDROID__
        const int configured = std::clamp(SettingInt("max_anisotropy", 1), 1, 16);
#else
        const int configured = std::clamp(SettingInt("max_anisotropy", 16), 1, 16);
#endif
        Log("samplers: effective DXVK forwarding maximum %dx%s", configured,
            configured == 1 ? "; MIN/MAG anisotropic -> LINEAR" : "");
        return configured;
    }();
    return value;
}

M(IDirect3DDevice9, SetSamplerState) {
    const DWORD stage = Arg(c, 1), type = Arg(c, 2), requested = Arg(c, 3);
    // This is the effective value forwarded to DXVK, not a pre-clamp request statistic. No cache or atomic counter
    // sits in the hot path; Reset and StateBlock Apply therefore cannot leave stale shadow state behind.
    const DWORD effective = D3D9EffectiveSamplerValue(type, requested, MaxAnisotropy());
    HRESULT hr = Host<IDirect3DDevice9>(Arg(c, 0))->SetSamplerState(
        stage, D3DSAMPLERSTATETYPE(type), effective);
    Return(c, uint32_t(hr), 4);
}
// The engine's worker threads poll this before every buffer lock; DXVK answers D3D_OK for a windowed device but
// takes the device lock first, so the polls queue up behind the render thread's draws and presents.
M(IDirect3DDevice9, TestCooperativeLevel) { Return(c, uint32_t(D3D_OK), 1); }
M(IDirect3DSwapChain9, Present) {
    auto* sc = Host<IDirect3DSwapChain9>(Arg(c, 0));
    HRESULT hr = GuardedPresent([&] { return sc->Present(Ptr<RECT>(Arg(c, 1)), Ptr<RECT>(Arg(c, 2)), nullptr, nullptr,
                                                         Arg(c, 5)); });
    Return(c, uint32_t(hr), 6);
}
M(IDirect3DSwapChain9, GetPresentParameters) {
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr = Host<IDirect3DSwapChain9>(Arg(c, 0))->GetPresentParameters(&pp);
    ToGuest(pp, Arg(c, 1));
    Return(c, uint32_t(hr), 2);
}

// GetContainer(riid, void**): the container is a texture or the device.
template <typename T>
void GetContainerImpl(Ctx* c) {
    const GUID& riid = *Ptr<GUID>(Arg(c, 1));
    void* p = nullptr;
    HRESULT hr = Host<T>(Arg(c, 0))->GetContainer(riid, &p);
    uint32_t g = 0;
    if (SUCCEEDED(hr)) {
        if (riid == __uuidof(IDirect3DTexture9)) g = Wrap(static_cast<IDirect3DTexture9*>(p));
        else if (riid == __uuidof(IDirect3DCubeTexture9)) g = Wrap(static_cast<IDirect3DCubeTexture9*>(p));
        else if (riid == __uuidof(IDirect3DVolumeTexture9)) g = Wrap(static_cast<IDirect3DVolumeTexture9*>(p));
        else if (riid == __uuidof(IDirect3DBaseTexture9)) g = Wrap(static_cast<IDirect3DBaseTexture9*>(p));
        else if (riid == __uuidof(IDirect3DDevice9)) g = Wrap(static_cast<IDirect3DDevice9*>(p));
        else Log("d3d9: GetContainer for an unsupported interface");
    }
    W32(Arg(c, 2), g);
    Return(c, uint32_t(hr), 3);
}
M(IDirect3DSurface9, GetContainer) { GetContainerImpl<IDirect3DSurface9>(c); }
M(IDirect3DVolume9, GetContainer) { GetContainerImpl<IDirect3DVolume9>(c); }

M(IDirect3DSurface9, LockRect) {
    auto* s = Host<IDirect3DSurface9>(Arg(c, 0));
    const RECT* rect = Ptr<RECT>(Arg(c, 2));
    D3DLOCKED_RECT lr;
    HRESULT hr = s->LockRect(&lr, rect, Arg(c, 3));
    D3DSURFACE_DESC desc;
    s->GetDesc(&desc);
    LockRectCommon(c, s, 0, hr, lr, desc, rect, Arg(c, 3), Arg(c, 1), 4);
}
M(IDirect3DSurface9, UnlockRect) {
    auto* s = Host<IDirect3DSurface9>(Arg(c, 0));
    ShadowUnlock(s, 0);
    Return(c, uint32_t(s->UnlockRect()), 1);
}
M(IDirect3DSurface9, GetDC) { Return(c, uint32_t(D3DERR_INVALIDCALL), 2); }
M(IDirect3DSurface9, ReleaseDC) { Return(c, uint32_t(D3DERR_INVALIDCALL), 2); }

M(IDirect3DDevice9, CreateTexture) {
    // (this, Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle)
    UINT w = Arg(c, 1), h = Arg(c, 2), levels = Arg(c, 3);
    DWORD usage = Arg(c, 4);
    D3DFORMAT format = D3DFORMAT(Arg(c, 5));
    TexAdapt a = PlanTexture(w, h, levels, usage, format);
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = Host<IDirect3DDevice9>(Arg(c, 0))->CreateTexture(
        w >> a.skip, h >> a.skip, levels ? levels - a.skip : 0, usage, a.decode ? a.decode_format : format,
        D3DPOOL(Arg(c, 6)), &tex, nullptr);
    if (SUCCEEDED(hr) && (a.skip || a.decode)) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        g_adapt[tex] = a;
    }
    W32(Arg(c, 7), SUCCEEDED(hr) ? Wrap(tex) : 0);
    Return(c, uint32_t(hr), 9);
}
M(IDirect3DDevice9, CreateCubeTexture) {
    // (this, EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle)
    UINT edge = Arg(c, 1), levels = Arg(c, 2);
    DWORD usage = Arg(c, 3);
    D3DFORMAT format = D3DFORMAT(Arg(c, 4));
    TexAdapt a = PlanTexture(edge, edge, levels, usage, format);
    IDirect3DCubeTexture9* tex = nullptr;
    HRESULT hr = Host<IDirect3DDevice9>(Arg(c, 0))->CreateCubeTexture(
        edge >> a.skip, levels ? levels - a.skip : 0, usage, a.decode ? a.decode_format : format, D3DPOOL(Arg(c, 5)),
        &tex, nullptr);
    if (SUCCEEDED(hr) && (a.skip || a.decode)) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        g_adapt[tex] = a;
    }
    W32(Arg(c, 6), SUCCEEDED(hr) ? Wrap(tex) : 0);
    Return(c, uint32_t(hr), 8);
}
M(IDirect3D9, CheckDeviceFormat) {
    // (this, Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat): DXT is always available when decoded.
    D3DFORMAT check = D3DFORMAT(Arg(c, 6));
    HRESULT hr = Host<IDirect3D9>(Arg(c, 0))->CheckDeviceFormat(Arg(c, 1), D3DDEVTYPE(Arg(c, 2)),
                                                              D3DFORMAT(Arg(c, 3)), Arg(c, 4),
                                                              D3DRESOURCETYPE(Arg(c, 5)), check);
    if (FAILED(hr) && g_decode_dxt && IsDxt(check) && !(Arg(c, 4) & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))) {
        hr = D3D_OK;
    }
    Return(c, uint32_t(hr), 7);
}
M(IDirect3DTexture9, GetLevelCount) {
    auto* t = Host<IDirect3DTexture9>(Arg(c, 0));
    const TexAdapt* a = AdaptOf(t);
    Return(c, t->GetLevelCount() + (a ? a->skip : 0), 1);
}
M(IDirect3DCubeTexture9, GetLevelCount) {
    auto* t = Host<IDirect3DCubeTexture9>(Arg(c, 0));
    const TexAdapt* a = AdaptOf(t);
    Return(c, t->GetLevelCount() + (a ? a->skip : 0), 1);
}
M(IDirect3DTexture9, GetLevelDesc) {
    auto* t = Host<IDirect3DTexture9>(Arg(c, 0));
    UINT level = Arg(c, 1);
    const TexAdapt* a = AdaptOf(t);
    D3DSURFACE_DESC* d = Ptr<D3DSURFACE_DESC>(Arg(c, 2));
    HRESULT hr = t->GetLevelDesc(a && level >= a->skip ? level - a->skip : a ? 0 : level, d);
    if (SUCCEEDED(hr) && a) AdaptDesc(*a, level, d);
    Return(c, uint32_t(hr), 3);
}
M(IDirect3DCubeTexture9, GetLevelDesc) {
    auto* t = Host<IDirect3DCubeTexture9>(Arg(c, 0));
    UINT level = Arg(c, 1);
    const TexAdapt* a = AdaptOf(t);
    D3DSURFACE_DESC* d = Ptr<D3DSURFACE_DESC>(Arg(c, 2));
    HRESULT hr = t->GetLevelDesc(a && level >= a->skip ? level - a->skip : a ? 0 : level, d);
    if (SUCCEEDED(hr) && a) AdaptDesc(*a, level, d);
    Return(c, uint32_t(hr), 3);
}
// Surfaces of adapted textures: the nearest host level (dropped levels map to the top host level).
M(IDirect3DTexture9, GetSurfaceLevel) {
    auto* t = Host<IDirect3DTexture9>(Arg(c, 0));
    const TexAdapt* a = AdaptOf(t);
    UINT level = Arg(c, 1);
    IDirect3DSurface9* surf = nullptr;
    HRESULT hr = t->GetSurfaceLevel(a ? (level >= a->skip ? level - a->skip : 0) : level, &surf);
    W32(Arg(c, 2), SUCCEEDED(hr) ? Wrap(surf) : 0);
    Return(c, uint32_t(hr), 3);
}
M(IDirect3DCubeTexture9, GetCubeMapSurface) {
    auto* t = Host<IDirect3DCubeTexture9>(Arg(c, 0));
    const TexAdapt* a = AdaptOf(t);
    UINT level = Arg(c, 2);
    IDirect3DSurface9* surf = nullptr;
    HRESULT hr = t->GetCubeMapSurface(D3DCUBEMAP_FACES(Arg(c, 1)), a ? (level >= a->skip ? level - a->skip : 0) : level,
                                      &surf);
    W32(Arg(c, 3), SUCCEEDED(hr) ? Wrap(surf) : 0);
    Return(c, uint32_t(hr), 4);
}
M(IDirect3DTexture9, LockRect) {
    // (this, Level, pLockedRect, pRect, Flags)
    auto* t = Host<IDirect3DTexture9>(Arg(c, 0));
    uint32_t level = Arg(c, 1);
    const RECT* rect = Ptr<RECT>(Arg(c, 3));
    if (const TexAdapt* a = AdaptOf(t)) {
        TexAdapt copy = *a;
        HRESULT hr = AdaptedLockRect(t, level, copy, level, rect, Arg(c, 4), Arg(c, 2),
                                     [&](UINT host_level, D3DLOCKED_RECT* lr) {
                                         return t->LockRect(host_level, lr, rect, Arg(c, 4));
                                     });
        Return(c, uint32_t(hr), 5);
        return;
    }
    D3DLOCKED_RECT lr;
    HRESULT hr = t->LockRect(level, &lr, rect, Arg(c, 4));
    D3DSURFACE_DESC desc;
    t->GetLevelDesc(level, &desc);
    LockRectCommon(c, t, level, hr, lr, desc, rect, Arg(c, 4), Arg(c, 2), 5);
}
M(IDirect3DTexture9, UnlockRect) {
    auto* t = Host<IDirect3DTexture9>(Arg(c, 0));
    uint32_t level = Arg(c, 1);
    const TexAdapt* a = AdaptOf(t);
    bool host_locked = true;
    if (a && AdaptedUnlock(t, level, &host_locked)) {
        Return(c, host_locked ? uint32_t(t->UnlockRect(level - a->skip)) : 0u, 2);
        return;
    }
    ShadowUnlock(t, level);
    Return(c, uint32_t(t->UnlockRect(a ? level - a->skip : level)), 2);
}
M(IDirect3DCubeTexture9, LockRect) {
    // (this, FaceType, Level, pLockedRect, pRect, Flags)
    auto* t = Host<IDirect3DCubeTexture9>(Arg(c, 0));
    uint32_t face = Arg(c, 1), level = Arg(c, 2);
    const RECT* rect = Ptr<RECT>(Arg(c, 4));
    if (const TexAdapt* a = AdaptOf(t)) {
        TexAdapt copy = *a;
        HRESULT hr = AdaptedLockRect(t, face * 64 + level, copy, level, rect, Arg(c, 5), Arg(c, 3),
                                     [&](UINT host_level, D3DLOCKED_RECT* lr) {
                                         return t->LockRect(D3DCUBEMAP_FACES(face), host_level, lr, rect, Arg(c, 5));
                                     });
        Return(c, uint32_t(hr), 6);
        return;
    }
    D3DLOCKED_RECT lr;
    HRESULT hr = t->LockRect(D3DCUBEMAP_FACES(face), level, &lr, rect, Arg(c, 5));
    D3DSURFACE_DESC desc;
    t->GetLevelDesc(level, &desc);
    LockRectCommon(c, t, face * 64 + level, hr, lr, desc, rect, Arg(c, 5), Arg(c, 3), 6);
}
M(IDirect3DCubeTexture9, UnlockRect) {
    auto* t = Host<IDirect3DCubeTexture9>(Arg(c, 0));
    uint32_t face = Arg(c, 1), level = Arg(c, 2);
    const TexAdapt* a = AdaptOf(t);
    bool host_locked = true;
    if (a && AdaptedUnlock(t, face * 64 + level, &host_locked)) {
        Return(c, host_locked ? uint32_t(t->UnlockRect(D3DCUBEMAP_FACES(face), level - a->skip)) : 0u, 3);
        return;
    }
    ShadowUnlock(t, face * 64 + level);
    Return(c, uint32_t(t->UnlockRect(D3DCUBEMAP_FACES(face), a ? level - a->skip : level)), 3);
}

// D3DLOCKED_BOX in guest memory: {INT RowPitch; INT SlicePitch; void* pBits} (12 bytes).
void LockBoxCommon(Ctx* c, void* object, uint32_t sub, HRESULT hr, const D3DLOCKED_BOX& lb, uint32_t depth,
                   DWORD flags, uint32_t out, int args) {
    if (SUCCEEDED(hr)) {
        W32(out, uint32_t(lb.RowPitch));
        W32(out + 4, uint32_t(lb.SlicePitch));
        W32(out + 8, ShadowLock(object, sub, lb.pBits, uint32_t(lb.SlicePitch) * depth, flags));
    }
    Return(c, uint32_t(hr), args);
}
M(IDirect3DVolumeTexture9, LockBox) {
    auto* t = Host<IDirect3DVolumeTexture9>(Arg(c, 0));
    uint32_t level = Arg(c, 1);
    const D3DBOX* box = Ptr<D3DBOX>(Arg(c, 3));
    D3DLOCKED_BOX lb;
    HRESULT hr = t->LockBox(level, &lb, box, Arg(c, 4));
    D3DVOLUME_DESC desc;
    t->GetLevelDesc(level, &desc);
    LockBoxCommon(c, t, level, hr, lb, box ? box->Back - box->Front : desc.Depth, Arg(c, 4), Arg(c, 2), 5);
}
M(IDirect3DVolumeTexture9, UnlockBox) {
    auto* t = Host<IDirect3DVolumeTexture9>(Arg(c, 0));
    ShadowUnlock(t, Arg(c, 1));
    Return(c, uint32_t(t->UnlockBox(Arg(c, 1))), 2);
}
M(IDirect3DVolume9, LockBox) {
    auto* v = Host<IDirect3DVolume9>(Arg(c, 0));
    const D3DBOX* box = Ptr<D3DBOX>(Arg(c, 2));
    D3DLOCKED_BOX lb;
    HRESULT hr = v->LockBox(&lb, box, Arg(c, 3));
    D3DVOLUME_DESC desc;
    v->GetDesc(&desc);
    LockBoxCommon(c, v, 0, hr, lb, box ? box->Back - box->Front : desc.Depth, Arg(c, 3), Arg(c, 1), 4);
}
M(IDirect3DVolume9, UnlockBox) {
    auto* v = Host<IDirect3DVolume9>(Arg(c, 0));
    ShadowUnlock(v, 0);
    Return(c, uint32_t(v->UnlockBox()), 1);
}

template <typename T, typename Desc>
void BufferLock(Ctx* c) {
    auto* b = Host<T>(Arg(c, 0));
    uint32_t offset = Arg(c, 1), size = Arg(c, 2), out = Arg(c, 3);
    DWORD flags = Arg(c, 4);
    void* p = nullptr;
    HRESULT hr = b->Lock(offset, size, &p, flags);
    if (SUCCEEDED(hr)) {
        if (!size) {
            Desc d;
            b->GetDesc(&d);
            size = d.Size - offset;
        }
        W32(out, ShadowLock(b, 0, p, size, flags));
    }
    Return(c, uint32_t(hr), 5);
}
template <typename T>
void BufferUnlock(Ctx* c) {
    auto* b = Host<T>(Arg(c, 0));
    ShadowUnlock(b, 0);
    Return(c, uint32_t(b->Unlock()), 1);
}
M(IDirect3DVertexBuffer9, Lock) { BufferLock<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC>(c); }
M(IDirect3DVertexBuffer9, Unlock) { BufferUnlock<IDirect3DVertexBuffer9>(c); }
M(IDirect3DIndexBuffer9, Lock) { BufferLock<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC>(c); }
M(IDirect3DIndexBuffer9, Unlock) { BufferUnlock<IDirect3DIndexBuffer9>(c); }

#undef M

#include "d3d9_gen.inc"

namespace {

// Builds the guest vtables (one thunk per method) and registers Direct3DCreate9.
struct Registrar {
    Registrar() {
        static const ImportDef create[] = {{"d3d9.dll", "Direct3DCreate9", x_Direct3DCreate9}};
        RegisterImports(create, 1);
        for (const InterfaceDef& i : kInterfaces) {
            std::vector<ImportDef> defs;
            for (size_t k = 0; k < i.count; ++k) {
                std::string n = std::string(i.name) + "::" + i.methods[k].name;
                defs.push_back({"d3d9.com", strdup(n.c_str()), i.methods[k].fn});
            }
            RegisterImports(defs.data(), defs.size());
        }
    }
} g_registrar;

}  // namespace

// Called once guest memory exists (the vtables live there).
void InitD3D9Bridge() {
    for (const InterfaceDef& i : kInterfaces) {
        uint32_t vt = HeapAlloc(uint32_t(4 * i.count));
        for (size_t k = 0; k < i.count; ++k) {
            std::string n = std::string(i.name) + "::" + i.methods[k].name;
            W32(vt + uint32_t(4 * k), ImportThunk("d3d9.com", n.c_str()));
        }
        g_vtables[i.name] = vt;
    }
}

}  // namespace native
