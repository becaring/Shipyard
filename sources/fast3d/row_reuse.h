#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "fast3d/indexed_submission.h"

namespace Shipyard {
namespace Fast3D {

// Per-slot x/w and y/w for one TRI2 run. Within a run no vertex is loaded and
// no state changes; Flush and reconciliation also invalidate, so a cached
// quotient never crosses a renderer call or a rounding-mode change.
template <unsigned Slots> struct TriangleNdcCache {
    struct Entry {
        unsigned generation;
        float x, y;
    };
    Entry entries[Slots];
    unsigned generation;

    void Reset() {
        memset(entries, 0, sizeof(entries));
        generation = 1;
    }
    __forceinline void Invalidate() {
        if (++generation == 0)
            Reset();
    }
    template <class Vertex> __forceinline void Load(unsigned slot, const Vertex& vertex, float& x, float& y) {
        Entry& entry = entries[slot & (Slots - 1)];
        if (entry.generation != generation) {
            entry.x = vertex.x / vertex.w;
            entry.y = vertex.y / vertex.w;
            entry.generation = generation;
        }
        x = entry.x;
        y = entry.y;
    }
};

// Packed row per vertex address. Pointer tags make collisions harmless:
// an evicted vertex packs a second, identical row.
struct TriangleRowTable {
    struct Entry {
        const void* vertex;
        unsigned generation;
        uint16_t row;
    };
    Entry entries[128];
    unsigned generation;

    void Reset() {
        memset(entries, 0, sizeof(entries));
        generation = 1;
    }
    __forceinline void Invalidate() {
        if (++generation == 0)
            Reset();
    }
    // Returns true with the stored row, else records `next` for the vertex.
    __forceinline bool Find(const void* vertex, size_t size, uint16_t next, uint16_t& row) {
        Entry& entry = entries[(uintptr_t(vertex) / size) & 127];
        if (entry.generation == generation && entry.vertex == vertex) {
            row = entry.row;
            return true;
        }
        entry.vertex = vertex;
        entry.generation = generation;
        entry.row = next;
        row = next;
        return false;
    }
};

// One batch of packed rows plus uint16 corner indices. Rows are shared only
// inside a TRI2 run, after the last Flush and reconciliation, and never for
// rectangles or pipelines with per-triangle inputs (LOD fraction reads the
// triangle's first w). Without sharing the indices are 0..3n-1, so Flush
// submits the unchanged flat stream.
template <unsigned MaxTriangles> struct TriangleRows {
    uint16_t indices[MaxTriangles * 3];
    size_t rows;
    size_t stride;
    size_t next;
    bool run;
    bool perTriangle;
    TriangleNdcCache<128> ndc;
    TriangleRowTable table;

    void Reset() {
        rows = stride = next = 0;
        run = false;
        perTriangle = true;
        ndc.Reset();
        table.Reset();
    }
    __forceinline void Invalidate() {
        ndc.Invalidate();
        table.Invalidate();
    }
    // Reconciliation publishes the pipeline's row layout.
    __forceinline void Prepare(size_t floats, bool triangleInputs) {
        next = floats;
        perTriangle = triangleInputs;
        Invalidate();
    }
    __forceinline void Begin(bool enabled) {
        run = enabled;
        if (enabled)
            Invalidate();
    }
    __forceinline void End() {
        run = false;
    }
    // After the submission; the next batch starts empty.
    __forceinline void Clear() {
        rows = stride = 0;
        Invalidate();
    }
    __forceinline bool Indexed(size_t triangles) const {
#if defined(SHIPYARD_XENOS_INDEXED_TRIANGLES)
        return rows < 3 * triangles;
#else
        (void)triangles;
        return false;
#endif
    }
    // Submits an Indexed() batch: the packed rows and every corner's index.
    void Draw(const float* vertices, size_t floats, size_t triangles) const {
#if defined(SHIPYARD_XENOS_INDEXED_TRIANGLES) && defined(_XBOX)
        ::Fast::ShipyardXenos::DrawIndexedTriangles(vertices, floats, rows, indices, triangles);
#else
        (void)vertices;
        (void)floats;
        (void)triangles;
#endif
    }
    // Assigns this triangle's corner rows. `pack` receives the vertices that
    // need new rows, in corner order, padded with the first for fixed-lane
    // packers. Returns their count; zero means every corner is cached.
    template <class Vertex>
    __forceinline unsigned Plan(Vertex* const corners[3], Vertex* pack[3], size_t triangle, bool shareable) {
        uint16_t* const out = indices + 3 * triangle;
#if defined(SHIPYARD_XENOS_INDEXED_TRIANGLES)
        const bool share = shareable && run && !perTriangle;
#else
        const bool share = false;
        (void)shareable;
#endif
        unsigned count = 0;
        for (unsigned corner = 0; corner < 3; ++corner) {
            const uint16_t row = uint16_t(rows + count);
            if (share && table.Find(corners[corner], sizeof(Vertex), row, out[corner]))
                continue;
            out[corner] = row;
            pack[count++] = corners[corner];
        }
        for (unsigned lane = count; count && lane < 3; ++lane)
            pack[lane] = pack[0];
        rows += count;
        return count;
    }
};

} // namespace Fast3D
} // namespace Shipyard
