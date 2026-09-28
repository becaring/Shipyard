#pragma once

#include <xtl.h>
#include <d3dx9.h>
#include <stdint.h>
#include <stddef.h>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "shader_provider.h"
#include "depth_discard.h"
#include "tiling_space_probe.h"
#include "tiling_guard_trial.h"
#include "tile1_predication_trial.h"
#include "alpha_mode_histogram.h"

namespace Fast {
struct ShaderProgram;

namespace ShipyardXenos {
struct ShaderCacheOps {
    ID3DXBuffer* (*load)(const std::string&, const char*, const char*);
    void (*store)(const std::string&, const char*, const char*, ID3DXBuffer*);
};
void SetShaderCache(const ShaderCacheOps* operations);
struct NativeCounters {
    unsigned alphaDraws[NativeAlphaHistogram::ModeCount];
    unsigned alphaTriangles[NativeAlphaHistogram::ModeCount];
    unsigned alphaVertices[NativeAlphaHistogram::ModeCount];
    unsigned samplerAttempted, samplerWritten, samplerSuppressed;
    unsigned constantsAttempted, constantsWritten, constantsSuppressed;
    unsigned bindingAttempted, bindingWritten, bindingSuppressed;
    unsigned uploadRequests, uploadReused, uploadSavedBytes;
    unsigned clears, restoresAvoided, depthRestoresAvoided;
    unsigned depthResolves, depthResolvesSkipped, earlyDepthFences;
    unsigned tilingDrawRestarts, tilingPeakBytes;
    unsigned tiling1080Batches, tiling1080Tile1Batches;
    unsigned tiling1080Vertices, tiling1080Tile1Vertices;
    unsigned tiling1080OversizeBatches;
    unsigned ringSameSegmentDraws, ringAmbiguousDraws, ringSegmentChangedDraws;
    unsigned ringMaxSameSegmentBytes;
    uint64_t ringSameSegmentBytes, ringSameSegmentEstimatedBytes;
    TilingSpaceSummary tilingSpace;
    TilingGuardTrialStats tilingGuardTrial;
    Tile1PredicationTrialCounters tile1PredicationTrial;
    unsigned presentationColorResolvesSkipped, presentationColorDiscardBlocked;
    unsigned pendingTextures, pendingBytes, width, height, samples, tiles;
};
void CaptureAndResetCounters(NativeCounters* counters);
// Graphics-thread-only opt-in CPU wall-clock buckets. Nested buckets overlap;
// draw includes preparation, framebuffer setup and native submission, not GPU time.
struct NativeTimingCounters {
    uint64_t drawTicks, uploadTicks, framebufferSetupTicks, framebufferResolveTicks;
    uint64_t framebufferClearTicks, depthReadTicks, depthLockTicks, uploadBytes, frequency;
    uint64_t tilingSpaceQueryTicks;
    unsigned drawCalls, uploadCalls, framebufferSetupCalls, framebufferResolveCalls;
    unsigned framebufferClearCalls, depthReadCalls, depthLockCalls, depthSamples;
    unsigned tilingSpaceQueryCalls;
    // Pre-LockRect resource state, sampled only with timing enabled.
    unsigned depthBusyCalls, depthIdleCalls, depthBusy1080Calls, depthIdle1080Calls;
    unsigned preparationRebuilt, preparationReused;
    unsigned activeWidth, activeHeight, activeSamples, activeTiles;
    unsigned presentationWidth, presentationHeight;
    unsigned buffer2Frames, presentationInterval;
    bool enabled;
};
// A false-to-true transition starts a fresh interval. Repeated values preserve it.
void SetTimingEnabled(bool enabled);
// Render-thread-only, off by default. Applies only to a 1920x1080,
// two-tile, single-sample target.
void SetTilingGuardTrialEnabled(bool enabled);
// Resets counts; reads no clock. Upload bytes include requests served by reuse.
void CaptureAndResetTimingCounters(NativeTimingCounters* counters);

GfxClipParameters ClipParameters();
void UnloadShader(ShaderProgram* shader);
void LoadShader(ShaderProgram* shader);
ShaderProgram* CreateShader(const ShaderProgramKey& key);
ShaderProgram* LookupShader(const ShaderProgramKey& key);
ShaderProgramKey ShaderKey(const ShaderProgram* shader);
ShaderProgram* CurrentShader();
void ShaderInfo(ShaderProgram* shader, uint8_t* inputs, bool used[2]);
uint32_t NewTexture();
void SelectTexture(int slot, uint32_t id);
void UploadTexture(const uint8_t* pixels, uint32_t width, uint32_t height);
void SetSamplerParameters(int slot, bool linear, uint32_t addressS, uint32_t addressT);
void SetDepthTestAndMask(bool test, bool write);
void SetZmodeDecal(bool decal);
void SetViewport(int x, int y, int width, int height);
void SetScissor(int x, int y, int width, int height);
void SetUseAlpha(bool alpha);
void DrawTriangles(float* vertices, size_t count, size_t triangles);
void InitializeDisplay();
HRESULT PresentStartupUI();
void BeginGuiDraw();
void InvalidateGuiState();
void Init();
void Resize();
void StartFrame();
void EndFrame();
void FinishRender();
void Shutdown();
int CreateFramebuffer();
void DestroyFramebuffer(int id);
// flip/readDepth are compatibility arguments; this backend uses native
// orientation and retains depth fragments whenever depth is enabled.
void UpdateFramebuffer(int id, uint32_t width, uint32_t height, uint32_t msaa, bool flip,
                       bool target, bool depth, bool readDepth);
void StartDrawToFramebuffer(int id, float noiseScale);
// Source and destination rectangles use framebuffer pixel coordinates.
void CopyFramebuffer(int destination, int source, int sourceX0, int sourceY0, int sourceX1,
                     int sourceY1, int destinationX0, int destinationY0, int destinationX1,
                     int destinationY1);
void ClearFramebuffer(bool color, bool depth);
void SetColorWriteMask(bool enabled);
// Region edges are normalized to [0, 1] and clamped to the current target.
void ClearColorRegion(float left, float top, float right, float bottom);
void ReadFramebufferToCPU(int id, uint32_t width, uint32_t height, uint16_t* pixels);
void ResolveMSAAColorBuffer(int destination, int source);
std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GetPixelDepth(int id, const std::set<std::pair<float, float> >& coordinates);
void* GetFramebufferTextureId(int id);
void SelectTextureFb(int id);
bool FbNeedsSampleVFlip(int id);
void DeleteTexture(uint32_t id);
void SetTextureFilter(FilteringMode mode);
FilteringMode GetTextureFilter();
ImTextureID GetTextureById(int id);
bool ReadPresentedFrameRGBA(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height);
} // namespace ShipyardXenos
} // namespace Fast
