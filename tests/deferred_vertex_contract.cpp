// Deferred lit/fog vertices publish the eager loops' exact u/v/color bytes:
// Majora's (bounded float or int lighting) and BattleShip's (float until an
// intensity reaches 1024, finite fog reciprocal) under all four rounding
// modes, with snapshot reuse, the normal cache and opposed light pairs.
// Also: exact light-direction /127 and the aspect reciprocal.
#include "fast3d/deferred_vertex.h"
#include "fast3d/aspect_reciprocal.h"
#include "fast3d/vertex_transform.h"
#include <float.h>
#include <stdio.h>
#include <string.h>

typedef struct { short ob[3]; unsigned short flag; short tc[2]; unsigned char cn[4]; } F3DVtx_t;
typedef struct { short ob[3]; unsigned short flag; short tc[2]; signed char n[3]; unsigned char a; } F3DVtx_tn;
typedef union { F3DVtx_t v; F3DVtx_tn n; long long align; } F3DVtx;
struct RGBA { uint8_t r, g, b, a; };
struct LoadedVertex { float x, y, z, w, u, v; RGBA color; uint8_t clip_rej; };
struct Light { struct { uint8_t col[3]; uint8_t pad; int8_t dir[3]; } l; };

static const unsigned MaxLights = 8;
struct State {
    Light lights[MaxLights + 1];
    float coeffs[MaxLights][3];
    unsigned lights1;
    bool fog;
    uint16_t s, t;
    int16_t mul, offset;
};

static unsigned seed = 777;
static unsigned Next() { return seed = seed * 1664525u + 1013904223u; }
static float Unit() { return (float)(int)(Next() >> 9 & 0xffff) / 32768.0f - 1.0f; }

static float Clamp(float d, float lo, float hi) {
    const float t = d < lo ? lo : d;
    return t > hi ? hi : t;
}

// Majora: GfxSpVertex with a position batch (float lighting when bounded).
static void Majora(LoadedVertex* d, const F3DVtx& source, const State& st) {
    using namespace Shipyard::Fast3D;
    const F3DVtx_t* v = &source.v;
    const F3DVtx_tn* vn = &source.n;
    const bool nearest = NearestRounding();
    short U = v->tc[0] * st.s >> 16;
    short V = v->tc[1] * st.t >> 16;
    const int count = (int)st.lights1 - 1;
    int r = st.lights[count].l.col[0], g = st.lights[count].l.col[1], b = st.lights[count].l.col[2];
    bool floatLighting = true;
    for (int light = 0; light < count; ++light)
        for (int axis = 0; axis < 3; ++axis)
            if (!(st.coeffs[light][axis] >= -16.0f && st.coeffs[light][axis] <= 16.0f))
                floatLighting = false;
    float R = 0, G = 0, B = 0;
    if (floatLighting) {
        R = LightByteFloat(st.lights[count].l.col[0]);
        G = LightByteFloat(st.lights[count].l.col[1]);
        B = LightByteFloat(st.lights[count].l.col[2]);
    }
    const float normal[3] = {NormalByteFloat(vn->n[0]), NormalByteFloat(vn->n[1]), NormalByteFloat(vn->n[2])};
    for (int i = 0; i < count; i++) {
        float intensity = 0;
        intensity += normal[0] * st.coeffs[i][0];
        intensity += normal[1] * st.coeffs[i][1];
        intensity += normal[2] * st.coeffs[i][2];
        intensity = DivideNormal127(intensity, nearest);
        if (intensity > 0.0f) {
            const float c[3] = {LightByteFloat(st.lights[i].l.col[0]), LightByteFloat(st.lights[i].l.col[1]),
                                LightByteFloat(st.lights[i].l.col[2])};
            if (floatLighting) {
                R = TruncateLightChannel(R + intensity * c[0]);
                G = TruncateLightChannel(G + intensity * c[1]);
                B = TruncateLightChannel(B + intensity * c[2]);
            } else {
                r += intensity * c[0];
                g += intensity * c[1];
                b += intensity * c[2];
            }
        }
    }
    DeferredVertexBytes bytes;
    if (floatLighting) {
        bytes.Store(0, VertexColorLimit(R));
        bytes.Store(1, VertexColorLimit(G));
        bytes.Store(2, VertexColorLimit(B));
    } else {
        d->color.r = r > 255 ? 255 : r;
        d->color.g = g > 255 ? 255 : g;
        d->color.b = b > 255 ? 255 : b;
    }
    d->u = SignedShortFloat(U);
    d->v = SignedShortFloat(V);
    float z = d->z, w = d->w;
    if (st.fog) {
        if (fabsf(w) < 0.001f)
            w = 0.001f;
        float winv = 1.0f / w;
        if (winv < 0.0f)
            winv = 32767;
        float fog_z = z * winv * SignedShortFloat(st.mul) + SignedShortFloat(st.offset);
        fog_z = Clamp(fog_z, 0.0f, 255.0f);
        bytes.Store(3, fog_z);
        d->color.a = bytes.Load(3);
    } else {
        d->color.a = v->cn[3];
    }
    if (floatLighting) {
        d->color.r = bytes.Load(0);
        d->color.g = bytes.Load(1);
        d->color.b = bytes.Load(2);
    }
}

