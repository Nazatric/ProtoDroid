// Smaller imports of the native host: WINMM timers, SHELL32/SHLWAPI paths, COM and network
// stubs (online features are not ported).
#include <sys/stat.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#include "guest_time.h"
#include "win.h"

namespace native {

bool CreateXAudio2(uint32_t clsid, uint32_t out, uint32_t* hr);       // xaudio2.cpp
bool CreateDirectInput8(uint32_t clsid, uint32_t out, uint32_t* hr);  // dinput.cpp

namespace {

auto g_start = std::chrono::steady_clock::now();

#define F(name) void x_##name(Ctx* c)

// WINMM
F(timeGetTime) {
    const uint64_t ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - g_start).count());
    Return(c, uint32_t(GuestElapsedNanos(ns) / 1000000ull));
}
F(timeSetEvent) {
    Log("timeSetEvent(%u ms, callback %08x) ignored", Arg(c, 0), Arg(c, 2));
    Return(c, 1, 5);
}
F(timeKillEvent) { Return(c, 0, 1); }
F(waveOutGetNumDevs) { Return(c, 0); }
F(waveOutGetDevCapsA) { Return(c, 2, 3); }  // MMSYSERR_BADDEVICEID
F(waveOutOpen) { Return(c, 2, 6); }
F(waveOutClose) { Return(c, 0, 1); }
F(waveOutWrite) { Return(c, 0, 3); }
F(waveOutPrepareHeader) { Return(c, 0, 3); }
F(waveOutUnprepareHeader) { Return(c, 0, 3); }
F(waveOutReset) { Return(c, 0, 1); }


// SHELL32 / SHLWAPI
F(SHGetFolderPathA) {
    // Documents / AppData: saves and settings go to C:\Users\Player\... (native_c on the host).
    char* output = Str(Arg(c, 4));
    if (!output) {
        Return(c, 0x80070057u, 5);  // E_INVALIDARG, not a native null-pointer write
        return;
    }
    strcpy(output, "C:\\Users\\Player\\Documents");
    std::string host = HostPath("C:\\Users\\Player\\Documents");
    std::string cmd = host;
    for (size_t i = 1; i < cmd.size(); ++i) {
        if (cmd[i] == '/') {
            cmd[i] = 0;
            mkdir(cmd.c_str(), 0755);
            cmd[i] = '/';
        }
    }
    mkdir(host.c_str(), 0755);
    Return(c, 0, 5);
}
F(PathRelativePathToA) { Return(c, 0, 5); }

// ole32 / OLEAUT32: COM is used for hardware queries and online services only.
F(CoInitialize) { Return(c, 0, 1); }
F(CoUninitialize) { Return(c, 0); }
F(CoCreateInstance) {
    uint32_t hr;
    if (CreateXAudio2(Arg(c, 0), Arg(c, 4), &hr) || CreateDirectInput8(Arg(c, 0), Arg(c, 4), &hr)) {
        Return(c, hr, 5);
        return;
    }
    const uint8_t* g = Ptr<uint8_t>(Arg(c, 0));
    Log("CoCreateInstance({%08x-%04x-%04x-%02x%02x-...}): class not available", R32(Arg(c, 0)), R16(Arg(c, 0) + 4),
        R16(Arg(c, 0) + 6), g[8], g[9]);
    if (Arg(c, 4)) W32(Arg(c, 4), 0);
    Return(c, 0x80040154u, 5);  // REGDB_E_CLASSNOTREG
}
F(CoCreateGuid) {
    for (int i = 0; i < 16; ++i) W8(Arg(c, 0) + i, uint8_t(rand()));
    Return(c, 0, 1);
}
F(CLSIDFromString) { Return(c, 0x800401f3u, 2); }  // CO_E_CLASSSTRING
F(CoSetProxyBlanket) { Return(c, 0, 8); }
F(SysAllocString) { Return(c, 0, 1); }
F(SysAllocStringLen) { Return(c, 0, 2); }
F(SysFreeString) { Return(c, 0, 1); }
F(VariantInit) { Return(c, 0, 1); }
F(VariantClear) { Return(c, 0, 1); }
F(VariantChangeType) { Return(c, 0x80004001u, 4); }
F(CreateErrorInfo) { Return(c, 0x80004001u, 1); }
F(SetErrorInfo) { Return(c, 0, 2); }
F(GetErrorInfo) {
    W32(Arg(c, 1), 0);
    Return(c, 1, 2);  // S_FALSE
}

// DSOUND: Android audio uses the native XAudio2 path. Enumerate no DirectSound devices rather than inventing one.
F(DirectSoundEnumerateA) {
    if (!Arg(c, 0)) {
        Return(c, 0x80070057u, 2);  // DSERR_INVALIDPARAM / E_INVALIDARG
        return;
    }
    Return(c, 0, 2);  // DS_OK: an empty enumeration never invokes the callback
}

