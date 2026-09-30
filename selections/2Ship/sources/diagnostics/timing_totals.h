#pragma once
#include <stdint.h>
#include <string.h>

namespace Shipyard { namespace Diagnostics {
// Single-producer inclusive timing buckets. Clock and measurement boundaries
// belong to the caller; no allocation or output occurs while collecting.
template<unsigned Count> struct TimingTotals {
    uint64_t total[Count], maximum[Count];
    unsigned calls[Count];
    TimingTotals() { Reset(); }
    void Reset() { memset(total,0,sizeof(total)); memset(maximum,0,sizeof(maximum)); memset(calls,0,sizeof(calls)); }
    void Add(unsigned stage, uint64_t elapsed) {
        if (stage >= Count) return;
        total[stage] += elapsed;
        if (elapsed > maximum[stage]) maximum[stage] = elapsed;
        ++calls[stage];
    }
};
} }