// BattleShip: float accumulation until an intensity reaches 1024.
static void BattleShip(LoadedVertex* d, const F3DVtx& source, const State& st) {
    using namespace Shipyard::Fast3D;
    const F3DVtx_t* v = &source.v;
    const F3DVtx_tn* vn = &source.n;
    const bool nearest = NearestRounding();
    short U = v->tc[0] * st.s >> 16;
    short V = v->tc[1] * st.t >> 16;
    const int count = (int)st.lights1 - 1;
    int r = st.lights[count].l.col[0], g = st.lights[count].l.col[1], b = st.lights[count].l.col[2];
    bool floatLight = true;
    float R = LightByteFloat((uint8_t)r), G = LightByteFloat((uint8_t)g), B = LightByteFloat((uint8_t)b);
    for (int i = 0; i < count; i++) {
        float intensity = 0;
        intensity += NormalByteFloat(vn->n[0]) * st.coeffs[i][0];
        intensity += NormalByteFloat(vn->n[1]) * st.coeffs[i][1];
        intensity += NormalByteFloat(vn->n[2]) * st.coeffs[i][2];
        intensity = DivideNormal127(intensity, nearest);
        if (intensity > 0.0f) {
            const float cr = LightByteFloat(st.lights[i].l.col[0]), cg = LightByteFloat(st.lights[i].l.col[1]),
                        cb = LightByteFloat(st.lights[i].l.col[2]);
            if (floatLight && intensity < 1024.0f) {
                R = TruncateLightChannel(R + intensity * cr);
                G = TruncateLightChannel(G + intensity * cg);
                B = TruncateLightChannel(B + intensity * cb);
            } else {
                if (floatLight) {
                    r = (int)R; g = (int)G; b = (int)B;
                    floatLight = false;
                }
                r += intensity * cr;
                g += intensity * cg;
                b += intensity * cb;
            }
        }
    }
    DeferredVertexBytes bytes;
    bool deferredRgb = false;
    if (floatLight) {
        bytes.Store(0, VertexColorLimit(R));
        bytes.Store(1, VertexColorLimit(G));
        bytes.Store(2, VertexColorLimit(B));
        deferredRgb = true;
    } else {
        d->color.r = r > 255 ? 255 : r;
        d->color.g = g > 255 ? 255 : g;
        d->color.b = b > 255 ? 255 : b;
    }
    d->u = SignedShortFloat(U);
    d->v = SignedShortFloat(V);
    if (st.fog) {
        const float winv = VertexFogReciprocalFinite(d->w);
        float fog_z = d->z * winv * SignedShortFloat(st.mul) + SignedShortFloat(st.offset);
        fog_z = Clamp(fog_z, 0.0f, 255.0f);
        d->color.a = (uint8_t)fog_z;
    } else {
        d->color.a = v->cn[3];
    }
    if (deferredRgb) {
        d->color.r = bytes.Load(0);
        d->color.g = bytes.Load(1);
        d->color.b = bytes.Load(2);
    }
}

typedef Shipyard::Fast3D::DeferredVertices<F3DVtx, 64, MaxLights> Deferred;
static Deferred deferred;
static int failures;
static void Check(bool ok, const char* what, unsigned at) {
    if (!ok && ++failures < 12)
        printf("FAIL %s at %u\n", what, at);
}

static void Randomize(State& st, unsigned scenario) {
    st.lights1 = 1 + Next() % (MaxLights + 1);
    for (unsigned i = 0; i <= MaxLights; ++i)
        for (unsigned c = 0; c < 3; ++c)
            st.lights[i].l.col[c] = (uint8_t)Next();
    const bool wide = scenario % 7 == 0;
    for (unsigned i = 0; i < MaxLights; ++i)
        for (unsigned c = 0; c < 3; ++c)
            st.coeffs[i][c] = wide ? Unit() * 400.0f : Unit();
    // Exactly opposed final pair, as directional rigs often set up.
    if (scenario % 3 == 0 && st.lights1 >= 3)
        for (unsigned c = 0; c < 3; ++c)
            st.coeffs[st.lights1 - 2][c] = -st.coeffs[st.lights1 - 3][c];
    st.fog = (Next() & 3) != 0;
    st.s = (uint16_t)Next();
    st.t = (uint16_t)Next();
    st.mul = (int16_t)Next();
    st.offset = (int16_t)(Next() % 2000);
}

