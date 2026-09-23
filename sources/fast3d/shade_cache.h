#pragma once

#include <stdint.h>
#include <string.h>

namespace Shipyard { namespace Fast3D {

struct ShadeKey {
    uint32_t words[140];
    unsigned count;

    template<class Light, class ColorAt>
    void Set(unsigned lights, const Light* colors, ColorAt colorAt,
             const float (*directions)[3], unsigned texgen,
             const float (*lookat)[3], unsigned s, unsigned t) {
        count = 0;
        words[count++] = lights;
        words[count++] = texgen;
        words[count++] = texgen ? (s << 16) | t : 0;
        for (unsigned i = 0; i < lights; ++i) {
            words[count++] = (uint32_t(colorAt(colors[i], 0)) << 16) |
                             (uint32_t(colorAt(colors[i], 1)) << 8) |
                             colorAt(colors[i], 2);
            if (i + 1 < lights) {
                memcpy(words + count, directions[i], 12);
                count += 3;
            }
        }
        if (texgen) {
            memcpy(words + count, lookat, 24);
            count += 6;
        }
    }

    bool Equals(const ShadeKey& other) const {
        return count == other.count && memcmp(words, other.words, count * 4) == 0;
    }
};

struct ShadeValue { uint8_t rgb[3]; float u, v; };

// Keep the large value-owned cache in its interpreter, not XDK TLS.
class ShadeCache {
    struct Entry { uint32_t normal, generation; ShadeValue value; };
    struct State { ShadeKey key; uint32_t generation; Entry entries[256]; };
    State states[4];
    unsigned current, recent[4];
    bool initialized;

    void Touch(unsigned index) {
        unsigned position = 0;
        while (recent[position] != index) ++position;
        while (position) {
            recent[position] = recent[position - 1];
            --position;
        }
        recent[0] = index;
        current = index;
    }

    static unsigned Slot(uint32_t normal) { return (normal * 0x9e3779b9u) >> 24; }

public:
    ShadeCache() : current(0), initialized(false) {}

    void Initialize() {
        if (initialized) return;
        current = 0;
        memset(states, 0, sizeof(states));
        for (unsigned i = 0; i < 4; ++i) recent[i] = i;
        initialized = true;
    }

    void Select(const ShadeKey& key) {
        if (states[current].generation && states[current].key.Equals(key)) return;
        for (unsigned i = 0; i < 4; ++i) {
            if (states[i].generation && states[i].key.Equals(key)) {
                Touch(i);
                return;
            }
        }
        Touch(recent[3]);
        State& state = states[current];
        state.key.count = key.count;
        memcpy(state.key.words, key.words, key.count * 4);
        if (++state.generation == 0) {
            memset(state.entries, 0, sizeof(state.entries));
            state.generation = 1;
        }
    }

    static uint32_t Normal(const int8_t* normal) {
        return (uint32_t(uint8_t(normal[0])) << 16) |
               (uint32_t(uint8_t(normal[1])) << 8) | uint8_t(normal[2]);
    }

    const ShadeValue* Find(uint32_t normal) const {
        const State& state = states[current];
        const Entry& entry = state.entries[Slot(normal)];
        return entry.generation == state.generation && entry.normal == normal ? &entry.value : NULL;
    }

    void Store(uint32_t normal, const ShadeValue& value) {
        State& state = states[current];
        Entry& entry = state.entries[Slot(normal)];
        entry.normal = normal;
        entry.generation = state.generation;
        entry.value = value;
    }
};

inline bool ShadeStateFinite(unsigned lights, const float (*directions)[3],
                             const float (*lookat)[3]) {
    for (unsigned light = 0; light + 1 < lights; ++light) {
        for (unsigned c = 0; c < 3; ++c) {
            const float value = directions[light][c];
            if (!(value >= -1.0f && value <= 1.0f)) return false;
        }
    }
    for (unsigned axis = 0; axis < 2; ++axis) {
        for (unsigned c = 0; c < 3; ++c) {
            const float value = lookat[axis][c];
            if (!(value >= -1.0f && value <= 1.0f)) return false;
        }
    }
    return true;
}

} }
