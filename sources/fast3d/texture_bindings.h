#pragma once

#include <stdint.h>

namespace Shipyard {
namespace Fast3D {

struct TextureSamplerState {
    bool linear;
    uint8_t s, t;

    TextureSamplerState() : linear(false), s(0), t(0) {
    }
    TextureSamplerState(bool linearValue, uint32_t sValue, uint32_t tValue)
        : linear(linearValue), s(static_cast<uint8_t>(sValue)), t(static_cast<uint8_t>(tValue)) {
    }

    bool operator==(const TextureSamplerState& other) const {
        return linear == other.linear && s == other.s && t == other.t;
    }
};

template <unsigned Count> class TextureBindings {
    struct Slot {
        const void* node;
        uint32_t texture;
        TextureSamplerState sampler;
        bool valid;
        Slot() : node(0), texture(0), sampler(), valid(false) {
        }
    };
    Slot slots[Count];

public:
    void Invalidate() {
        for (unsigned i = 0; i < Count; ++i)
            slots[i] = Slot();
    }

    template <class Node> Node* Bound(unsigned slot) const {
        return slots[slot].valid ? static_cast<Node*>(const_cast<void*>(slots[slot].node)) : 0;
    }

    bool NeedsSelect(unsigned slot, const void* node, uint32_t texture,
                     const TextureSamplerState& textureSampler) const {
        const Slot& current = slots[slot];
        return !current.valid || current.node != node || current.texture != texture ||
               !(current.sampler == textureSampler);
    }

    void Selected(unsigned slot, const void* node, uint32_t texture,
                  const TextureSamplerState& textureSampler) {
        Slot& current = slots[slot];
        current.node = node;
        current.texture = texture;
        current.sampler = textureSampler;
        current.valid = true;
    }

    bool NeedsSampler(unsigned slot, const TextureSamplerState& sampler) const {
        return !slots[slot].valid || !(slots[slot].sampler == sampler);
    }

    void SamplerChanged(unsigned slot, const TextureSamplerState& sampler) {
        if (slots[slot].valid)
            slots[slot].sampler = sampler;
    }

    bool Known(unsigned slot) const {
        return slots[slot].valid;
    }
    TextureSamplerState Sampler(unsigned slot) const {
        return slots[slot].sampler;
    }
};

} // namespace Fast3D
} // namespace Shipyard