// SETUPAPI / iphlpapi / RPCRT4: device and network enumeration finds nothing.
F(SetupDiGetClassDevsA) { Return(c, 0xffffffffu, 4); }
F(SetupDiEnumDeviceInterfaces) { Return(c, 0, 5); }
F(SetupDiGetDeviceInterfaceDetailA) { Return(c, 0, 6); }
F(SetupDiGetDeviceRegistryPropertyA) { Return(c, 0, 7); }
F(SetupDiGetDeviceInterfaceAlias) { Return(c, 0, 4); }
F(SetupDiDestroyDeviceInfoList) { Return(c, 1, 1); }
F(GetAdaptersInfo) { Return(c, 232, 2); }  // ERROR_NO_DATA
F(GetIpAddrTable) { Return(c, 232, 3); }
F(GetIfEntry) { Return(c, 232, 1); }
F(GetPerAdapterInfo) { Return(c, 232, 3); }
F(UuidToStringA) { Return(c, 14, 2); }  // RPC_S_OUT_OF_MEMORY
F(RpcStringFreeA) { Return(c, 0, 1); }

// WS2_32: the network subsystem starts but every connection attempt fails.
F(WSAStartup) {
    const uint32_t address = Arg(c, 1);
    void* data = Ptr<void>(address);
    if (!data) {
        Return(c, 10014, 2);  // WSAEFAULT, not a native null-pointer write
        return;
    }
    memset(data, 0, 400);
    W16(address, 0x0202);
    W16(address + 2, 0x0202);
    Return(c, 0, 2);
}
F(WSACleanup) { Return(c, 0); }
F(WSAGetLastError) { Return(c, 10050); }  // WSAENETDOWN
F(accept) { Return(c, 0xffffffffu, 3); }   // INVALID_SOCKET
F(bind) { Return(c, 0xffffffffu, 3); }     // SOCKET_ERROR
F(connect) { Return(c, 0xffffffffu, 3); }
F(getpeername) { Return(c, 0xffffffffu, 3); }
F(getsockname) { Return(c, 0xffffffffu, 3); }
F(getsockopt) { Return(c, 0xffffffffu, 5); }
F(ioctlsocket) { Return(c, 0xffffffffu, 3); }
F(listen) { Return(c, 0xffffffffu, 2); }
F(recv) { Return(c, 0xffffffffu, 4); }
F(recvfrom) { Return(c, 0xffffffffu, 6); }
F(select) { Return(c, 0xffffffffu, 5); }
F(send) { Return(c, 0xffffffffu, 4); }
F(sendto) { Return(c, 0xffffffffu, 6); }
F(setsockopt) { Return(c, 0xffffffffu, 5); }
F(shutdown) { Return(c, 0xffffffffu, 2); }
F(socket) { Return(c, 0xffffffffu, 3); }
F(closesocket) { Return(c, 0, 1); }
F(WSAFDIsSet) {
    const uint32_t set = Arg(c, 1);
    if (!Ptr<void>(set)) {
        Return(c, 0, 2);
        return;
    }
    const uint32_t socket = Arg(c, 0);
    const uint32_t count = R32(set);
    const uint32_t bounded = count < 64 ? count : 64;  // Win32 FD_SETSIZE
    for (uint32_t i = 0; i < bounded; ++i) {
        if (R32(set + 4 + 4 * i) == socket) {
            Return(c, 1, 2);
            return;
        }
    }
    Return(c, 0, 2);
}
F(htons) { Return(c, uint32_t(__builtin_bswap16(uint16_t(Arg(c, 0)))), 1); }
F(ntohs) { Return(c, uint32_t(__builtin_bswap16(uint16_t(Arg(c, 0)))), 1); }
F(htonl) { Return(c, __builtin_bswap32(Arg(c, 0)), 1); }
F(ntohl) { Return(c, __builtin_bswap32(Arg(c, 0)), 1); }
F(getaddrinfo) { Return(c, 11001, 4); }  // WSAHOST_NOT_FOUND
F(freeaddrinfo) { Return(c, 0, 1); }
F(WSAAsyncGetHostByName) { Return(c, 0, 5); }
F(WSACancelAsyncRequest) { Return(c, 0, 1); }

#undef F

