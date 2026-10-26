// Depth results follow their request and apply two builds after capture.
#include "overlap/depth.h"
#include <cassert>
#include <cstdio>

static unsigned prepared;
static void Prepare(float, float, void*) { ++prepared; }
static uint16_t Read(float x, float, void*) { return static_cast<uint16_t>(x); }

int main() {
    ShipyardOverlapDepthRequest request;
    memset(&request, 0, sizeof(request));
    request.active = 1;
    request.generation = 5;
    request.sunX = 700.0f;
    request.lightCount = SHIPYARD_OVERLAP_LIGHT_COUNT + 4;
    request.lights[0].eligible = 1;
    request.lights[0].x = 100.0f;
    request.lights[0].projectedZ = 49;
    request.lights[0].slot = 7;
    request.lights[1].eligible = 1;
    request.lights[1].x = 100.0f;
    request.lights[1].projectedZ = 50;
    ShipyardOverlapDepthResult result;
    Shipyard_OverlapSampleDepth(&request, &result, Prepare, Read, NULL);
    assert(result.active && result.generation == 5 && result.sunDepth == 700);
    assert(result.lightCount == SHIPYARD_OVERLAP_LIGHT_COUNT && prepared == 3);
    // Glow draws when the light is nearer than half the sampled depth.
    assert(result.lights[0].drawGlow == 1 && result.lights[0].slot == 7);
    assert(result.lights[1].drawGlow == 0 && result.lights[2].drawGlow == 0);
    assert(!Shipyard_OverlapDepthReady(&result, 6) && Shipyard_OverlapDepthReady(&result, 7));
    request.active = 0;
    Shipyard_OverlapSampleDepth(&request, &result, Prepare, Read, NULL);
    assert(!result.active && !Shipyard_OverlapDepthReady(&result, 100));
    std::printf("overlap depth sampling and readiness\n");
    return 0;
}
