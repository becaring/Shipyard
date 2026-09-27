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

}} // namespace Fast::ShipyardXenos
