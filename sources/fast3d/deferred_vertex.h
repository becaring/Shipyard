#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "fast3d/numeric.h"

namespace Shipyard {
namespace Fast3D {

// Directional-lighting, fog and texture-scale inputs of a vertex load, kept
// until a triangle that survives culling (or a ModifyVertex) reads the
// attributes. Positions and clip codes are published at load time. Identical
// inputs share one snapshot across loads; each snapshot has a unique serial.
template <unsigned MaxLights> struct DeferredLighting {
    float coeff[MaxLights][3];
    float color[MaxLights][3];
    float ambientFloat[3];
    uint8_t raw[MaxLights + 1][3];
    unsigned count;
    unsigned references;
    uint32_t serial;
    uint32_t fogKey;
    float fogMul, fogOffset;
    uint16_t scaleS, scaleT;
    bool fog;
    // |normal| <= 128 and |coeff| <= 16 bound each intensity below 49: the
    // truncated float sum then equals the int compound assignment.
    bool floatLighting;
    // Rounding mode at load, where the eager path divides by 127.
    bool nearest;
    // Exactly negated final light pair: at most one of the two contributes.
    bool opposedLastPair;
    uint32_t normalKey;
    uint8_t normalRgb[3];

    template <class Light>
    bool Matches(const Light* lights, unsigned lights1, const float (*coeffs)[3], bool fogOn,
                 uint16_t s, uint16_t t, uint32_t fogInputs) const {
        if (count + 1 != lights1 || fog != fogOn || scaleS != s || scaleT != t || fogKey != fogInputs)
            return false;
        for (unsigned i = 0; i <= count; ++i)
            if (memcmp(raw[i], lights[i].l.col, 3))
                return false;
        return !memcmp(coeff, coeffs, count * sizeof(coeff[0]));
    }

    template <class Light>
    void Capture(const Light* lights, unsigned lights1, const float (*coeffs)[3], bool fogOn,
                 uint16_t s, uint16_t t, int16_t mul, int16_t offset, bool nearestMode, uint32_t id) {
        count = lights1 - 1;
        floatLighting = true;
        for (unsigned i = 0; i <= count; ++i)
            memcpy(raw[i], lights[i].l.col, 3);
        for (unsigned i = 0; i < count; ++i)
            for (unsigned c = 0; c < 3; ++c) {
                coeff[i][c] = coeffs[i][c];
                color[i][c] = LightByteFloat(raw[i][c]);
                if (!(coeff[i][c] >= -16.0f && coeff[i][c] <= 16.0f))
                    floatLighting = false;
            }
        for (unsigned c = 0; c < 3; ++c)
            ambientFloat[c] = LightByteFloat(raw[count][c]);
        fog = fogOn;
        scaleS = s;
        scaleT = t;
        fogKey = (uint32_t(uint16_t(mul)) << 16) | uint16_t(offset);
        fogMul = SignedShortFloat(mul);
        fogOffset = SignedShortFloat(offset);
        nearest = nearestMode;
        serial = id;
        normalKey = 0xffffffffu;
        opposedLastPair = nearest && floatLighting && count >= 2;
        for (unsigned c = 0; opposedLastPair && c < 3; ++c) {
            uint32_t a, b;
            memcpy(&a, &coeff[count - 2][c], sizeof(a));
            memcpy(&b, &coeff[count - 1][c], sizeof(b));
            if ((a ^ b) != 0x80000000u)
                opposedLastPair = false;
        }
    }
};

// Lit RGB by (snapshot, normal); only nearest-rounding snapshots use it.
struct DeferredNormalCache {
    struct Entry {
        const void* owner;
        uint32_t serial, normal;
        uint8_t rgb[3];
    };
    Entry entries[64];
    bool enabled;
};

template <class Source, unsigned Slots, unsigned MaxLights> struct DeferredVertices {
    typedef DeferredLighting<MaxLights> Context;
    struct Pending {
        Source source;
        Context* context;
    };
    Pending pending[Slots];
    Context contexts[Slots];
    Context* current;
    DeferredNormalCache normals;
    uint32_t serial;
    bool enabled;

    void Reset() {
        memset(pending, 0, sizeof(pending));
        memset(contexts, 0, sizeof(contexts));
        memset(&normals, 0, sizeof(normals));
        current = NULL;
        serial = 0;
        enabled = false;
    }
    // Sampled once per Run: bit 0 defers lit loads, bit 1 enables the normal cache.
    unsigned Configure(unsigned control) {
        enabled = (control & 1) != 0;
        normals.enabled = (control & 2) != 0;
        return (enabled ? 1u : 0u) | (enabled && normals.enabled ? 2u : 0u);
    }
    __forceinline bool Waiting(size_t index) const {
        return index < Slots && pending[index].context;
    }
    __forceinline void Discard(size_t index) {
        Pending& slot = pending[index];
        if (slot.context) {
            --slot.context->references;
            slot.context = NULL;
        }
    }
    // Slots the eager load overwrites; indices past Slots are never pending.
    void Discard(size_t first, size_t count) {
        for (size_t i = first; i < Slots && i - first < count; ++i)
            Discard(i);
    }

    // The snapshot for the current inputs, or NULL when none is free.
    // Coefficients must already be refreshed from lights_changed.
    template <class Light>
    Context* Snapshot(const Light* lights, unsigned lights1, const float (*coeffs)[3], bool fogOn,
                      uint16_t s, uint16_t t, int16_t mul, int16_t offset, bool nearestMode) {
        const uint32_t fogInputs = (uint32_t(uint16_t(mul)) << 16) | uint16_t(offset);
        if (current && current->nearest == nearestMode &&
            current->Matches(lights, lights1, coeffs, fogOn, s, t, fogInputs))
            return current;
        Context* context = NULL;
        for (unsigned i = 0; i < Slots && !context; ++i)
            if (!contexts[i].references)
                context = &contexts[i];
        if (!context)
            return NULL;
        if (!++serial) {
            memset(normals.entries, 0, sizeof(normals.entries));
            serial = 1;
        }
        context->Capture(lights, lights1, coeffs, fogOn, s, t, mul, offset, nearestMode, serial);
        current = context;
        return context;
    }
    void Commit(const Source* vertices, size_t count, size_t first, Context* context) {
        for (size_t i = 0; i < count; ++i) {
            pending[first + i].source = vertices[i];
            pending[first + i].context = context;
        }
        context->references += (unsigned)count;
    }

    template <class Loaded> __forceinline void Materialize(size_t index, Loaded* loaded) {
        if (Waiting(index))
            Publish(index, loaded[index]);
    }
    template <class Loaded> void MaterializeAll(Loaded* loaded) {
        for (size_t i = 0; i < Slots; ++i)
            Materialize(i, loaded);
    }

    // The eager load's u/v and color, from the snapshot. Expression forms
    // match the consumers' eager loops operation for operation.
    template <class Loaded> __declspec(noinline) void Publish(size_t index, Loaded& d) {
        Context* c = pending[index].context;
        const Source& source = pending[index].source;
        short U = source.v.tc[0] * c->scaleS >> 16;
        short V = source.v.tc[1] * c->scaleT >> 16;
        d.u = SignedShortFloat(U);
        d.v = SignedShortFloat(V);
        DeferredVertexBytes bytes;
        if (c->fog) {
            float w = d.w;
            if (fabsf(w) < 0.001f)
                w = 0.001f;
            float winv = 1.0f / w;
            if (winv < 0.0f)
                winv = 32767;
            float fog_z = d.z * winv * c->fogMul + c->fogOffset;
            const float t = fog_z < 0.0f ? 0.0f : fog_z;
            fog_z = t > 255.0f ? 255.0f : t;
            bytes.Store(3, fog_z);
        }
        const int8_t* n = source.n.n;
        if (c->floatLighting) {
            const uint32_t normalKey =
                (uint32_t(uint8_t(n[0])) << 16) | (uint32_t(uint8_t(n[1])) << 8) | uint8_t(n[2]);
            bool reuse = normalKey == c->normalKey;
            DeferredNormalCache::Entry* entry = NULL;
            if (!reuse && normals.enabled && c->nearest) {
                entry = &normals.entries[(normalKey * 2654435761u) >> 26];
                if (entry->owner == c && entry->serial == c->serial && entry->normal == normalKey) {
                    memcpy(c->normalRgb, entry->rgb, 3);
                    c->normalKey = normalKey;
                    reuse = true;
                }
            }
            if (!reuse) {
                float r = c->ambientFloat[0], g = c->ambientFloat[1], b = c->ambientFloat[2];
                const float normal[3] = {NormalByteFloat(n[0]), NormalByteFloat(n[1]), NormalByteFloat(n[2])};
                for (int i = 0; i < (int)c->count; ++i) {
                    float intensity = 0;
                    intensity += normal[0] * c->coeff[i][0];
                    intensity += normal[1] * c->coeff[i][1];
                    intensity += normal[2] * c->coeff[i][2];
                    intensity = DivideNormal127(intensity, c->nearest);
                    int colorLight = i;
                    // Bounded finite coefficients put the quotient inside (-49,49).
                    if (c->opposedLastPair && i + 2 == (int)c->count && intensity != 0.0f &&
                        intensity > -49.0f && intensity < 49.0f) {
                        if (intensity < 0.0f) {
                            intensity = -intensity;
                            colorLight = i + 1;
                        }
                        ++i;
                    }
                    if (intensity > 0.0f) {
                        r = TruncateLightChannel(r + intensity * c->color[colorLight][0]);
                        g = TruncateLightChannel(g + intensity * c->color[colorLight][1]);
                        b = TruncateLightChannel(b + intensity * c->color[colorLight][2]);
                    }
                }
                bytes.Store(0, VertexColorLimit(r));
                bytes.Store(1, VertexColorLimit(g));
                bytes.Store(2, VertexColorLimit(b));
                c->normalRgb[0] = bytes.Load(0);
                c->normalRgb[1] = bytes.Load(1);
                c->normalRgb[2] = bytes.Load(2);
                c->normalKey = normalKey;
                if (entry) {
                    entry->owner = c;
                    entry->serial = c->serial;
                    entry->normal = normalKey;
                    memcpy(entry->rgb, c->normalRgb, 3);
                }
            }
            d.color.r = c->normalRgb[0];
            d.color.g = c->normalRgb[1];
            d.color.b = c->normalRgb[2];
        } else {
            int r = c->raw[c->count][0], g = c->raw[c->count][1], b = c->raw[c->count][2];
            const float normal[3] = {NormalByteFloat(n[0]), NormalByteFloat(n[1]), NormalByteFloat(n[2])};
            for (unsigned i = 0; i < c->count; ++i) {
                float intensity = 0;
                intensity += normal[0] * c->coeff[i][0];
                intensity += normal[1] * c->coeff[i][1];
                intensity += normal[2] * c->coeff[i][2];
                intensity = DivideNormal127(intensity, c->nearest);
                if (intensity > 0.0f) {
                    r += intensity * c->color[i][0];
                    g += intensity * c->color[i][1];
                    b += intensity * c->color[i][2];
                }
            }
            d.color.r = r > 255 ? 255 : r;
            d.color.g = g > 255 ? 255 : g;
            d.color.b = b > 255 ? 255 : b;
        }
        d.color.a = c->fog ? bytes.Load(3) : source.v.cn[3];
        Discard(index);
    }
};

} // namespace Fast3D
} // namespace Shipyard
