#pragma once
#include <string.h>

// Render-owner counts stay off by default and read no timing clocks.
// Capture copies the completed interval before resetting its counts.
namespace Fast { namespace ShipyardXenos {
enum DetailsCount {
    DetailsDraws, DetailsTriangles, DetailsSamplerAttempted, DetailsSamplerWritten,
    DetailsSamplerSuppressed, DetailsConstantsAttempted, DetailsConstantsWritten,
    DetailsConstantsSuppressed, DetailsBindingAttempted, DetailsBindingWritten,
    DetailsBindingSuppressed, DetailsUploadRequests, DetailsUploadReused,
    DetailsUploadSavedBytes, DetailsCountSize
};
struct DetailsCounts {
    bool enabled;
    unsigned values[DetailsCountSize];
    DetailsCounts() : enabled(false) { Reset(); }
    void Reset() { memset(values, 0, sizeof(values)); }
    void Begin(bool value) { Reset(); enabled = value; }
    void Add(DetailsCount kind, unsigned count = 1) {
        if (enabled) values[kind] += count;
    }
};
void SetDetailsCountsEnabled(bool enabled);
void CaptureAndResetDetailsCounts(DetailsCounts* counts);
}}
