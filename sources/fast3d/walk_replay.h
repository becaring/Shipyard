#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>

namespace Shipyard {
namespace Fast3D {

// One renderer call of a recorded command-list walk. Replays reissue the
// calls in order; a draw reads the shared row stream.
struct WalkOp {
    enum Kind {
        UnloadShader, LoadShader, SelectTexture, SelectTextureFb, Sampler, Depth, Decal, Viewport,
        Scissor, Alpha, PrimDepth, Framebuffer, StartFramebuffer, CopyFramebuffer, ClearFramebuffer,
        ClearDepthRegion, ResolveMsaa, Draw
    };
    Kind kind;
    void* pointer;
    float value;
    int args[10];
    // Draw: stream offset and length in floats, triangles, index offset, indexed rows.
    size_t at, floats, triangles, indices, rows;
};

// Rows of the stream whose clip position (floats 0-3) and texture coordinate
// pairs (at uv offsets, 0 for none) differ per replay.
struct WalkPatch {
    size_t at;
    unsigned rows, stride;
    bool position;
    unsigned uv[2];
};

// Reasons a walk cannot stand in for its batch's later presentations.
enum WalkTaint {
    WalkClean, WalkUnsupportedCall, WalkTextureRewrite, WalkVertexSource, WalkBranchDiverged,
    WalkTileResized, WalkSegmentInterpolation, WalkThrown, WalkTaintCount
};

// The walk of an interpolation batch's first presentation, kept for the
// batch's other presentations. Each of those differs only in its matrices
// and lerped tile offsets: the walk keeps the first presentation's rows once
// and, per replay, only the values those change. A replay writes its values
// into the rows before drawing them. Anything a replay cannot reproduce
// taints it, and those presentations walk the list themselves.
template <unsigned MaxReplays> class WalkLog {
  public:
    std::vector<WalkOp> ops;
    std::vector<float> rows;
    std::vector<WalkPatch> patches;
    std::vector<float> values[MaxReplays];
    std::vector<uint16_t> indices;
    const void* commands;
    unsigned replays;
    unsigned taint;
    bool ready;

    WalkLog() : commands(0), replays(0), taint(WalkClean), ready(false), draws(0), upload(~0u), generation(0) {}

    void Begin(const void* list, unsigned count) {
        ops.clear();
        rows.clear();
        patches.clear();
        for (unsigned i = 0; i < MaxReplays; ++i)
            values[i].clear();
        indices.clear();
        commands = list;
        replays = count;
        taint = WalkClean;
        ready = false;
        draws = 0;
        upload = ~0u;
        if (++generation == 0) {
            stamps.clear();
            generation = 1;
        }
    }
    void Taint(unsigned reason) {
        if (taint == WalkClean)
            taint = reason;
    }
    // The walk finished; replays may use it unless something tainted it.
    void Finish() {
        ready = taint == WalkClean;
    }
    bool Replays(const void* list, unsigned replay) const {
        return ready && list == commands && replay < replays;
    }
    WalkOp& Push(WalkOp::Kind kind) {
        ops.push_back(WalkOp());
        WalkOp& op = ops.back();
        memset(&op, 0, sizeof(op));
        op.kind = kind;
        return op;
    }

    // An upload replaces a texture's pixels for every replay, so it must not
    // follow a recorded draw that may have sampled the old pixels.
    void Selected(uint32_t texture) {
        upload = texture;
        Stamp& stamp = StampFor(texture);
        if (stamp.generation != generation) {
            stamp.generation = generation;
            stamp.draws = draws;
        }
    }
    void SelectedFramebuffer() {
        upload = ~0u;
    }
    void Uploaded() {
        if (upload == ~0u) {
            Taint(WalkTextureRewrite);
            return;
        }
        Stamp& stamp = StampFor(upload);
        if (stamp.generation == generation && stamp.draws != draws)
            Taint(WalkTextureRewrite);
        stamp.generation = generation;
        stamp.draws = draws;
    }

    // Rows [at, at + count * stride) of the pending draw differ per replay;
    // each replay then appends count rows of values with Values().
    void Patch(size_t at, unsigned count, unsigned stride, bool position, unsigned uv0, unsigned uv1) {
        const WalkPatch patch = {rows.size() + at, count, stride, position, {uv0, uv1}};
        patches.push_back(patch);
    }
    float* Values(unsigned replay, size_t floats) {
        std::vector<float>& v = values[replay];
        v.resize(v.size() + floats);
        return &v[v.size() - floats];
    }
    // The live draw's rows, which hold the first presentation's values.
    void Draw(const float* source, size_t floats, size_t triangles, const uint16_t* corners, size_t indexed) {
        WalkOp& op = Push(WalkOp::Draw);
        op.at = rows.size();
        op.floats = floats;
        op.triangles = triangles;
        op.rows = corners ? indexed : 0;
        op.indices = indices.size();
        rows.insert(rows.end(), source, source + floats);
        if (corners)
            indices.insert(indices.end(), corners, corners + 3 * triangles);
        ++draws;
    }
    // Writes one replay's values into the rows; every replay writes the same fields.
    void Apply(unsigned replay) {
        const float* v = values[replay].empty() ? 0 : &values[replay][0];
        for (size_t i = 0; i < patches.size(); ++i) {
            const WalkPatch& p = patches[i];
            // Rows of a batch that never reached a draw.
            if (p.at + (size_t)p.rows * p.stride > rows.size())
                break;
            float* row = &rows[p.at];
            for (unsigned r = 0; r < p.rows; ++r, row += p.stride) {
                if (p.position) {
                    row[0] = v[0]; row[1] = v[1]; row[2] = v[2]; row[3] = v[3];
                    v += 4;
                }
                for (int t = 0; t < 2; ++t)
                    if (p.uv[t]) {
                        row[p.uv[t]] = v[0];
                        row[p.uv[t] + 1] = v[1];
                        v += 2;
                    }
            }
        }
    }
    float* Stream(const WalkOp& op) {
        return &rows[op.at];
    }
    const uint16_t* Corners(const WalkOp& op) const {
        return op.rows ? &indices[op.indices] : 0;
    }

  private:
    struct Stamp {
        unsigned generation;
        unsigned draws;
    };
    std::vector<Stamp> stamps;
    unsigned draws;
    uint32_t upload;
    unsigned generation;

    Stamp& StampFor(uint32_t texture) {
        if (texture >= stamps.size()) {
            Stamp empty = {0, 0};
            stamps.resize(texture + 1, empty);
        }
        return stamps[texture];
    }
};

} // namespace Fast3D
} // namespace Shipyard