int main() {
    using namespace Shipyard::Fast3D;
    static const unsigned modes[4] = {_RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP};
    unsigned cases = 0, reused = 0;
    for (unsigned m = 0; m < 4; ++m) {
        unsigned previous;
        _controlfp_s(&previous, modes[m], _MCW_RC);
        deferred.Reset();
        deferred.Configure(m & 1 ? 1 : 3);
        State st;
        Randomize(st, 0);
        for (unsigned scenario = 0; scenario < 3000; ++scenario) {
            if (scenario % 4 != 1) Randomize(st, scenario);
            else if (scenario % 8 == 1) st.lights[Next() % st.lights1].l.col[Next() % 3] ^= 1;
            F3DVtx vertices[16];
            const unsigned count = 1 + Next() % 16, first = Next() % (64 - count);
            for (unsigned i = 0; i < count; ++i) {
                for (unsigned c = 0; c < 2; ++c) vertices[i].v.tc[c] = (short)Next();
                // A few repeated normals exercise both reuse forms.
                const unsigned normal = Next() % 5 ? Next() : 0x12345u * (i & 1);
                for (unsigned c = 0; c < 3; ++c) vertices[i].n.n[c] = (signed char)(normal >> (8 * c));
                vertices[i].n.a = (uint8_t)Next();
            }
            Deferred::Context* previousContext = deferred.current;
            Deferred::Context* context = deferred.Snapshot(st.lights, st.lights1, st.coeffs, st.fog, st.s, st.t,
                                                            st.mul, st.offset, NearestRounding());
            Check(context != NULL, "snapshot", scenario);
            if (!context) continue;
            reused += context == previousContext;
            LoadedVertex loaded[64], majora[16], battle[16];
            memset(loaded, 0, sizeof(loaded));
            deferred.Discard(first, count);
            for (unsigned i = 0; i < count; ++i) {
                LoadedVertex& d = loaded[first + i];
                d.x = Unit() * 900.0f; d.y = Unit() * 900.0f;
                d.z = Unit() * 3000.0f;
                d.w = Next() % 9 ? Unit() * 3000.0f : Unit() * 0.002f;
                majora[i] = battle[i] = d;
            }
            deferred.Commit(vertices, count, first, context);
            for (unsigned i = 0; i < count; ++i) {
                Majora(&majora[i], vertices[i], st);
                BattleShip(&battle[i], vertices[i], st);
            }
            // Materialize in reverse order, then again (already published).
            for (unsigned k = 0; k < 2 * count; ++k)
                deferred.Materialize(first + (k < count ? count - 1 - k : k - count), loaded);
            for (unsigned i = 0; i < count; ++i) {
                Check(!deferred.Waiting(first + i), "materialized", scenario);
                Check(memcmp(&loaded[first + i], &majora[i], sizeof(LoadedVertex)) == 0, "majora bytes", scenario);
                Check(memcmp(&loaded[first + i], &battle[i], sizeof(LoadedVertex)) == 0, "battleship bytes", scenario);
                ++cases;
            }
            // Leave some vertices pending across later snapshots.
            if (scenario % 5 == 0) {
                deferred.Discard(63, 1);
                deferred.Commit(vertices, 1, 63, context);
            }
        }
        unsigned held = 0;
        for (unsigned i = 0; i < 64; ++i) held += deferred.contexts[i].references;
        Check(held == (deferred.Waiting(63) ? 1u : 0u), "reference count", m);
        _controlfp_s(&previous, _RC_NEAR, _MCW_RC);
    }

    // Light directions: the nearest product equals the division for every byte.
    for (int value = -128; value < 128; ++value) {
        const volatile float quotient = (float)value / 127.0f;
        Check(DivideNormalByte127(NormalByteFloat((int8_t)value), true) == quotient, "light direction", value + 128);
    }
    // Aspect reciprocal against division across magnitudes and aspects.
    unsigned aspects = 0;
    for (unsigned i = 0; i < 2000000; ++i) {
        uint32_t bits = Next();
        float numerator;
        memcpy(&numerator, &bits, 4);
        if (!(numerator == numerator)) continue;
        const float denominator = i % 4 ? (float)(1 + Next() % 4096) / (float)(1 + Next() % 4096) : 16.0f / 9.0f;
        const AspectReciprocal aspect(denominator);
        const volatile float expected = numerator / denominator;
        const float actual = aspect.Divide(numerator, denominator);
        Check(memcmp(&actual, (const void*)&expected, 4) == 0, "aspect reciprocal", i);
        ++aspects;
    }
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("PASS %u deferred vertices (%u reused snapshots), %u aspect quotients\n", cases, reused, aspects);
    return 0;
}
