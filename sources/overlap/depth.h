/* Depth packets contain game projections and stable slot identities only.
 * Sampling keeps the renderer's logical-coordinate scaling and depth defaults.
 * The game applies retired results two builds after their capture. */
#ifndef SHIPYARD_OVERLAP_DEPTH_H
#define SHIPYARD_OVERLAP_DEPTH_H
#include <stdint.h>
#include <string.h>
#define SHIPYARD_OVERLAP_LIGHT_COUNT 32

typedef struct {
    uint64_t incarnation;
    float x, y;
    int32_t projectedZ;
    uint32_t slot, eligible;
} ShipyardOverlapLightRequest;

typedef struct {
    uint64_t generation, sceneEpoch;
    uint32_t active, lightCount, screenWidth, screenHeight, shrink;
    float sunX, sunY;
    ShipyardOverlapLightRequest lights[SHIPYARD_OVERLAP_LIGHT_COUNT];
} ShipyardOverlapDepthRequest;

typedef struct {
    uint64_t incarnation;
    uint32_t slot, drawGlow;
} ShipyardOverlapLightResult;

typedef struct {
    uint64_t generation, sceneEpoch;
    uint32_t active, lightCount;
    uint16_t sunDepth;
    ShipyardOverlapLightResult lights[SHIPYARD_OVERLAP_LIGHT_COUNT];
} ShipyardOverlapDepthResult;

typedef void (*ShipyardOverlapDepthPrepare)(float, float, void*);
typedef uint16_t (*ShipyardOverlapDepthRead)(float, float, void*);

static __inline int Shipyard_OverlapDepthReady(const ShipyardOverlapDepthResult* result,
                                             uint64_t buildGeneration) {
    return result->active && buildGeneration >= result->generation &&
           buildGeneration - result->generation >= 2;
}

static __inline void Shipyard_OverlapSampleDepth(const ShipyardOverlapDepthRequest* request,
                                               ShipyardOverlapDepthResult* result,
                                               ShipyardOverlapDepthPrepare prepare,
                                               ShipyardOverlapDepthRead read, void* context) {
    uint32_t i;
    memset(result, 0, sizeof(*result));
    if (!request->active) return;
    result->active = 1;
    result->generation = request->generation;
    result->sceneEpoch = request->sceneEpoch;
    result->lightCount = request->lightCount;
    if (result->lightCount > SHIPYARD_OVERLAP_LIGHT_COUNT)
        result->lightCount = SHIPYARD_OVERLAP_LIGHT_COUNT;
    if (prepare) {
        prepare(request->sunX, request->sunY, context);
        for (i = 0; i < result->lightCount; ++i) {
            if (request->lights[i].eligible)
                prepare(request->lights[i].x, request->lights[i].y, context);
        }
    }
    result->sunDepth = read ? read(request->sunX, request->sunY, context) : 0;
    for (i = 0; i < result->lightCount; ++i) {
        const ShipyardOverlapLightRequest* light = &request->lights[i];
        result->lights[i].slot = light->slot;
        result->lights[i].incarnation = light->incarnation;
        if (light->eligible) {
            int32_t depth = read ? read(light->x, light->y, context) : 0;
            result->lights[i].drawGlow = light->projectedZ < ((depth * 4) >> 3);
        }
    }
}

#ifdef __cplusplus
extern "C" {
#endif
void Shipyard_OverlapStageDepth(const ShipyardOverlapDepthRequest* request);
void Shipyard_OverlapApplyDepthResult(const ShipyardOverlapDepthResult* result);
#ifdef __cplusplus
}
#endif
#endif
