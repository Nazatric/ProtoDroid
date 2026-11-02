// Production runtime/import-dispatch tests with a small synthetic guest address space. No game files are required.
#include <cstdarg>
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

    puts("Runtime bridge argument, direct/followed dispatch, and guest-context checks passed");
    return 0;
}
