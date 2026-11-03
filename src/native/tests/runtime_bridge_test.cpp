// Production runtime/import-dispatch tests with a small synthetic guest address space. No game files are required.
#include <sys/wait.h>
#include <unistd.h>

#include <cstdarg>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../guest.h"

namespace native {
extern thread_local Ctx* tl_ctx;
}

namespace {

std::vector<uint8_t> g_memory(2u << 20);
uint32_t g_next_page = 0x1000;
uint32_t g_import_calls = 0;
uint32_t g_recompiled_calls = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "runtime bridge test: %s\n", message);
        abort();
    }
}

void ImportProbe(Ctx* c) {
    using namespace native;
    Check(tl_ctx == c, "import did not receive the active guest context");
    Check(Arg(c, 0) == 0x12345678u, "integer argument changed");
    Check(ArgF(c, 1) == 3.5f, "float argument changed");
    Check(ArgD(c, 2) == -12.75, "four-byte-aligned double argument changed");
    ++g_import_calls;
    Return(c, 0x89abcdefu, 4);
}

void RecompiledProbe(Ctx* c) {
    ++g_recompiled_calls;
    native::Return(c, 0x11223344u);
}

void PrepareImportCall(Ctx* c) {
    memset(c, 0, sizeof(*c));
    c->esp = 0x8000;
    W32(c->esp, 0x4444u);
    W32(c->esp + 4, 0x12345678u);
    WF32(c->esp + 8, 3.5f);
    // x86 stack arguments need only four-byte alignment. This deliberately exercises the unaligned double path.
    WF64(c->esp + 12, -12.75);
}

void CheckImportCall(Ctx* c, uint32_t target, Ctx* outer) {
    const uint32_t initial_esp = 0x8000;
    PrepareImportCall(c);
    native::tl_ctx = outer;
    RecompDispatch(c, target);
    Check(native::tl_ctx == outer, "import did not restore the outer guest context");
    Check(c->eax == 0x89abcdefu, "import return value changed");
    Check(c->ret_target == 0x4444u, "import return target changed");
    Check(c->esp == initial_esp + 20, "stdcall stack cleanup changed");
}

void CheckRecompiledCall(Ctx* c, uint32_t target, Ctx* outer) {
    memset(c, 0, sizeof(*c));
    c->esp = 0x8000;
    W32(c->esp, 0x5555u);
    native::tl_ctx = outer;
    RecompDispatch(c, target);
    Check(native::tl_ctx == outer, "recompiled dispatch changed the outer guest context");
    Check(c->eax == 0x11223344u, "recompiled target did not run");
    Check(c->ret_target == 0x5555u && c->esp == 0x8004, "recompiled return changed");
}

void CheckOleAut32Route(const char* name, const char* ordinal, uint32_t expected, int args) {
    using namespace native;
    const char* routes[] = {name, ordinal};
    for (const char* route : routes) {
        const uint32_t target = ImportThunk("oleaut32.dll", route);
        Ctx ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.esp = 0x8000;
        W32(ctx.esp, 0x6666u);
        W32(ctx.esp + 4, 0x6000u);
        W32(ctx.esp + 8, 0x7000u);
        W32(0x7000u, 0xdeadbeefu);
        Check(DispatchImport(&ctx, target), "OLEAUT32 route was not an import thunk");
        Check(ctx.eax == expected, "OLEAUT32 named/ordinal behavior differs");
        Check(ctx.ret_target == 0x6666u, "OLEAUT32 return target changed");
        Check(ctx.esp == 0x8004u + uint32_t(4 * args), "OLEAUT32 stdcall cleanup changed");
        if (strcmp(name, "GetErrorInfo") == 0)
            Check(R32(0x7000u) == 0, "OLEAUT32 GetErrorInfo did not clear the output object");
    }
}

void PrepareMiscCall(Ctx* ctx, uint32_t arg0, uint32_t arg1) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->esp = 0x8000;
    W32(ctx->esp, 0x7777u);
    W32(ctx->esp + 4, arg0);
    W32(ctx->esp + 8, arg1);
}

