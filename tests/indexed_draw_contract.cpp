#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

typedef unsigned UINT;
typedef long HRESULT;
enum { D3DPT_TRIANGLELIST = 4, D3DFMT_INDEX16 = 101, E_INVALIDARG = -1 };
#define D3DPRED_TILE(i) (1u << (i))
#define SHIPYARD_DIAGNOSTIC(x) x
#define __forceinline inline
#include "xenos/indexed_draw.h"
#include "xenos/tile1_predication_draw.h"
#include "xenos/ring_probe.h"
#include "xenos/ring_limits.h"

struct D3DVIEWPORT9 { unsigned X, Y, Width, Height; float MinZ, MaxZ; };
struct Tile { int y2; };

namespace Fast {
std::vector<unsigned> events;
std::vector<float> submitted;
HRESULT drawResult;
struct Device {
    uintptr_t m_pRing, m_pRingLimit;
    void GetViewport(D3DVIEWPORT9* v) { events.push_back(1); v->Y = 0; v->Height = 1080; }
    HRESULT SetViewport(const D3DVIEWPORT9*) { events.push_back(10); return 0; }
    void SetPredication(unsigned mask) { events.push_back(20 + mask); }
    HRESULT DrawPrimitiveUP(unsigned primitive, unsigned triangles, const void* vertices, unsigned stride) {
        assert(primitive == D3DPT_TRIANGLELIST);
        const float* data = static_cast<const float*>(vertices);
        submitted.assign(data, data + triangles * 3 * stride / sizeof(float));
        events.push_back(6); m_pRing += 64; return drawResult;
    }
    HRESULT DrawIndexedPrimitiveUP(unsigned primitive, unsigned minimum, unsigned rows, unsigned triangles,
                                   const void* indexData, unsigned format, const void* vertices, unsigned stride) {
        assert(primitive == D3DPT_TRIANGLELIST && minimum == 0 && format == D3DFMT_INDEX16);
        const float* data = static_cast<const float*>(vertices);
        const uint16_t* index = static_cast<const uint16_t*>(indexData);
        const unsigned floats = stride / sizeof(float);
        submitted.clear();
        for (unsigned i = 0; i < triangles * 3; ++i) {
            assert(index[i] < rows);
            submitted.insert(submitted.end(), data + index[i] * floats, data + (index[i] + 1) * floats);
        }
        events.push_back(6); m_pRing += 64; return drawResult;
    }
} device;
Device* xenos_device = &device;
struct Shader { size_t floats; } shader = {8};
Shader* current_shader = &shader;
struct Framebuffer { unsigned width, height, samples; std::vector<Tile> tiles; };
std::vector<Framebuffer> buffers(1);
unsigned active_fb;
bool live_framebuffer, timing_enabled, detailed_timing_enabled;
bool tile1_predication_trial_enabled, tiling_guard_restart_pending;
uint64_t tiling_draw_bytes;
unsigned tiling_draw_restarts, tiling_peak_bytes;
unsigned tiling_1080_batches, tiling_1080_vertices, tiling_1080_oversize_batches;
unsigned tiling_1080_tile1_batches, tiling_1080_tile1_vertices;
unsigned ring_same_segment_draws, ring_max_same_segment_bytes, ring_ambiguous_draws, ring_segment_changed_draws;
uint64_t ring_same_segment_bytes, ring_same_segment_estimated_bytes;
struct Timing { uint64_t drawTicks; unsigned drawCalls; } timing_counts;
struct NativeTimingScope { NativeTimingScope(uint64_t&, unsigned&) {} };
struct DepthClip { void Apply(Device*) { events.push_back(4); } } depth_clip;
struct NativeState { unsigned alpha_mode; } native_state;
struct Histogram { void Record(unsigned, unsigned) {} } alpha_histogram;
struct Details { void Add(unsigned, unsigned = 1) {} } details_counts;
namespace ShipyardXenos { enum { DetailsDraws, DetailsTriangles }; }
ShipyardXenos::Tile1PredicationTrialCounters tile1_predication_trial_counts;
struct Budget {
    uint64_t charged;
    void ChargeVertices(uint64_t bytes) { charged += bytes; events.push_back(13); }
} tiling_budget;
bool continueBefore, continueAfter;
uint64_t receivedBytes;
void Shipyard_Fatal(const char*, HRESULT) { throw std::runtime_error("invalid draw"); }
void Shipyard_Check(HRESULT hr, const char*) { if (hr < 0) throw std::runtime_error("draw failed"); }
bool ContinueTilingBeforeDraw(uint64_t bytes) { events.push_back(14); receivedBytes = bytes; return continueBefore; }
bool GuardTrialBeforeDraw(uint64_t bytes) { events.push_back(15); receivedBytes = bytes; return false; }
bool ContinueTilingAfterDraw() { events.push_back(16); return continueAfter; }
bool GuardTrialAfterDraw() { events.push_back(17); return continueAfter; }
void FinishFramebuffer() { events.push_back(9); live_framebuffer = false; }
void BeginFramebuffer() { events.push_back(2); live_framebuffer = true; }
void PrepareDrawState() { events.push_back(3); }
void ApplyAlpha() { events.push_back(5); }

#include "production_draw.inc"

void Reset(unsigned tiles, bool pressure, bool before, bool after, bool predication, HRESULT result) {
    events.clear(); submitted.clear();
    buffers[0].width = 1920; buffers[0].height = 1080; buffers[0].samples = 1;
    buffers[0].tiles.assign(tiles, Tile()); buffers[0].tiles[0].y2 = 640;
    live_framebuffer = true; timing_enabled = detailed_timing_enabled = false;
    tile1_predication_trial_enabled = predication;
    tiling_draw_bytes = pressure ? ShipyardXenos::TilingPassLimit : 0;
    continueBefore = before; continueAfter = after;
    tiling_budget.charged = receivedBytes = 0;
    drawResult = result;
    current_shader = &shader;
}
}

