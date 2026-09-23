#pragma once
#include <stdint.h>
#include <ppcintrinsics.h>

enum ShipyardPerfSection {SHIPYARD_PERF_SHADER,SHIPYARD_PERF_UPLOAD,SHIPYARD_PERF_DRAW,SHIPYARD_PERF_RESTORE,SHIPYARD_PERF_STORE,SHIPYARD_PERF_DEPTH,SHIPYARD_PERF_DEPTH_WAIT,SHIPYARD_PERF_COUNT};
struct ShipyardPerfSample {uint64_t ticks;unsigned calls;};
extern __declspec(thread) ShipyardPerfSample shipyard_graphics_samples[SHIPYARD_PERF_COUNT];
struct ShipyardPerfScope {
    ShipyardPerfSection section;uint32_t start;
    ShipyardPerfScope(ShipyardPerfSection value):section(value),start(__mftb32()){}
    ~ShipyardPerfScope(){ShipyardPerfSample& sample=shipyard_graphics_samples[section];sample.ticks+=uint32_t(__mftb32()-start);++sample.calls;}
};
struct ShipyardTextureUploadCounts {unsigned requests,reused,verified,bytes_saved;};
struct ShipyardNativeStateCounts {unsigned sampler_attempted,sampler_written,sampler_suppressed,constants_attempted,constants_written,constants_suppressed,sampler_verified,constants_verified;};