void CheckMiscReturn(const Ctx& ctx, uint32_t expected, int args, const char* message) {
    Check(ctx.eax == expected, message);
    Check(ctx.ret_target == 0x7777u, "misc import return target changed");
    Check(ctx.esp == 0x8004u + uint32_t(4 * args), "misc import stdcall cleanup changed");
}

void CheckSetjmpRoundTrip() {
    constexpr uint32_t guest_buf = 0x12340;
    Ctx* ctx = new Ctx;
    memset(ctx, 0, sizeof(*ctx));
    ctx->esp = 0x9000;
    ctx->ebp = 0x11111111;
    ctx->ebx = 0x22222222;
    ctx->esi = 0x33333333;
    ctx->edi = 0x44444444;
    ctx->st_top = 3;
    ctx->fpu_cw = 0x027f;
    ctx->eax = 0xaaaaaaaa;

    jmp_buf* host = RecompSetjmpPrepare(ctx, guest_buf);
    Check(host != nullptr, "setjmp preparation returned no host jump buffer");
    if (setjmp(*host) == 0) {
        RecompSetjmpResume(ctx, guest_buf);
        Check(ctx->eax == 0, "initial setjmp resume did not return zero");
        ctx->esp = 0xdead0000;
        ctx->ebp = 0xdead0001;
        ctx->ebx = 0xdead0002;
        ctx->esi = 0xdead0003;
        ctx->edi = 0xdead0004;
        ctx->st_top = 7;
        ctx->fpu_cw = 0xffff;
        RecompLongjmp(ctx, guest_buf, 7);
    }

    RecompSetjmpResume(ctx, guest_buf);
    Check(ctx->esp == 0x9000 && ctx->ebp == 0x11111111 && ctx->ebx == 0x22222222,
          "longjmp did not restore the stack/frame registers");
    Check(ctx->esi == 0x33333333 && ctx->edi == 0x44444444,
          "longjmp did not restore the callee-saved registers");
    Check(ctx->st_top == 3 && ctx->fpu_cw == 0x027f, "longjmp did not restore floating-point state");
    Check(ctx->eax == 7, "longjmp return value was not preserved");
    delete ctx;
}

void CheckUnknownSetjmpResumeFailsCleanly() {
    const pid_t child = fork();
    Check(child >= 0, "could not create unknown-jmp-buffer death-test process");
    if (child == 0) {
        freopen("/dev/null", "w", stderr);
        Ctx ctx = {};
        RecompSetjmpResume(&ctx, 0x12345000);
        _exit(0);
    }
    int status = 0;
    Check(waitpid(child, &status, 0) == child, "could not reap unknown-jmp-buffer death-test process");
    Check(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
          "unknown jump buffer did not follow the controlled fatal diagnostic path");
}

}  // namespace

extern "C" {
struct RecompTableEntry {
    uint32_t va;
    RecompFn fn;
};
extern const RecompTableEntry g_recomp_table[] = {{0x110100u, RecompiledProbe}};
extern const uint32_t g_recomp_count = 1;
volatile uint32_t RecompDiagnosticTour = 0;
}

namespace native {

void Log(const char*, ...) {}
[[noreturn]] void Fatal(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    abort();
}

uint32_t PageAlloc(uint32_t size, uint32_t at) {
    if (at) return at;
    const uint32_t result = g_next_page;
    g_next_page += (size + 0xfffu) & ~0xfffu;
    Check(g_next_page <= g_memory.size(), "synthetic guest memory exhausted");
    return result;
}

GuestThread* CurrentThread() {
    static GuestThread thread = {};
    thread.stack_hi = 0x9000;
    return &thread;
}

int SettingInt(const char*, int fallback) { return fallback; }
void DiagnosticTourGameplayObserved(uint32_t) {}

// misc.cpp's production import table references these factory hooks and path helper. The selected OLEAUT32 and
// null-boundary routes return without creating an audio, input, or host filesystem service.
bool CreateXAudio2(uint32_t, uint32_t, uint32_t*) { return false; }
bool CreateDirectInput8(uint32_t, uint32_t, uint32_t*) { return false; }
std::string HostPath(const char*) { return {}; }

}  // namespace native

