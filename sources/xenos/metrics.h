#pragma once

// Renderer counters are accumulated on the graphics thread and reset when
// CaptureAndResetCounters takes a snapshot for a consumer's diagnostics.
struct ShipyardTextureUploadCounts {
    unsigned requests;
    unsigned reused;
    unsigned bytes_saved;
};

struct ShipyardNativeStateCounts {
    unsigned sampler_attempted;
    unsigned sampler_written;
    unsigned sampler_suppressed;
    unsigned constants_attempted;
    unsigned constants_written;
    unsigned constants_suppressed;
};
