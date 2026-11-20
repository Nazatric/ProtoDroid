// Unit tests for the per-flag liveness masks (runtime.h RECOMP_F_*, tools/recomp/recomp.py live_mask).
//
// The generated code passes a compile-time literal mask so the compiler folds dead flag writes away. These
// tests pin the semantics the recompiler relies on:
//   1. A masked helper computes exactly the flags its mask requests, and leaves the others bit-identical.
//   2. With the full status mask every helper matches the architectural x86 flag definitions (spot tables
//      below cover carry/borrow, signed overflow, and parity).
//   3. inc/dec (mask without CF) never touch CF - the recompiler relies on this to drop its save/restore.
#include "runtime.h"
#include "runtime_fp.h"

#include <cstdio>
#include <cstdlib>

namespace {

constexpr uint32_t SENTINEL = 0x99;

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "flag mask test failed: %s\n", message);
        ++failures;
    }
}

struct Flags {
    uint8_t cf, zf, sf, of, pf, af, df;
};

Flags RunAdd(uint32_t a, uint32_t b, uint32_t carry, uint32_t live) {
    Ctx c{};
    c.cf = SENTINEL + 1; c.zf = SENTINEL + 2; c.sf = SENTINEL + 3; c.of = SENTINEL + 4; c.pf = SENTINEL + 5; c.af = SENTINEL + 6; c.df = SENTINEL + 7;
    AddFlags(&c, a, b, carry, 32, live);
    return {c.cf, c.zf, c.sf, c.of, c.pf, c.af, c.df};
}

Flags RunSub(uint32_t a, uint32_t b, uint32_t borrow, uint32_t live) {
    Ctx c{};
    c.cf = SENTINEL + 1; c.zf = SENTINEL + 2; c.sf = SENTINEL + 3; c.of = SENTINEL + 4; c.pf = SENTINEL + 5; c.af = SENTINEL + 6; c.df = SENTINEL + 7;
    SubFlags(&c, a, b, borrow, 32, live);
    return {c.cf, c.zf, c.sf, c.of, c.pf, c.af, c.df};
}

// Architectural references (32-bit ops).
uint32_t RefCfAdd(uint32_t a, uint32_t b, uint32_t carry) {
    uint64_t w = uint64_t(a) + b + carry;
    return (w >> 32) & 1;
}
uint32_t RefOfAdd(uint32_t a, uint32_t b, uint32_t r) {
    return ((~(a ^ b) & (a ^ r)) >> 31) & 1;
}
uint32_t RefCfSub(uint32_t a, uint32_t b, uint32_t borrow) {
    return uint64_t(a) < uint64_t(b) + borrow;
}
uint32_t RefOfSub(uint32_t a, uint32_t b, uint32_t r) {
    return (((a ^ b) & (a ^ r)) >> 31) & 1;
}
uint32_t RefPf(uint32_t r) { return Parity(r); }

}  // namespace