int main() {
    using namespace native;
    RecompMemoryBase = g_memory.data();
    g_engine_lo = 0x110000;
    g_engine_hi = 0x111000;

    const ImportDef defs[] = {{"test.dll", "Probe", ImportProbe}};
    RegisterImports(defs, 1);
    const uint32_t import = ImportThunk("test.dll", "Probe");
    Check(import == 0x1000u, "unexpected import thunk address");
    Check(ImportThunk("test.dll", "Probe") == import, "import thunk identity changed");

    Ctx ctx;
    Ctx outer;
    memset(&outer, 0, sizeof(outer));
    CheckImportCall(&ctx, import, &outer);
    Check(g_import_calls == 1, "direct import was not dispatched exactly once");

    // A normal computed guest call still reaches the recompiled-function table.
    CheckRecompiledCall(&ctx, 0x110100u, &outer);
    Check(g_recompiled_calls == 1, "direct recompiled target was not dispatched exactly once");

    // An unresolved engine near-jump thunk still resolves to a recompiled target.
    W8(0x110000, 0xe9);
    W32(0x110001, 0x110100u - (0x110000u + 5u));
    CheckRecompiledCall(&ctx, 0x110000u, &outer);
    Check(g_recompiled_calls == 2, "followed recompiled target was not dispatched exactly once");

    // The other important legacy route is jmp dword ptr [iat], whose final target is an import thunk.
    W8(0x110020, 0xff);
    W8(0x110021, 0x25);
    W32(0x110022, 0x110800u);
    W32(0x110800, import);
    CheckImportCall(&ctx, 0x110020u, &outer);
    Check(g_import_calls == 2, "followed import target was not dispatched exactly once");

    // These ordinal keys occur in the shipped engine. Each must reach the existing named implementation rather than
    // the fatal null-handler path, with the same return value and stdcall stack contract.
    CheckOleAut32Route("SysAllocString", "#2", 0, 1);
    CheckOleAut32Route("SysAllocStringLen", "#4", 0, 2);
    CheckOleAut32Route("SysFreeString", "#6", 0, 1);
    CheckOleAut32Route("VariantInit", "#8", 0, 1);
    CheckOleAut32Route("VariantClear", "#9", 0, 1);
    CheckOleAut32Route("VariantChangeType", "#12", 0x80004001u, 4);
    CheckOleAut32Route("GetErrorInfo", "#200", 1, 2);
    CheckOleAut32Route("SetErrorInfo", "#201", 0, 2);
    CheckOleAut32Route("CreateErrorInfo", "#202", 0x80004001u, 1);

    // Invalid output pointers must produce API errors rather than native null writes. These cases return before raw
    // pointer access, so they are valid in this synthetic non-flat guest-memory fixture as well as on Android.
    PrepareMiscCall(&ctx, 0, 0);
    W32(ctx.esp + 20, 0);  // Arg(4): output path
    Check(DispatchImport(&ctx, ImportThunk("shell32.dll", "SHGetFolderPathA")), "SHGetFolderPathA missing");
    CheckMiscReturn(ctx, 0x80070057u, 5, "SHGetFolderPathA accepted a null output pointer");

    PrepareMiscCall(&ctx, 0x0202, 0);
    Check(DispatchImport(&ctx, ImportThunk("ws2_32.dll", "WSAStartup")), "WSAStartup missing");
    CheckMiscReturn(ctx, 10014, 2, "WSAStartup accepted a null WSADATA pointer");

    CheckSetjmpRoundTrip();
    CheckUnknownSetjmpResumeFailsCleanly();

    puts("Runtime bridge imports, boundaries, jump state, dispatch, and guest-context checks passed");
    return 0;
}
