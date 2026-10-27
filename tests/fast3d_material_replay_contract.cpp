// Executes fast3d_material_replay's projected decode and texture lookup
// against the original methods through random state, clears and owner reuse.
#include "fast3d/material_memo.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

struct LoadedTexture {
    bool masked, blended;
};
struct RDP {
    uint64_t combine_mode;
    uint32_t other_mode_l, other_mode_h;
    bool grayscale;
    LoadedTexture loaded_texture[2];
};
struct ColorCombinerKey {
    uint64_t combine_mode, options;
    bool operator<(const ColorCombinerKey& rhs) const {
        return combine_mode != rhs.combine_mode ? combine_mode < rhs.combine_mode : options < rhs.options;
    }
};
struct ColorCombiner {
    ColorCombinerKey key;
    unsigned serial;
};
struct TextureCacheKey {
    const uint8_t* texture_addr;
    uint8_t fmt, siz, palette_index;
    uint32_t size_bytes;
    bool operator==(const TextureCacheKey& rhs) const {
        return texture_addr == rhs.texture_addr && fmt == rhs.fmt && siz == rhs.siz &&
               palette_index == rhs.palette_index && size_bytes == rhs.size_bytes;
    }
    struct Hasher {
        size_t operator()(const TextureCacheKey& key) const {
            return (size_t)key.texture_addr ^ key.size_bytes ^ (key.fmt << 8);
        }
    };
};
typedef std::unordered_map<TextureCacheKey, struct TextureCacheValue, TextureCacheKey::Hasher> TextureCacheMap;
typedef std::pair<const TextureCacheKey, struct TextureCacheValue> TextureCacheNode;
struct TextureCacheValue {
    unsigned id;
    std::list<struct TextureCacheMapIter>::iterator lru_location;
};
struct TextureCacheMapIter {
    TextureCacheMap::iterator it;
};
struct TextureCache {
    TextureCacheMap map;
    std::list<TextureCacheMapIter> lru;
};
struct Backend {
    void Init() {}
};
struct XboxTrianglePipelineTraits {
    template <class T> static bool ShaderPresent(const T& o) { return !o.mShaderStack.empty(); }
    template <class T> static int64_t ShaderIdentity(const T& o) { return o.mShaderStack.back(); }
};
struct Decoded {
    ColorCombinerKey key;
    unsigned serial;
    bool alpha, fog, grayscale;
    uint64_t tail;
};
static unsigned sMisses;

namespace Ref {
// REFERENCE
}
namespace Fast {
// PROJECTED
}

static uint32_t sRandom = 0x2545f491;
static uint32_t Next() {
    sRandom ^= sRandom << 13;
    sRandom ^= sRandom >> 17;
    sRandom ^= sRandom << 5;
    return sRandom;
}
static uint8_t sImages[4][16];

static RDP RandomState() {
    RDP state = {};
    state.combine_mode = Next() % 3 == 0 ? 0x1234567800000000ull | (Next() % 3) : Next() % 3;
    state.other_mode_l = (Next() % 4) | ((Next() % 4) << 30) | ((Next() % 2) << 8);
    state.other_mode_h = Next() % 2;
    state.grayscale = Next() % 2 == 0;
    for (unsigned i = 0; i < 2; ++i) {
        state.loaded_texture[i].masked = Next() % 4 == 0;
        state.loaded_texture[i].blended = Next() % 4 == 0;
    }
    return state;
}

template <class A, class B> static bool Step(A& a, B& b) {
    // Materials recur, as in display-list replays; one draw in eight is new.
    static RDP materials[12];
    static bool ready;
    if (!ready) {
        for (unsigned i = 0; i < 12; ++i)
            materials[i] = RandomState();
        ready = true;
    }
    const unsigned action = Next() % 16;
    if (action < 6) {
        const RDP state = Next() % 8 ? materials[Next() % 12] : RandomState();
        *a.mRdp = state;
        *b.mRdp = state;
        if (Next() % 5 == 0) {
            a.mShaderStack.clear();
            b.mShaderStack.clear();
            if (Next() % 2) {
                const int shader = (int)(Next() % 3);
                a.mShaderStack.push_back(shader);
                b.mShaderStack.push_back(shader);
            }
        }
        Decoded x = {}, y = {};
        a.Decode(x);
        b.Decode(y);
        // Equal serials prove the projected memo never kept a combiner the
        // pool no longer holds (it would skip a creation the original makes).
        return x.key.combine_mode == y.key.combine_mode && x.key.options == y.key.options &&
               x.serial == y.serial && x.alpha == y.alpha && x.fog == y.fog && x.grayscale == y.grayscale &&
               x.tail == y.tail && a.mColorCombinerPool.size() == b.mColorCombinerPool.size();
    }
    if (action < 13) {
        TextureCacheKey key = { sImages[Next() % 4], (uint8_t)(Next() % 2), 0, (uint8_t)(Next() % 2), 16 };
        const TextureCacheNode* x = 0;
        const TextureCacheNode* y = 0;
        const bool hitA = a.TextureCacheLookup(key, &x);
        const bool hitB = b.TextureCacheLookup(key, &y);
        return hitA == hitB && x->second.id == y->second.id && x->first == key && y->first == key &&
               a.mTextureCache.map.size() == b.mTextureCache.map.size();
    }
    if (action == 13) {
        TextureCacheKey key = { sImages[Next() % 4], (uint8_t)(Next() % 2), 0, (uint8_t)(Next() % 2), 16 };
        a.Delete(key);
        b.Delete(key);
    } else if (action == 14 && Next() % 32 == 0) {
        const unsigned which = Next() % 3;
        if (which == 0) { a.TextureCacheClear(); b.TextureCacheClear(); }
        if (which == 1) { a.ClearCombiners(); b.ClearCombiners(); }
        if (which == 2) { a.Reinit(); b.Reinit(); }
    }
    return true;
}

int main() {
    alignas(Fast::Interpreter) static unsigned char storage[sizeof(Fast::Interpreter)];
    unsigned steps = 0;
    for (unsigned owner = 0; owner < 4; ++owner) {
        // Each owner reuses the same address: memo entries of a destroyed
        // interpreter must never answer for its successor.
        Ref::Interpreter reference;
        Fast::Interpreter* projected = new (storage) Fast::Interpreter();
        for (unsigned i = 0; i < 40000; ++i, ++steps) {
            if (!Step(reference, *projected)) {
                std::printf("FAIL owner %u step %u\n", owner, i);
                return 1;
            }
        }
        projected->~Interpreter();
    }
    if (sMisses == 0 || sMisses > steps / 4) {
        std::printf("FAIL memo misses %u of %u steps\n", sMisses, steps);
        return 2;
    }
    std::printf("PASS material replay matches the original over %u steps (%u mode misses)\n", steps, sMisses);
    return 0;
}
