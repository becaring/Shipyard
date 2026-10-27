// Shared TRI2 rows: indexed batches expand to the flat stream exactly, for
// both cursor (Majora) and fixed-lane (Smash) packers; run NDC equals x/w.
#define SHIPYARD_XENOS_INDEXED_TRIANGLES 1
#include "fast3d/row_reuse.h"
#include "xenos/indexed_draw.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

struct Vertex {
    float x, y, z, w, u, v;
    unsigned char color[4];
    short clip;
};

static const unsigned Stride = 9;
static unsigned seed = 12345;
static unsigned Next() { return seed = seed * 1103515245u + 12345u; }
static float Value() { return (float)((int)(Next() >> 8 & 0xffff) - 32768) / 97.0f; }

static void Row(float* out, const Vertex* v, float constant) {
    out[0] = v->x; out[1] = v->y; out[2] = v->z; out[3] = v->w;
    out[4] = v->u * 0.03125f; out[5] = v->v * 0.03125f;
    out[6] = v->color[0] / 255.0f; out[7] = v->color[3] / 255.0f; out[8] = constant;
}

// Smash form: three row pointers alias row 0 past the pack count, and lane
// writes fill all three before any store of a later lane reads back.
static void Columns(float* base, Vertex* const pack[3], int count, float constant) {
    float* rows[3] = {base, base + (count > 1 ? Stride : 0), base + (count > 2 ? 2 * Stride : 0)};
    for (int i = 0; i < count; ++i) {
        rows[i][0] = pack[i]->x; rows[i][1] = pack[i]->y; rows[i][2] = pack[i]->z; rows[i][3] = pack[i]->w;
    }
    const float a = pack[0]->u * 0.03125f, b = pack[1]->u * 0.03125f, c = pack[2]->u * 0.03125f;
    rows[0][4] = a; rows[1][4] = b; rows[2][4] = c;
    const float d = pack[0]->v * 0.03125f, e = pack[1]->v * 0.03125f, f = pack[2]->v * 0.03125f;
    rows[0][5] = d; rows[1][5] = e; rows[2][5] = f;
    for (int i = 0; i < count; ++i) {
        rows[i][6] = pack[i]->color[0] / 255.0f; rows[i][7] = pack[i]->color[3] / 255.0f; rows[i][8] = constant;
    }
}

static int failures;
static void Check(bool ok, const char* what, unsigned at) {
    if (!ok && ++failures < 10) printf("FAIL %s at %u\n", what, at);
}

static Shipyard::Fast3D::TriangleRows<256> rows;

static void Scenario(bool columns, unsigned& shared, unsigned& batches) {
    Vertex vertices[64];
    memset(vertices, 0, sizeof(vertices));
    for (unsigned i = 0; i < 64; ++i) {
        Vertex& v = vertices[i];
        v.x = Value(); v.y = Value(); v.z = Value(); v.w = Value() + 0.5f;
        v.u = Value(); v.v = Value();
        for (unsigned c = 0; c < 4; ++c) v.color[c] = (unsigned char)Next();
    }
    std::vector<float> buffer(256 * 32 * 3), flat(256 * 3 * Stride);
    size_t length = 0, triangles = 0;
    rows.Reset();
    rows.Prepare(Stride, false);
    float constant = 1.0f;
    auto flush = [&]() {
        if (length) {
            ++batches;
            Check(length == rows.rows * Stride, "row count", (unsigned)triangles);
            if (rows.Indexed(triangles)) {
                ++shared;
                Check(Fast::ShipyardXenos::ValidIndexedTriangleInput(buffer.data(), length, rows.rows,
                      rows.indices, triangles, Stride), "valid indexed input", (unsigned)triangles);
            } else {
                Check(rows.rows == 3 * triangles, "identity rows", (unsigned)triangles);
                for (size_t i = 0; i < 3 * triangles; ++i)
                    Check(rows.indices[i] == i, "identity index", (unsigned)i);
            }
            for (size_t i = 0; i < 3 * triangles; ++i)
                Check(memcmp(&buffer[rows.indices[i] * Stride], &flat[i * Stride], Stride * sizeof(float)) == 0,
                      "expanded row", (unsigned)i);
        }
        length = triangles = 0;
        rows.Clear();
    };
    for (unsigned run = 0; run < 400; ++run) {
        const unsigned kind = Next() % 8;
        if (kind == 0) {
            // Reconciliation: new constants and possibly a per-triangle layout.
            flush();
            constant = Value();
            rows.Prepare(Stride, Next() % 4 == 0);
        } else if (kind == 1) {
            // Mid-run reconciliation without a flush (key miss, same shader).
            constant = Value();
            rows.Invalidate();
        }
        rows.Begin(Next() % 5 != 0);
        const unsigned count = 1 + Next() % 24, window = 4 + Next() % 28, origin = Next() % (64 - window);
        for (unsigned t = 0; t < count; ++t) {
            Vertex* corners[3];
            for (unsigned c = 0; c < 3; ++c) corners[c] = &vertices[origin + Next() % window];
            const bool rect = Next() % 16 == 0;
            for (unsigned c = 0; c < 3; ++c) Row(&flat[(3 * triangles + c) * Stride], corners[c], constant);
            Vertex* pack[3];
            const int packed = (int)rows.Plan(corners, pack, triangles, !rect);
            Check(packed >= 0 && packed <= 3, "pack count", t);
            if (packed) {
                if (columns) {
                    Columns(&buffer[length], pack, packed, constant);
                } else {
                    for (int i = 0; i < packed; ++i) Row(&buffer[length + i * Stride], pack[i], constant);
                }
                length += packed * Stride;
            }
            if (++triangles == 256) flush();
        }
        rows.End();
        if (Next() % 3 == 0) flush();
    }
    flush();
}

int main() {
    unsigned shared = 0, batches = 0;
    Scenario(false, shared, batches);
    Scenario(true, shared, batches);
    Check(shared > 0 && shared < batches, "both submission forms exercised", shared);

    // Run NDC equals the direct quotient under every rounding mode, and a
    // generation bump recomputes after a mode change.
    static const unsigned modes[4] = {_RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP};
    Shipyard::Fast3D::TriangleNdcCache<128> ndc;
    ndc.Reset();
    unsigned checked = 0;
    for (unsigned m = 0; m < 4; ++m) {
        unsigned previous;
        _controlfp_s(&previous, modes[m], _MCW_RC);
        ndc.Invalidate();
        for (unsigned i = 0; i < 20000; ++i) {
            Vertex v;
            v.x = Value(); v.y = Value(); v.w = Value() * 0.37f + 0.001f;
            float x, y;
            const volatile float vx = v.x, vy = v.y, vw = v.w;
            ndc.Invalidate();
            ndc.Load(i, v, x, y);
            Check(x == vx / vw && y == vy / vw, "ndc quotient", i);
            float cx, cy;
            ndc.Load(i, v, cx, cy);
            Check(memcmp(&cx, &x, 4) == 0 && memcmp(&cy, &y, 4) == 0, "ndc reuse", i);
            ++checked;
        }
        _controlfp_s(&previous, _RC_NEAR, _MCW_RC);
    }
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("PASS %u batches (%u indexed), %u ndc cases\n", batches, shared, checked);
    return 0;
}
