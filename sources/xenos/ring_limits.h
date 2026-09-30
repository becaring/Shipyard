#pragma once

#include "configuration.h"

// Leave half the command ring available while the CPU records the next pass.
// SegmentCount stays at the XDK default of 32.
namespace Fast { namespace ShipyardXenos {
enum {
    RingSecondaryBytes = SHIPYARD_XENOS_TILING_BUDGET ? 16 * 1024 * 1024 : 8 * 1024 * 1024,
    TilingPassLimit = SHIPYARD_XENOS_TILING_BUDGET ? 4 * 1024 * 1024 : 2 * 1024 * 1024,
    RingSegmentBytes = RingSecondaryBytes / 32
};
}} // namespace Fast::ShipyardXenos
