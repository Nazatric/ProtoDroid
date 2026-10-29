// Native replacement of the instance matrix blending FUN_1071b610 (pure3d skin.cpp; run by the engine's worker
// threads, the busiest code there on phones). The original is packed SSE; each lane is plain float arithmetic,
// associated exactly as in the original, FP contraction off.
#include <cstring>

#include "hot_common.h"

#pragma STDC FP_CONTRACT OFF
#pragma clang fp contract(off)

namespace native {

namespace {

// GCC and Clang lower this four-lane vector to SSE on x86-64 and NEON on ARM64. Keeping each operation and its
// association explicit preserves the original arithmetic while avoiding the compiler's partial scalarization of
// the local-frame products. Loads/stores stay memcpy-based because records and outputs need not share alignment.
typedef float V4 __attribute__((vector_size(16)));

inline V4 Load(uint32_t a) {
    V4 v;
    memcpy(&v, Ptr<void>(a), 16);
    return v;
}
inline V4 Mul(V4 a, float s) { return a * V4{s, s, s, s}; }
inline V4 Add(V4 a, V4 b) { return a + b; }
// maxps/minps: the second operand wins unless the first compares greater/less (NaN -> second).
inline float MaxPs(float a, float b) { return a > b ? a : b; }
inline float MinPs(float a, float b) { return a < b ? a : b; }

}  // namespace

// FUN_1071b610 (thiscall, 3 args: out, records, count). Unless the render settings object says to skip it, blends
// four bone matrices per record (weights at +0x30, bone indices at +0x40) and multiplies the record's local 3x4
// frame (+0x00/+0x10/+0x20) by the result into three output rows, keeping the bounding box of the first rows in
// this+0x2c4 (min) / this+0x2d0 (max).
extern "C" void Native_1071b610(Ctx* c) {
    const uint32_t esp = c->esp;
    const uint32_t self = c->ecx;
    const uint32_t out0 = R32(esp + 4), records = R32(esp + 8), count = R32(esp + 12);

    // settings = (*g_render)->vf[0x60](0x400001); if (settings->vf[0x1c]()) skip
    const uint32_t render = R32(E(0x1127f354));
    const uint32_t settings = CallGuestThiscall(c, R32(R32(render) + 0x60), render, {0x400001});
    const bool skip = (CallGuestThiscall(c, R32(R32(settings) + 0x1c), settings, {}) & 0xff) != 0;
    if (!skip) {
        const uint32_t bones = R32(R32(self + 0x70) + 0x38);
        if (R8(E(0x111378b0)) != 0 && (bones & 0xf) != 0) {
            const uint32_t r = CallGuestCdecl(c, E(0x1063c750), {1, E(0x10d9e93c), E(0x10e5bdc8), 0x298, E(0x10e5bdfc),
                                                                 E(0x10e5be20), 0});
            if (r == 3) W8(E(0x111378b0), 0);
        }
        const float lo0 = F(E(0x10da251c)), hi0 = F(E(0x10e5bcc8));
        V4 vmax = {lo0, lo0, lo0, lo0}, vmin = {hi0, hi0, hi0, hi0};
        const float one = F(E(0x10d946e0));
        uint32_t rec = records, out = out0;
        for (uint32_t n = 0; n < count; ++n, rec += 0x50, out += 0x30) {
            const V4 w = Load(rec + 0x30);
            uint32_t b[4];
            for (int k = 0; k < 4; ++k) b[k] = uint32_t(R16(rec + 0x40 + 2 * k)) * 0x40 + bones;
            V4 rows[4];
            for (int r = 0; r < 4; ++r) {
                const uint32_t o = uint32_t(r) * 0x10;
                rows[r] = Add(Add(Add(Mul(Load(b[0] + o), w[0]), Mul(Load(b[1] + o), w[1])), Mul(Load(b[2] + o), w[2])),
                              Mul(Load(b[3] + o), w[3]));
            }
            const V4 l0 = Load(rec), l1 = Load(rec + 0x10), l2 = Load(rec + 0x20);
            V4 r0 = Add(Add(Add(Mul(rows[0], l0[0]), Mul(rows[1], l0[1])), Mul(rows[2], l0[2])), rows[3]);
            V4 r1 = Add(Add(Mul(rows[0], l1[0]), Mul(rows[1], l1[1])), Mul(rows[2], l1[2]));
            V4 r2 = Add(Add(Mul(rows[0], l2[0]), Mul(rows[1], l2[1])), Mul(rows[2], l2[2]));
            r0[3] = one;
            r1[3] = one;
            r2[3] = l2[3];
            memcpy(Ptr<void>(out), &r0, 16);
            memcpy(Ptr<void>(out + 0x10), &r1, 16);
            memcpy(Ptr<void>(out + 0x20), &r2, 16);
            vmax = V4{MaxPs(vmax[0], r0[0]), MaxPs(vmax[1], r0[1]), MaxPs(vmax[2], r0[2]), MaxPs(vmax[3], r0[3])};
            vmin = V4{MinPs(vmin[0], r0[0]), MinPs(vmin[1], r0[1]), MinPs(vmin[2], r0[2]), MinPs(vmin[3], r0[3])};
        }
        WF32(self + 0x2c4, vmin[0]);
        WF32(self + 0x2c8, vmin[1]);
        WF32(self + 0x2cc, vmin[2]);
        WF32(self + 0x2d0, vmax[0]);
        WF32(self + 0x2d4, vmax[1]);
        WF32(self + 0x2d8, vmax[2]);
    }
    c->ret_target = Pop32(c);
    c->esp += 12;  // thiscall: the callee pops its 3 arguments
}

}  // namespace native