int main() {
    using namespace Fast;
    using Fast::ShipyardXenos::ValidIndexedTriangleInput;
    float unique[32] = {-0.5f,-0.9f,0.5f,1, 1,2,3,4,
                        0.5f,-0.9f,0.5f,1, 5,6,7,8,
                        0.5f,-0.8f,0.5f,1, 9,10,11,12,
                       -0.5f,-0.8f,0.5f,1, 13,14,15,16};
    const uint16_t index[6] = {2,0,1,2,1,3};
    float flat[48];
    for (unsigned i = 0; i < 6; ++i)
        for (unsigned j = 0; j < 8; ++j) flat[i * 8 + j] = unique[index[i] * 8 + j];
    assert(ValidIndexedTriangleInput(unique,32,4,index,2,8));
    assert(!ValidIndexedTriangleInput(NULL,32,4,index,2,8));
    assert(!ValidIndexedTriangleInput(unique,32,4,NULL,2,8));
    assert(!ValidIndexedTriangleInput(unique,31,4,index,2,8));
    assert(!ValidIndexedTriangleInput(unique,0,0,index,2,8));
    assert(!ValidIndexedTriangleInput(unique,32,4,index,0,8));
    assert(!ValidIndexedTriangleInput(unique,0,4,index,2,0));
    assert(!ValidIndexedTriangleInput(unique,32,4,index,size_t(-1),8));
    assert(!ValidIndexedTriangleInput(unique,32,size_t(-1),index,2,8));
    assert(!ValidIndexedTriangleInput(unique,32,4,index,2,size_t(-1)));
    uint16_t high[3] = {65535,0,1};
    assert(ValidIndexedTriangleInput(unique,65536 * 4,65536,high,1,4));
    assert(!ValidIndexedTriangleInput(unique,65535 * 4,65535,high,1,4));
    assert(!ValidIndexedTriangleInput(unique,65537 * 4,65537,high,1,4));
    unsigned cases = 0;
    for (unsigned tiles = 1; tiles <= 2; ++tiles)
    for (unsigned pressure = 0; pressure < 2; ++pressure)
    for (unsigned before = 0; before < 2; ++before)
    for (unsigned after = 0; after < 2; ++after)
    for (unsigned predication = 0; predication < 2; ++predication)
    for (unsigned failure = 0; failure < 2; ++failure) {
        Reset(tiles, pressure != 0, before != 0, after != 0, predication != 0, failure ? -7 : 0);
        bool failed = false;
        try { Draw(flat,48,2); } catch (const std::runtime_error&) { failed = true; }
        assert(failed == (failure != 0));
        const std::vector<unsigned> flatEvents = events;
        const std::vector<float> flatSubmitted = submitted;
        const uint64_t flatCharge = tiling_budget.charged;
        Reset(tiles, pressure != 0, before != 0, after != 0, predication != 0, failure ? -7 : 0);
        failed = false;
        try { Draw(unique,32,2,index,4); } catch (const std::runtime_error&) { failed = true; }
        assert(failed == (failure != 0));
        assert(events == flatEvents && submitted == flatSubmitted);
#if SHIPYARD_XENOS_TILING_BUDGET
        assert(flatCharge == sizeof(flat));
        assert(tiling_budget.charged == sizeof(unique) + sizeof(index) + 31);
#endif
        ++cases;
    }
    Reset(1,false,false,false,false,0);
    uint16_t bad[6] = {2,0,1,2,1,4};
    bool rejected = false;
    try { Draw(unique,32,2,bad,4); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected && events.empty() && submitted.empty());
    std::printf("%u flat/indexed draw-state and submission cases pass\n", cases);
}
