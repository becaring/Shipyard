#pragma once

#include "tile1_predication_trial.h"
#include <stdint.h>

namespace Fast { namespace ShipyardXenos {

// XDK-only helper. Predication affects Draw and Gpuxxx calls; ordinary D3D
// state is shared by all tiles. Leave state preparation outside this scope and
// restore automatic predication before handling a Draw failure.
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

template <typename Device>
inline HRESULT SubmitTile1PredicatedIndexedDraw(Device* device, bool tile1Only,
                                               unsigned triangles, const float* vertices,
                                               unsigned vertexCount, const uint16_t* indices,
                                               unsigned stride, Tile1PredicationTrialCounters* counters) {
    if (tile1Only) {
        device->SetPredication(D3DPRED_TILE(1));
        if (counters) ++counters->applied;
    }
    const HRESULT result = device->DrawIndexedPrimitiveUP(
        D3DPT_TRIANGLELIST, 0, vertexCount, triangles, indices, D3DFMT_INDEX16, vertices, stride);
    if (tile1Only) device->SetPredication(0);
    return result;
}

}} // namespace Fast::ShipyardXenos
