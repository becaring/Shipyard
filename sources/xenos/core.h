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

namespace Fast {
struct ShaderProgram;

namespace ShipyardXenos {
struct ShaderCacheOps {
    ID3DXBuffer* (*load)(const std::string&,const char*,const char*);
    void (*store)(const std::string&,const char*,const char*,ID3DXBuffer*);
};
void SetShaderCache(const ShaderCacheOps* operations);
struct NativeCounters {
    unsigned samplerAttempted,samplerWritten,samplerSuppressed;
    unsigned constantsAttempted,constantsWritten,constantsSuppressed;
    unsigned bindingAttempted,bindingWritten,bindingSuppressed;
    unsigned uploadRequests,uploadReused,uploadSavedBytes;
    unsigned clears,restoresAvoided,depthRestoresAvoided;
    unsigned pendingTextures,pendingBytes,width,height,samples,tiles;
};
void CaptureAndResetCounters(NativeCounters* counters);
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
void SetSamplerParameters(int slot, bool linear, uint32_t s, uint32_t t);
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
void UpdateFramebuffer(int id, uint32_t width, uint32_t height, uint32_t msaa,
                       bool flip, bool target, bool depth, bool readDepth);
void StartDrawToFramebuffer(int id, float noise);
void CopyFramebuffer(int dst, int src, int sx0, int sy0, int sx1, int sy1,
                     int dx0, int dy0, int dx1, int dy1);
void ClearFramebuffer(bool color, bool depth);
void SetColorWriteMask(bool enabled);
void ClearColorRegion(float x0, float y0, float x1, float y1);
void ReadFramebufferToCPU(int id, uint32_t width, uint32_t height, uint16_t* pixels);
void ResolveMSAAColorBuffer(int dst, int src);
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
}
}
