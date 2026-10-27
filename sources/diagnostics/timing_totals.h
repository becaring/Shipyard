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
    // Per-call maxima stay per-call; merged totals keep the larger one.
    void Merge(const TimingTotals& other) {
        for (unsigned stage = 0; stage < Count; ++stage) {
            total[stage] += other.total[stage];
            if (other.maximum[stage] > maximum[stage]) maximum[stage] = other.maximum[stage];
            calls[stage] += other.calls[stage];
        }
    }
};

// One thread records a sample; another merges it only after a handoff that
// orders the recording before the merge (a packet publish, for example).
template<unsigned Count> struct TimingSample {
    TimingTotals<Count> totals;
    uint64_t starts[Count];
    bool enabled;
    TimingSample() { Reset(false); }
    void Reset(bool on) {
        enabled = on;
        totals.Reset();
        memset(starts, 0, sizeof(starts));
    }
    void Begin(unsigned stage, uint64_t now) {
        if (enabled && stage < Count) starts[stage] = now;
    }
    void End(unsigned stage, uint64_t now) {
        if (!enabled || stage >= Count || !starts[stage]) return;
        totals.Add(stage, now - starts[stage]);
        starts[stage] = 0;
    }
};
} }