int main() {
    const uint32_t ALL = RECOMP_F_STATUS;

    // --- masked helpers leave unrequested flags untouched ---
    Flags f = RunAdd(7, 3, 0, RECOMP_F_ZF);
    Check(f.cf == SENTINEL + 1 && f.sf == SENTINEL + 3 && f.of == SENTINEL + 4 && f.pf == SENTINEL + 5 &&
              f.af == SENTINEL + 6 && f.df == SENTINEL + 7 && f.zf == 0,
          "AddFlags with ZF-only mask wrote a flag it must not touch");
    f = RunAdd(0xffffffffu, 1, 0, RECOMP_F_CF);
    Check(f.cf == 1 && f.zf == SENTINEL + 2 && f.of == SENTINEL + 4,
          "AddFlags with CF-only mask: cf=1, zf/of untouched");
    f = RunSub(0, 1, 0, RECOMP_F_CF | RECOMP_F_ZF);
    Check(f.cf == 1 && f.zf == 0 && f.sf == SENTINEL + 3 && f.of == SENTINEL + 4,
          "SubFlags with CF|ZF mask leaves sf/of untouched");

    // --- full mask matches the architecture ---
    struct AddCase { uint32_t a, b, carry; };
    const AddCase adds[] = {
        {0, 0, 0}, {0xffffffffu, 1, 0}, {0xffffffffu, 0, 1}, {0x7fffffffu, 1, 0},
        {0x80000000u, 0x80000000u, 0}, {0x12345678u, 0x87654321u, 1}, {0xfffffffeu, 1, 1},
    };
    for (const AddCase& t : adds) {
        f = RunAdd(t.a, t.b, t.carry, ALL);
        uint32_t r = t.a + t.b + t.carry;
        char msg[128];
        snprintf(msg, sizeof(msg), "AddFlags(%08x,%08x,%u) full-mask mismatch", t.a, t.b, t.carry);
        Check(f.cf == RefCfAdd(t.a, t.b, t.carry) && f.zf == (r == 0) && f.sf == ((r >> 31) & 1) &&
                  f.of == RefOfAdd(t.a, t.b, r) && f.pf == RefPf(r),
              msg);
    }
    struct SubCase { uint32_t a, b, borrow; };
    const SubCase subs[] = {
        {0, 0, 0}, {0, 1, 0}, {5, 7, 0}, {0x80000000u, 1, 0}, {0, 0, 1},
        {0x7fffffffu, 0x80000000u, 0}, {0xdeadbeefu, 0xdeadbeefu, 0}, {3, 0xffffffffu, 1},
    };
    for (const SubCase& t : subs) {
        f = RunSub(t.a, t.b, t.borrow, ALL);
        uint32_t r = t.a - t.b - t.borrow;
        char msg[128];
        snprintf(msg, sizeof(msg), "SubFlags(%08x,%08x,%u) full-mask mismatch", t.a, t.b, t.borrow);
        Check(f.cf == RefCfSub(t.a, t.b, t.borrow) && f.zf == (r == 0) && f.sf == ((r >> 31) & 1) &&
                  f.of == RefOfSub(t.a, t.b, r) && f.pf == RefPf(r),
              msg);
    }

    // --- RECOMP_FLAGS_LOGIC clears CF/OF, sets ZF/SF/PF from the result ---
    {
        Ctx c{};
        c.cf = 1; c.of = 1; c.zf = 0; c.sf = 0; c.pf = 0;
        RECOMP_FLAGS_LOGIC(&c, 0x80000001u, 32, RECOMP_F_STATUS);
        Check(c.cf == 0 && c.of == 0 && c.zf == 0 && c.sf == 1 && c.pf == RefPf(0x80000001u),
              "RECOMP_FLAGS_LOGIC full-mask mismatch");
        c.cf = 1; c.of = 1; c.zf = 0; c.sf = 0;
        RECOMP_FLAGS_LOGIC(&c, 0x80000001u, 32, RECOMP_F_ZF);
        Check(c.zf == 0 && c.cf == 1 && c.of == 1 && c.sf == 0,
              "RECOMP_FLAGS_LOGIC ZF-only mask touched another flag");
    }

    // --- 16-bit and 8-bit operand sizes (zf/sf respect the width) ---
    {
        Ctx c{};
        AddFlags(&c, 0x7fffu, 1u, 0, 16, RECOMP_F_STATUS);
        Check(c.zf == 0 && c.sf == 1 && c.cf == 0 && c.of == 1,
              "16-bit AddFlags overflow/width mismatch");
        AddFlags(&c, 0xffffu, 1u, 0, 16, RECOMP_F_STATUS);
        Check(c.zf == 1 && c.cf == 1, "16-bit AddFlags wrap mismatch");
        SubFlags(&c, 0x00u, 0x01u, 0, 8, RECOMP_F_STATUS);
        Check(c.cf == 1 && c.zf == 0 && c.sf == 1 && c.of == 0, "8-bit SubFlags mismatch");
    }

    // --- X87CompareFlags masked variants (fcomi family) ---
    {
        Ctx c{};
        c.cf = 0xC1; c.zf = 0xC2; c.pf = 0xC3; c.of = 0xC4; c.sf = 0xC5; c.af = 0xC6;
        X87CompareFlags(&c, 1.0, 2.0, RECOMP_F_ZF | RECOMP_F_PF | RECOMP_F_CF);
        // less: ZF=0 PF=0 CF=1; of/sf/af cleared only when requested - here they must be untouched
        Check(c.cf == 1 && c.zf == 0 && c.pf == 0 && c.of == 0xC4 && c.sf == 0xC5 && c.af == 0xC6,
              "X87CompareFlags masked variant touched of/sf/af without a request");
        X87CompareFlags(&c, 2.0, 2.0, RECOMP_F_STATUS);
        Check(c.zf == 1 && c.pf == 0 && c.cf == 0 && c.of == 0 && c.sf == 0,
              "X87CompareFlags full-mask equal-case mismatch");
        X87CompareFlags(&c, 1.0, 0.0 / 0.0, RECOMP_F_STATUS);  // unordered: ZF=PF=CF=1
        Check(c.zf == 1 && c.pf == 1 && c.cf == 1, "X87CompareFlags unordered-case mismatch");
    }

    // --- RECOMP_F_* bit order matches recomp.py FLAG_NAMES (CF ZF SF OF PF AF DF) ---
    Check(RECOMP_F_CF == 0x01u && RECOMP_F_ZF == 0x02u && RECOMP_F_SF == 0x04u && RECOMP_F_OF == 0x08u &&
              RECOMP_F_PF == 0x10u && RECOMP_F_AF == 0x20u,
          "RECOMP_F_* bit values drifted from recomp.py FLAG_NAMES order");

    if (failures) {
        std::fprintf(stderr, "%d flag mask test failure(s)\n", failures);
        return 1;
    }
    std::puts("Flag mask tests passed (masked writes, architectural spot tables, widths, x87 compare)");
    return 0;
}
