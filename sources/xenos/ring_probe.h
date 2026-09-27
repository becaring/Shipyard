#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Fast { namespace ShipyardXenos {

struct RingCursor {
    uintptr_t position, segmentLimit;
    RingCursor(uintptr_t p, uintptr_t l) : position(p), segmentLimit(l) {}
};

// Address arithmetic only: C++ pointer subtraction would be undefined if the
// SDK moved to another segment. A false result means the delta is ambiguous.
inline bool SameRingSegmentDelta(RingCursor before, RingCursor after,
                                 size_t segmentBytes, size_t* bytes) {
    if (!bytes || !before.position || !before.segmentLimit || !after.position ||
        before.segmentLimit != after.segmentLimit || before.position > before.segmentLimit ||
        after.position > after.segmentLimit || after.position < before.position)
        return false;
    const uintptr_t delta = after.position - before.position;
    if (delta > segmentBytes)
        return false;
    *bytes = (size_t)delta;
    return true;
}

}} // namespace Fast::ShipyardXenos
