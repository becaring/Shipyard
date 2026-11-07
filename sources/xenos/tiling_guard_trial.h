#pragma once

#include <stdint.h>
#include <string.h>
#include "ring_limits.h"

namespace Fast {
namespace ShipyardXenos {

// Render-thread-only opt-in; the normal tiling guard remains the default.
void SetTilingGuardTrialEnabled(bool enabled);

enum GuardTrialDecision {
    GuardTrialContinue,
    GuardTrialIneligible,
    GuardTrialOversize,
    GuardTrialCapacity,
    GuardTrialCursor,
    GuardTrialSpace
};

struct TilingGuardTrialStats {
    unsigned beginCapacityRejected, candidates, continued;
    unsigned ineligibleFallbacks, oversizeFallbacks, capacityFallbacks;
    unsigned cursorFallbacks, spaceFallbacks;
    unsigned postQueries, postFallbacks, postCapacityChanges, postCursorRegressions;
    unsigned maxDrawBytes, maxUsed;
    uint64_t totalDrawBytes;
};

// Opt-in diagnostic policy. QueryBufferSpace gives current
// occupancy, but the SDK does not document an upper bound for the next Draw or
// EndTiling; keep half of the command buffer in reserve for this trial.
struct TilingGuardTrialWindow {
    enum {
        ExpectedCapacity = RingSecondaryBytes,
        CapacityTolerance = 64 * 1024,
        MinimumRemaining = ExpectedCapacity / 2 + CapacityTolerance,
        MaximumInputBytes = 256 * 1024
    };
    TilingGuardTrialStats stats;
    uint64_t capacity;
    unsigned lastUsed, beforeDrawUsed;
    bool active, pending;

    TilingGuardTrialWindow() : capacity(0), lastUsed(0), beforeDrawUsed(0), active(false), pending(false) {
        memset(&stats, 0, sizeof(stats));
    }
    void ResetStats() { memset(&stats, 0, sizeof(stats)); }
    void Close() { active = pending = false; }
    void Begin(unsigned used, unsigned remaining) {
        Close();
        capacity = uint64_t(used) + remaining;
        if (capacity < ExpectedCapacity - CapacityTolerance ||
            capacity > ExpectedCapacity + CapacityTolerance) {
            ++stats.beginCapacityRejected;
            return;
        }
        lastUsed = used;
        active = true;
    }
    GuardTrialDecision BeforeDraw(unsigned used, unsigned remaining, uint64_t inputBytes) {
        ++stats.candidates;
        if (!active || pending) {
            ++stats.ineligibleFallbacks;
            return GuardTrialIneligible;
        }
        if (inputBytes > MaximumInputBytes) {
            ++stats.oversizeFallbacks;
            return GuardTrialOversize;
        }
        if (uint64_t(used) + remaining != capacity) {
            ++stats.capacityFallbacks;
            return GuardTrialCapacity;
        }
        if (used < lastUsed) {
            ++stats.cursorFallbacks;
            return GuardTrialCursor;
        }
        if (uint64_t(remaining) < uint64_t(MinimumRemaining) + inputBytes) {
            ++stats.spaceFallbacks;
            return GuardTrialSpace;
        }
        beforeDrawUsed = lastUsed = used;
        pending = true;
        ++stats.continued;
        if (used > stats.maxUsed) stats.maxUsed = used;
        return GuardTrialContinue;
    }
    // False asks the caller to close this bracket immediately after the Draw.
    bool AfterDraw(unsigned used, unsigned remaining) {
        if (!active || !pending) return false;
        pending = false;
        ++stats.postQueries;
        const bool capacityChanged = uint64_t(used) + remaining != capacity;
        const bool cursorRegressed = used < beforeDrawUsed;
        if (capacityChanged) ++stats.postCapacityChanges;
        if (cursorRegressed) ++stats.postCursorRegressions;
        if (!capacityChanged && !cursorRegressed) {
            const unsigned bytes = used - beforeDrawUsed;
            stats.totalDrawBytes += bytes;
            if (bytes > stats.maxDrawBytes) stats.maxDrawBytes = bytes;
            if (used > stats.maxUsed) stats.maxUsed = used;
        }
        lastUsed = used;
        if (capacityChanged || cursorRegressed || remaining < MinimumRemaining) {
            ++stats.postFallbacks;
            Close();
            return false;
        }
        return true;
    }
};

}
}
