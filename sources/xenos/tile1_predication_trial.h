#pragma once

#include <stddef.h>
#include "tile_coverage.h"

namespace Fast { namespace ShipyardXenos {

struct Tile1PredicationTrialCounters {
    unsigned candidates, eligible, applied;
    unsigned targetRejected, viewportRejected, geometryRejected;
};

// Render-thread-only, off by default. Manually predicates certified batches
// to tile 1 on a 1920x1080, two-tile, single-sample target.
void SetTile1PredicationTrialEnabled(bool enabled);

// The caller supplies an exact framebuffer/tile/sample eligibility result.
// Shader VSMain passes the first float4 through unchanged as clip position.
inline bool ClassifyTile1Predication(bool exactTarget, const float* vertices,
                                    size_t vertexCount, size_t stride,
                                    unsigned framebufferHeight, unsigned viewportY,
                                    unsigned viewportHeight, unsigned splitY,
                                    Tile1PredicationTrialCounters* counters) {
    if (counters) ++counters->candidates;
    if (!exactTarget) {
        if (counters) ++counters->targetRejected;
        return false;
    }
    if (viewportY > framebufferHeight || viewportHeight > framebufferHeight - viewportY) {
        if (counters) ++counters->viewportRejected;
        return false;
    }
    if (!BatchInsideSecondTile(vertices, vertexCount, stride, viewportY, viewportHeight, splitY)) {
        if (counters) ++counters->geometryRejected;
        return false;
    }
    if (counters) ++counters->eligible;
    return true;
}

// Predication affects Draw and Gpuxxx calls; ordinary D3D state is shared by
// all tiles. Leave state preparation outside this scope and restore automatic
// predication before handling a Draw failure.
template <typename Device>
inline HRESULT SubmitTile1PredicatedDraw(Device* device, bool tile1Only,
                                        unsigned triangles, const float* vertices,
                                        unsigned stride, Tile1PredicationTrialCounters* counters) {
    if (tile1Only) {
        device->SetPredication(D3DPRED_TILE(1));
        if (counters) ++counters->applied;
    }
    const HRESULT result = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, vertices, stride);
    if (tile1Only) device->SetPredication(0);
    return result;
}

}} // namespace Fast::ShipyardXenos
