#pragma once

#include <stdint.h>
#include <string.h>

namespace Fast {
namespace ShipyardXenos {

// QueryBufferSpace reports command-buffer occupancy since BeginTiling. This
// records complete 1080p brackets, including setup and resolve commands; it
// never changes the conservative restart guard or issues GPU synchronization.
struct TilingSpaceSummary {
    unsigned brackets, guardFinishes, normalFinishes, querySamples, capacityChanges;
    unsigned usedRegressions;
    unsigned maxUsed[4], minRemaining[4], phaseSamples[4];
    unsigned maxSetupBytes, maxFinishBytes;
};

struct TilingSpaceProbe {
    TilingSpaceSummary summary;
    unsigned priorUsed;
    uint64_t capacity;
    bool active;

    TilingSpaceProbe() : priorUsed(0), capacity(0), active(false) {
        memset(&summary, 0, sizeof(summary));
    }
    void ResetSummary() { memset(&summary, 0, sizeof(summary)); }
    void Record(unsigned phase, unsigned used, unsigned remaining) {
        ++summary.querySamples;
        if (used > summary.maxUsed[phase]) summary.maxUsed[phase] = used;
        if (summary.phaseSamples[phase]++ == 0 || remaining < summary.minRemaining[phase])
            summary.minRemaining[phase] = remaining;
    }
    void Begin(unsigned used, unsigned remaining) {
        active = true;
        priorUsed = used;
        capacity = uint64_t(used) + remaining;
        ++summary.brackets;
        Record(0, used, remaining);
    }
    void Setup(unsigned used, unsigned remaining) {
        if (!active) return;
        Record(1, used, remaining);
        const bool capacityChanged = uint64_t(used) + remaining != capacity;
        const bool regressed = used < priorUsed;
        if (capacityChanged) ++summary.capacityChanges;
        if (regressed) ++summary.usedRegressions;
        if (!capacityChanged && !regressed && used - priorUsed > summary.maxSetupBytes)
            summary.maxSetupBytes = used - priorUsed;
        priorUsed = used;
        capacity = uint64_t(used) + remaining;
    }
    void BeforeResolve(unsigned used, unsigned remaining, bool guard) {
        if (!active) return;
        Record(2, used, remaining);
        if (guard) ++summary.guardFinishes;
        else ++summary.normalFinishes;
        if (uint64_t(used) + remaining != capacity) ++summary.capacityChanges;
        if (used < priorUsed) ++summary.usedRegressions;
        priorUsed = used;
        capacity = uint64_t(used) + remaining;
    }
    void BeforeEnd(unsigned used, unsigned remaining) {
        if (!active) return;
        Record(3, used, remaining);
        const bool capacityChanged = uint64_t(used) + remaining != capacity;
        const bool regressed = used < priorUsed;
        if (capacityChanged) ++summary.capacityChanges;
        if (regressed) ++summary.usedRegressions;
        if (!capacityChanged && !regressed && used - priorUsed > summary.maxFinishBytes)
            summary.maxFinishBytes = used - priorUsed;
        active = false;
    }
};

}
}
