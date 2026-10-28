// Small, host-independent cache for the D3D9 states that the engine repeatedly writes.
// The bridge serializes each device cache with its host call. Store is deliberately separate from Equal: a failed
// host call must not make a later retry look redundant.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace native {

class D3D9StateCache {
public:
    enum class Kind { Render, Sampler, TextureStage };

    bool Equal(Kind kind, uint32_t index, uint32_t value) const {
        const Entry* e = At(kind, index);
        return e && e->valid && e->value == value;
    }

    void Store(Kind kind, uint32_t index, uint32_t value) {
        if (Entry* e = At(kind, index)) *e = {value, true};
    }

    void Invalidate() {
        for (auto& e : render_) e.valid = false;
        for (auto& e : sampler_) e.valid = false;
        for (auto& e : texture_stage_) e.valid = false;
    }

private:
    struct Entry {
        uint32_t value = 0;
        bool valid = false;
    };

    static constexpr size_t kSamplerStates = 16, kSamplerTypes = 16;
    static constexpr size_t kTextureStages = 8, kTextureStageTypes = 64;

    Entry* At(Kind kind, uint32_t index) {
        return const_cast<Entry*>(static_cast<const D3D9StateCache*>(this)->At(kind, index));
    }

    const Entry* At(Kind kind, uint32_t index) const {
        switch (kind) {
            case Kind::Render:
                return index < render_.size() ? &render_[index] : nullptr;
            case Kind::Sampler:
                return index < sampler_.size() ? &sampler_[index] : nullptr;
            case Kind::TextureStage:
                return index < texture_stage_.size() ? &texture_stage_[index] : nullptr;
        }
        return nullptr;
    }

    std::array<Entry, 256> render_{};
    std::array<Entry, kSamplerStates * kSamplerTypes> sampler_{};
    std::array<Entry, kTextureStages * kTextureStageTypes> texture_stage_{};
};

constexpr uint32_t D3D9SamplerCacheIndex(uint32_t stage, uint32_t type) {
    return stage < 16 && type < 16 ? stage * 16 + type : 0xffffffffu;
}

constexpr uint32_t D3D9TextureStageCacheIndex(uint32_t stage, uint32_t type) {
    return stage < 8 && type < 64 ? stage * 64 + type : 0xffffffffu;
}

// Numeric D3D9 values keep this helper independent of the DXVK headers in its host test.
// D3DSAMP_MIN/MAG/MIPFILTER = 5/6/7, MAXANISOTROPY = 10; D3DTEXF_LINEAR/ANISOTROPIC = 2/3.
constexpr uint32_t D3D9EffectiveSamplerValue(uint32_t type, uint32_t requested, int maximum) {
    maximum = maximum < 1 ? 1 : maximum > 16 ? 16 : maximum;
    if (type == 10) return requested < uint32_t(maximum) ? requested : uint32_t(maximum);
    if (maximum == 1 && requested == 3 && (type == 5 || type == 6 || type == 7)) return 2;
    return requested;
}

}  // namespace native