#define E(dll, name) {dll, #name, x_##name}
#define O(ord, name) {"ws2_32.dll", "#" #ord, x_##name}
#define OLE(ord, name) {"oleaut32.dll", "#" #ord, x_##name}
#define DS(ord, name) {"dsound.dll", "#" #ord, x_##name}
const ImportDef kMisc[] = {
    E("winmm.dll", timeGetTime), E("winmm.dll", timeSetEvent), E("winmm.dll", timeKillEvent),
    E("winmm.dll", waveOutGetNumDevs), E("winmm.dll", waveOutGetDevCapsA), E("winmm.dll", waveOutOpen),
    E("winmm.dll", waveOutClose), E("winmm.dll", waveOutWrite), E("winmm.dll", waveOutPrepareHeader),
    E("winmm.dll", waveOutUnprepareHeader), E("winmm.dll", waveOutReset),
    E("shell32.dll", SHGetFolderPathA), E("shlwapi.dll", PathRelativePathToA),
    E("ole32.dll", CoInitialize), E("ole32.dll", CoUninitialize), E("ole32.dll", CoCreateInstance),
    E("ole32.dll", CoCreateGuid), E("ole32.dll", CLSIDFromString), E("ole32.dll", CoSetProxyBlanket),
    E("oleaut32.dll", SysAllocString), E("oleaut32.dll", SysAllocStringLen), E("oleaut32.dll", SysFreeString),
    E("oleaut32.dll", VariantInit), E("oleaut32.dll", VariantClear), E("oleaut32.dll", VariantChangeType),
    E("oleaut32.dll", CreateErrorInfo), E("oleaut32.dll", SetErrorInfo), E("oleaut32.dll", GetErrorInfo),
    // The engine imports these same nine exports by ordinal. Keep the exact slots on their real named handlers;
    // mappings follow Wine's oleaut32.spec rather than treating an ordinal as a new or successfully stubbed API.
    OLE(2, SysAllocString), OLE(4, SysAllocStringLen), OLE(6, SysFreeString), OLE(8, VariantInit),
    OLE(9, VariantClear), OLE(12, VariantChangeType), OLE(200, GetErrorInfo), OLE(201, SetErrorInfo),
    OLE(202, CreateErrorInfo),
    E("dsound.dll", DirectSoundEnumerateA), DS(2, DirectSoundEnumerateA),
    E("setupapi.dll", SetupDiGetClassDevsA), E("setupapi.dll", SetupDiEnumDeviceInterfaces),
    E("setupapi.dll", SetupDiGetDeviceInterfaceDetailA), E("setupapi.dll", SetupDiGetDeviceRegistryPropertyA),
    E("setupapi.dll", SetupDiGetDeviceInterfaceAlias), E("setupapi.dll", SetupDiDestroyDeviceInfoList),
    E("iphlpapi.dll", GetAdaptersInfo), E("iphlpapi.dll", GetIpAddrTable), E("iphlpapi.dll", GetIfEntry),
    E("iphlpapi.dll", GetPerAdapterInfo), E("rpcrt4.dll", UuidToStringA), E("rpcrt4.dll", RpcStringFreeA),
    E("ws2_32.dll", WSAStartup), E("ws2_32.dll", WSACleanup), E("ws2_32.dll", WSAGetLastError),
    E("ws2_32.dll", accept), E("ws2_32.dll", bind), E("ws2_32.dll", connect), E("ws2_32.dll", getpeername),
    E("ws2_32.dll", getsockname), E("ws2_32.dll", getsockopt), E("ws2_32.dll", ioctlsocket),
    E("ws2_32.dll", listen), E("ws2_32.dll", recv), E("ws2_32.dll", recvfrom), E("ws2_32.dll", select),
    E("ws2_32.dll", send), E("ws2_32.dll", sendto), E("ws2_32.dll", setsockopt), E("ws2_32.dll", shutdown),
    E("ws2_32.dll", socket), E("ws2_32.dll", closesocket), E("ws2_32.dll", htons), E("ws2_32.dll", ntohs),
    E("ws2_32.dll", htonl), E("ws2_32.dll", ntohl), E("ws2_32.dll", getaddrinfo), E("ws2_32.dll", freeaddrinfo),
    E("ws2_32.dll", WSAAsyncGetHostByName), E("ws2_32.dll", WSACancelAsyncRequest),
    {"ws2_32.dll", "__WSAFDIsSet", x_WSAFDIsSet},
    // Keep every Winsock ordinal imported by the engine on the same implementation as its verified named export.
    O(1, accept), O(2, bind), O(3, closesocket), O(4, connect), O(5, getpeername), O(6, getsockname),
    O(7, getsockopt), O(8, htonl), O(9, htons), O(10, ioctlsocket), O(13, listen), O(14, ntohl), O(15, ntohs),
    O(16, recv), O(17, recvfrom), O(18, select), O(19, send), O(20, sendto), O(21, setsockopt),
    O(22, shutdown), O(23, socket), O(103, WSAAsyncGetHostByName), O(108, WSACancelAsyncRequest),
    O(111, WSAGetLastError), O(115, WSAStartup), O(116, WSACleanup), O(151, WSAFDIsSet),
};
#undef E
#undef O
#undef OLE
#undef DS
ImportRegistrar g_misc(kMisc);

}  // namespace

}  // namespace native
