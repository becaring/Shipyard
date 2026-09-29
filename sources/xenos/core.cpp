#include "core.h"
#include "services.h"
#include "pixels.h"
#include "shader_provider.h"
#include "tile_coverage.h"
#include "tile1_predication_draw.h"
#include "ring_probe.h"
#include "tiling_budget.h"
#include "sampler_certificate.h"

#include "xenos_backend.h"
#include "metrics.h"
#include "presentation/xenos_swap_chain.h"
#include "depth_discard.h"
#include <xgraphics.h>
#include <vectorintrinsics.h>
#include <stddef.h>
#include <stdio.h>
#include <map>
#include <algorithm>

#ifndef SHIPYARD_XENOS_DIAGNOSTICS
#define SHIPYARD_XENOS_DIAGNOSTICS 0
#endif
#if SHIPYARD_XENOS_DIAGNOSTICS
#define SHIPYARD_DIAGNOSTIC(statement) do { statement; } while (0)
#else
#define SHIPYARD_DIAGNOSTIC(statement) do { } while (0)
#endif

namespace Fast {
// The graphics device is created during display initialization and released by Shutdown().
IDirect3DDevice9* xenos_device;
struct ShaderProgram {
    ShaderProgramKey key;
    IDirect3DVertexShader9* vertex;
    IDirect3DPixelShader9* pixel;
    IDirect3DVertexDeclaration9* declaration;
    CCFeatures features;
    size_t floats;
};
namespace {
// Consumer callbacks may load/store shader bytecode. Shipyard owns the compiled
// D3D objects and their lifetime regardless of the bytecode cache policy.
ShipyardXenos::ShaderCacheOps shader_cache_ops = {NULL, NULL};
ShipyardXenos::NativeTimingCounters timing_counts;
bool timing_enabled;
uint64_t timing_frequency;
#if SHIPYARD_XENOS_DIAGNOSTICS
class NativeTimingScope {
    uint64_t* ticks;
    LARGE_INTEGER started;
public:
    NativeTimingScope(uint64_t& total, unsigned& calls) : ticks(NULL) {
        if (timing_enabled) {
            QueryPerformanceCounter(&started);
            ticks = &total;
            ++calls;
        }
    }
    ~NativeTimingScope() {
        if (ticks) {
            LARGE_INTEGER ended;
            QueryPerformanceCounter(&ended);
            *ticks += (uint64_t)(ended.QuadPart - started.QuadPart);
        }
    }
};
#else
class NativeTimingScope {
public:
    NativeTimingScope(uint64_t&, unsigned&) {}
};
#endif

struct TextureSampler {
    bool linear;
    uint32_t s, t, key;
    TextureSampler(bool filter = false) : linear(filter), s(0), t(0), key(filter ? 128 : 0) {
    }
    void Set(bool filter, uint32_t u, uint32_t v) {
        linear = filter;
        s = u;
        t = v;
        key = (u & 3) | ((v & 3) << 2) | (filter ? 128 : 0);
    }
};
struct Texture {
    IDirect3DTexture9* native;
    uint32_t width, height;
    float shader_width, shader_height;
    TextureSampler sampler;
    Texture() : native(NULL), width(0), height(0), shader_width(0), shader_height(0) {
    }
};
struct Framebuffer {
    IDirect3DSurface9* color;            // Owned reference, including presentation-surface aliases.
    IDirect3DSurface9* depth;            // Owned reference, including presentation-surface aliases.
    IDirect3DTexture9* image;            // Owned resolved color image.
    IDirect3DTexture9* color_samples[4]; // Owned per-fragment storage.
    IDirect3DTexture9* depth_samples[4]; // Owned per-fragment storage.
    std::vector<D3DRECT> tiles;
    uint32_t width, height, samples;
    float shader_width, shader_height;
    TextureSampler sampler;
    bool stored, depth_stored, render_target, hiz_capable;
    Framebuffer()
        : color(NULL), depth(NULL), image(NULL), width(0), height(0), samples(1), shader_width(0),
          shader_height(0), sampler(true), stored(false), depth_stored(false),
          render_target(false), hiz_capable(false) {
        for (int i = 0; i < 4; ++i) {
            color_samples[i] = NULL;
            depth_samples[i] = NULL;
        }
    }
};
std::map<ShaderProgramKey, ShaderProgram*> shaders;
std::vector<Texture> textures;
// Exact upload snapshots avoid replacing a texture when its pixels are unchanged.
// Keep exact source bytes for at most 32 small textures (512 KiB total). Slots
// own no D3D references; collisions only cause a normal upload.
enum { UPLOAD_SNAPSHOT_SLOTS = 32, UPLOAD_SNAPSHOT_BYTES = 16 * 1024 };
struct UploadSnapshot {
    uint32_t id, width, height;
    IDirect3DTexture9* native;
    std::vector<BYTE> rgba;
    UploadSnapshot() : id(0), width(0), height(0), native(NULL) {
    }
};
UploadSnapshot upload_snapshots[UPLOAD_SNAPSHOT_SLOTS];
ShipyardTextureUploadCounts upload_counts;
struct PendingTextureRelease {
    IDirect3DTexture9* texture;
    size_t bytes;
    PendingTextureRelease(IDirect3DTexture9* value, size_t size) : texture(value), bytes(size) {
    }
};
std::vector<PendingTextureRelease> pending_texture_releases;
size_t pending_texture_bytes;
std::vector<Framebuffer> buffers;
std::vector<int> free_framebuffers;
// The depth an update's final replay left in a framebuffer, published for the
// next update's deferred game read. The game reads it only after that update's
// first replay was submitted; a depth resolve of the framebuffer in between
// moves the framebuffer to the spare instead of overwriting the published
// texture (copy on write). Steady replays discard intermediate depth, so the
// spare is allocated only when a replay switches targets mid-frame.
struct PublishedDepth {
    IDirect3DTexture9* texture; // Owned reference while published.
    IDirect3DTexture9* spare;   // Owned; a retired texture kept for the next copy.
    int framebuffer;
    uint32_t width, height, spare_width, spare_height;
} published_depth = {NULL, NULL, -1, 0, 0, 0, 0};
unsigned framebuffer_clear_count, framebuffer_restore_avoided, framebuffer_depth_restore_avoided;
unsigned framebuffer_depth_resolves, framebuffer_depth_resolves_skipped;
unsigned framebuffer_early_depth_fences;
unsigned tiling_draw_restarts, tiling_peak_bytes;
unsigned tiling_1080_batches, tiling_1080_tile1_batches;
unsigned tiling_1080_vertices, tiling_1080_tile1_vertices;
unsigned tiling_1080_oversize_batches;
unsigned ring_same_segment_draws, ring_ambiguous_draws, ring_segment_changed_draws;
unsigned ring_max_same_segment_bytes;
uint64_t ring_same_segment_bytes, ring_same_segment_estimated_bytes;
ShipyardXenos::TilingSpaceProbe tiling_space_probe;
bool tiling_guard_restart_pending;
ShipyardXenos::TilingGuardTrialWindow tiling_guard_trial_window;
bool tiling_guard_trial_enabled;
bool tile1_predication_trial_enabled;
ShipyardXenos::Tile1PredicationTrialCounters tile1_predication_trial_counts;
bool discard_presentation_depth_on_end;
bool discard_presentation_color_on_end;
unsigned presentation_color_resolves_skipped, presentation_color_discard_blocked;
ShaderProgram* current_shader;
uint32_t selected[6];
int selected_fb[6] = {-1, -1, -1, -1, -1, -1};
int upload_tile, active_fb;
// Published image holds a separate reference until replacement or shutdown.
IDirect3DTexture9* published_framebuffer_image = NULL;
bool presentation_image_ever_published;
IDirect3DSurface9* presentation_color;
IDirect3DSurface9* presentation_depth;
IDirect3DVertexShader9* restore_vertex;
IDirect3DPixelShader9* restore_color;
IDirect3DPixelShader9* restore_depth;
IDirect3DVertexDeclaration9* restore_declaration;
IDirect3DStateBlock9* restore_state;
bool live_framebuffer, tile_constants_owned;
int pending_game_depth_clear = -1;
size_t tiling_draw_bytes;
ShipyardXenos::TilingBudget tiling_budget;
void BeginFramebuffer();
void Clear(bool color, bool depth);
void FinishFramebuffer(bool preserveDepth = true, bool preserveColor = true);
void TilingSpaceBegin();
void TilingSpaceSetup();
void TilingSpaceBeforeResolve();
void TilingSpaceBeforeEnd();
void QueryTilingSpace(DWORD* used, DWORD* remaining);
bool GuardTrialBeforeDraw(uint64_t inputBytes);
bool ContinueTilingBeforeDraw(uint64_t inputBytes);
bool ContinueTilingAfterDraw();
bool GuardTrialAfterDraw();
void RebindSampledTextures();
void Resolve(int id);
unsigned frame_number;
float shader_frame_number;
float noise_scale = 1;
float noise_scale_input = 1;
FilteringMode filtering = FILTER_THREE_POINT;
unsigned filtering_key = unsigned(FILTER_THREE_POINT) << 4;

// Only this thread owns these values. Cache the actual native sampler fields,
// not texture identity: a sampler edit through either alias must reach both
// bound slots. External/native drawing invalidates this state explicitly.
struct NativeStateCache {
    DWORD sampler[6][5];
    __declspec(align(16)) float constants[12];
    __declspec(align(16)) float binding_constants[24];
    bool binding_constants_valid;
    bool sampler_valid[6], constants_valid, alpha_valid;
    unsigned alpha_mode;
    unsigned sampler_key[6];
    IDirect3DVertexShader9* vertex;
    IDirect3DPixelShader9* pixel;
    IDirect3DVertexDeclaration9* declaration;
} native_state;
typedef char NativeConstantsAligned[(__alignof(NativeStateCache) >= 16 &&
                                     offsetof(NativeStateCache, constants) % 16 == 0)
                                        ? 1
                                        : -1];
// A prepared draw reuses the existing native-state payloads; no resource pointer
// is retained. Mutations and external drawing revoke this render-thread certificate.
bool draw_state_prepared;
ShipyardXenos::SamplerCertificate sampler_certificate;
unsigned draw_sampler_mask;
ShipyardNativeStateCounts native_state_counts;
ShipyardXenos::NativeAlphaHistogram alpha_histogram;
unsigned binding_attempted, binding_written, binding_suppressed;
const D3DSAMPLERSTATETYPE sampler_fields[5] = {
    D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV};

void InvalidateNativeState() {
    draw_state_prepared = false;
    sampler_certificate.Invalidate();
    for (int i = 0; i < 6; ++i) {
        native_state.sampler_valid[i] = false;
    }
    native_state.constants_valid = false;
    native_state.alpha_valid = false;
    native_state.binding_constants_valid = false;
    native_state.vertex = NULL;
    native_state.pixel = NULL;
    native_state.declaration = NULL;
}
HRESULT SetNativeTexture(int tile, IDirect3DTexture9* texture) {
    // SetTexture changes the image, not the slot's sampler values. The SDK
    // merges both into its fetch constant. Keep full invalidation at external
    // drawing/state-block boundaries. Literal slots avoid XDK variable-mask work.
    if (tile == 0)
        return xenos_device->SetTexture(0, texture);
    if (tile == 1)
        return xenos_device->SetTexture(1, texture);
    if (tile == 2)
        return xenos_device->SetTexture(2, texture);
    if (tile == 3)
        return xenos_device->SetTexture(3, texture);
    if (tile == 4)
        return xenos_device->SetTexture(4, texture);
    if (tile == 5)
        return xenos_device->SetTexture(5, texture);
    return E_INVALIDARG;
}
__declspec(noinline) void VerifyNativeSampler(int tile, const DWORD* desired) {
    for (unsigned field = 0; field < 5; ++field) {
        DWORD actual;
        Shipyard_Check(xenos_device->GetSamplerState(tile, sampler_fields[field], &actual),
                       "verify native sampler");
        if (actual != desired[field])
            Shipyard_Fatal("reused native sampler differs from requested state", E_FAIL);
    }
}
__declspec(noinline) void VerifyNativeConstants(const float* constants) {
    float actual[12];
    Shipyard_Check(xenos_device->GetPixelShaderConstantF(0, actual, 3),
                   "verify native pixel constants");
    if (memcmp(actual, constants, sizeof(actual)))
        Shipyard_Fatal("reused native pixel constants differ from requested values", E_FAIL);
}
__forceinline bool SameNativeConstants(const float* constants) {
    // The preparation source and cache member are explicitly 16-byte aligned.
    // Compare all 48 bytes as integer bits, preserving signed zero and NaNs.
    const __vector4 d0 = __vxor(__lvx(native_state.constants, 0), __lvx(constants, 0));
    const __vector4 d1 = __vxor(__lvx(native_state.constants, 16), __lvx(constants, 16));
    const __vector4 d2 = __vxor(__lvx(native_state.constants, 32), __lvx(constants, 32));
    unsigned condition;
    __vcmpequwR(__vor(__vor(d0, d1), d2), __vspltisw(0), &condition);
    return (condition & 0x80) != 0; // XDK XM_CRMASK_CR6TRUE: all lanes equal.
}
void SetNativeConstants(const float* constants) {
    SHIPYARD_DIAGNOSTIC(++native_state_counts.constants_attempted);
    if (native_state.constants_valid && SameNativeConstants(constants)) {
        if (Shipyard_VerifyFast3DReuse())
            VerifyNativeConstants(constants);
        SHIPYARD_DIAGNOSTIC(++native_state_counts.constants_suppressed);
        return;
    }
    // One small upload is cheaper than scalar range scans and split SDK calls.
    // Retain the unchanged-block fast path across compatible shader switches.
    xenos_device->SetPixelShaderConstantF(0, constants, 3);
    __stvx(__lvx(constants, 0), native_state.constants, 0);
    __stvx(__lvx(constants, 16), native_state.constants, 16);
    __stvx(__lvx(constants, 32), native_state.constants, 32);
    native_state.constants_valid = true;
    SHIPYARD_DIAGNOSTIC(++native_state_counts.constants_written);
}

__declspec(noinline) void VerifyNativeBindingConstants(const float* values) {
    float actual[24];
    Shipyard_Check(xenos_device->GetPixelShaderConstantF(8, actual, 6),
                   "verify native binding constants");
    if (memcmp(actual, values, sizeof(actual)))
        Shipyard_Fatal("reused binding constants differ", E_FAIL);
}

void SetNativeBindingConstants(const float* values) {
    SHIPYARD_DIAGNOSTIC(++binding_attempted);
    float* cached = native_state.binding_constants;
    typedef char
        BindingConstantsAligned[(offsetof(NativeStateCache, binding_constants) % 16 == 0) ? 1 : -1];
    if (native_state.binding_constants_valid) {
        const __vector4 d0 = __vxor(__lvx(cached, 0), __lvx(values, 0));
        const __vector4 d1 = __vxor(__lvx(cached, 16), __lvx(values, 16));
        const __vector4 d2 = __vxor(__lvx(cached, 32), __lvx(values, 32));
        const __vector4 d3 = __vxor(__lvx(cached, 48), __lvx(values, 48));
        const __vector4 d4 = __vxor(__lvx(cached, 64), __lvx(values, 64));
        const __vector4 d5 = __vxor(__lvx(cached, 80), __lvx(values, 80));
        unsigned condition;
        __vcmpequwR(__vor(__vor(__vor(d0, d1), __vor(d2, d3)), __vor(d4, d5)), __vspltisw(0),
                    &condition);
        if ((condition & 0x80) != 0) {
            if (Shipyard_VerifyFast3DReuse())
                VerifyNativeBindingConstants(values);
            SHIPYARD_DIAGNOSTIC(++binding_suppressed);
            return;
        }
    }
    Shipyard_Check(xenos_device->SetPixelShaderConstantF(8, values, 6), "binding constants");
    __stvx(__lvx(values, 0), cached, 0);
    __stvx(__lvx(values, 16), cached, 16);
    __stvx(__lvx(values, 32), cached, 32);
    __stvx(__lvx(values, 48), cached, 48);
    __stvx(__lvx(values, 64), cached, 64);
    __stvx(__lvx(values, 80), cached, 80);
    native_state.binding_constants_valid = true;
    SHIPYARD_DIAGNOSTIC(++binding_written);
}

template <int tile> void ApplySamplerSlot(const TextureSampler& sampler) {
    const bool logical_change = sampler_certificate.Update(
        tile, sampler.linear, sampler.s, sampler.t, unsigned(filtering));
    if (!native_state.sampler_valid[tile] || logical_change)
        draw_state_prepared = false;
    const unsigned key = sampler.key | filtering_key;
    if (native_state.sampler_valid[tile] && native_state.sampler_key[tile] == key) {
        SHIPYARD_DIAGNOSTIC(native_state_counts.sampler_attempted += 5);
        SHIPYARD_DIAGNOSTIC(native_state_counts.sampler_suppressed += 5);
        if (Shipyard_VerifyFast3DReuse())
            VerifyNativeSampler(tile, native_state.sampler[tile]);
        return;
    }
    native_state.sampler_key[tile] = key;
    const D3DTEXTUREFILTERTYPE filter =
        filtering == FILTER_LINEAR && sampler.linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    const DWORD address_u = (sampler.s & 2)   ? D3DTADDRESS_CLAMP
                            : (sampler.s & 1) ? D3DTADDRESS_MIRROR
                                              : D3DTADDRESS_WRAP;
    const DWORD address_v = (sampler.t & 2)   ? D3DTADDRESS_CLAMP
                            : (sampler.t & 1) ? D3DTADDRESS_MIRROR
                                              : D3DTADDRESS_WRAP;
    DWORD* cached = native_state.sampler[tile];
    SHIPYARD_DIAGNOSTIC(native_state_counts.sampler_attempted += 5);
    if (native_state.sampler_valid[tile] && cached[0] == filter && cached[1] == filter &&
        cached[2] == D3DTEXF_NONE && cached[3] == address_u && cached[4] == address_v) {
        if (Shipyard_VerifyFast3DReuse())
            VerifyNativeSampler(tile, cached);
        SHIPYARD_DIAGNOSTIC(native_state_counts.sampler_suppressed += 5);
        return;
    }
    // A real change must not rewrite the other four independent fields.
#define SHIPYARD_SAMPLER_FIELD(index, field, value)                                                \
    if (!native_state.sampler_valid[tile] || cached[index] != (DWORD)(value)) {                    \
        xenos_device->SetSamplerState(tile, field, value);                                         \
        cached[index] = (DWORD)(value);                                                            \
        SHIPYARD_DIAGNOSTIC(++native_state_counts.sampler_written);                                \
    } else                                                                                         \
        SHIPYARD_DIAGNOSTIC(++native_state_counts.sampler_suppressed)
    SHIPYARD_SAMPLER_FIELD(0, D3DSAMP_MINFILTER, filter);
    SHIPYARD_SAMPLER_FIELD(1, D3DSAMP_MAGFILTER, filter);
    SHIPYARD_SAMPLER_FIELD(2, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    SHIPYARD_SAMPLER_FIELD(3, D3DSAMP_ADDRESSU, address_u);
    SHIPYARD_SAMPLER_FIELD(4, D3DSAMP_ADDRESSV, address_v);
#undef SHIPYARD_SAMPLER_FIELD
    native_state.sampler_valid[tile] = true;
}

void ApplySampler(int tile, const TextureSampler& sampler) {
    switch (tile) {
    case 0:
        ApplySamplerSlot<0>(sampler);
        break;
    case 1:
        ApplySamplerSlot<1>(sampler);
        break;
    case 2:
        ApplySamplerSlot<2>(sampler);
        break;
    case 3:
        ApplySamplerSlot<3>(sampler);
        break;
    case 4:
        ApplySamplerSlot<4>(sampler);
        break;
    case 5:
        ApplySamplerSlot<5>(sampler);
        break;
    default:
        Shipyard_Fatal("texture sampler index", E_INVALIDARG);
    }
}

GfxClipParameters ClipParameters() {
    GfxClipParameters parameters = {true, false};
    return parameters;
}
void UnloadShader(ShaderProgram*) {
    draw_state_prepared = false;
    current_shader = NULL;
}
void LoadShader(ShaderProgram* shader) {
    // Application c0-c2 are explicit in every generated program. Shader binds
    // preserve those registers; external rendering invalidates the whole cache.
    if (current_shader != shader)
        draw_state_prepared = false;
    current_shader = shader;
    if (native_state.declaration != shader->declaration) {
        Shipyard_Check(xenos_device->SetVertexDeclaration(shader->declaration),
                       "vertex declaration");
        native_state.declaration = shader->declaration;
    }
    if (native_state.vertex != shader->vertex) {
        Shipyard_Check(xenos_device->SetVertexShader(shader->vertex), "vertex shader");
        native_state.vertex = shader->vertex;
    }
    if (native_state.pixel != shader->pixel) {
        Shipyard_Check(xenos_device->SetPixelShader(shader->pixel), "pixel shader");
        native_state.pixel = shader->pixel;
    }
}
ID3DXBuffer* Compile(const std::string& source, const char* entry, const char* profile) {
    if (shader_cache_ops.load) {
        ID3DXBuffer* cached = shader_cache_ops.load(source, entry, profile);
        if (cached)
            return cached;
    }
    ID3DXBuffer *code = NULL, *errors = NULL;
    HRESULT hr = D3DXCompileShader(source.c_str(), (UINT)source.size(), NULL, NULL, entry, profile,
                                   0, &code, &errors, NULL);
    if (FAILED(hr)) {
        Shipyard_Log("shader %s: %s", entry,
                     errors ? (char*)errors->GetBufferPointer() : "unknown");
        Shipyard_WriteFile("failed-shader.hlsl", source.data(), source.size());
    }
    if (errors) {
        errors->Release();
    }
    Shipyard_Check(hr, "compile Shipyard combiner");
    if (shader_cache_ops.store) {
        shader_cache_ops.store(source, entry, profile, code);
    }
    return code;
}
void Element(std::vector<D3DVERTEXELEMENT9>& elements, WORD& offset, int count, BYTE usage,
             BYTE index) {
    // Xenos Type is a DWORD token with swizzle/numeric bits above bit 7, not a PC D3D9 BYTE.
    const DWORD types[] = {D3DDECLTYPE_FLOAT1, D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3,
                           D3DDECLTYPE_FLOAT4};
    D3DVERTEXELEMENT9 element = {0,     offset, types[count - 1], D3DDECLMETHOD_DEFAULT,
                                 usage, index};
    elements.push_back(element);
    offset += count * sizeof(float);
}
// Non-owning indices: each ShaderProgram owns one reference, including shared
// exact binaries/declarations. Shutdown releases those references then indices.
std::map<std::string, IDirect3DVertexShader9*> vertex_programs;
std::map<std::string, IDirect3DVertexDeclaration9*> vertex_declarations;
ShaderProgram* CreateShader(const ShaderProgramKey& key) {
    ShaderProgram* shader = new ShaderProgram();
    shader->key = key;
    std::string source, hlsl, error;
    if (!Shipyard_BuildFast3DShader(key, shader->features, shader->floats, hlsl, error) ||
        !Shipyard_LowerShader(hlsl, source, error)) {
        Shipyard_Log("Fast3D shader generation/lowering failed: %s", error.c_str());
        delete shader;
        Shipyard_Fatal(error.c_str(), E_FAIL);
    }
    ID3DXBuffer* code = Compile(source, "VSMain", "vs_3_0");
    const std::string vertex_key((const char*)code->GetBufferPointer(), code->GetBufferSize());
    std::map<std::string, IDirect3DVertexShader9*>::iterator existing_vertex =
        vertex_programs.find(vertex_key);
    if (existing_vertex != vertex_programs.end()) {
        shader->vertex = existing_vertex->second;
        shader->vertex->AddRef();
    } else {
        Shipyard_Check(
            xenos_device->CreateVertexShader((DWORD*)code->GetBufferPointer(), &shader->vertex),
            "CreateVertexShader");
        vertex_programs.insert(std::make_pair(vertex_key, shader->vertex));
    }
    code->Release();
    code = Compile(source, "PSMain", "ps_3_0");
    Shipyard_Check(
        xenos_device->CreatePixelShader((DWORD*)code->GetBufferPointer(), &shader->pixel),
        "CreatePixelShader");
    code->Release();
    std::vector<D3DVERTEXELEMENT9> elements;
    WORD offset = 0;
    Element(elements, offset, 4, D3DDECLUSAGE_POSITION, 0);
    for (int i = 0; i < 2; ++i)
        if (shader->features.usedTextures[i]) {
            Element(elements, offset, 2, D3DDECLUSAGE_TEXCOORD, i);
            for (int j = 0; j < 2; ++j) {
                if (shader->features.clamp[i][j]) {
                    Element(elements, offset, 1, D3DDECLUSAGE_TEXCOORD, 11 + i * 2 + j);
                }
            }
        }
    if (shader->features.opt_fog) {
        Element(elements, offset, 4, D3DDECLUSAGE_TEXCOORD, 9);
    }
    if (shader->features.opt_grayscale) {
        Element(elements, offset, 4, D3DDECLUSAGE_TEXCOORD, 10);
    }
    for (int i = 0; i < shader->features.numInputs; ++i) {
        Element(elements, offset, shader->features.opt_alpha ? 4 : 3, D3DDECLUSAGE_TEXCOORD, i + 2);
    }
    D3DVERTEXELEMENT9 end = D3DDECL_END();
    elements.push_back(end);
    const std::string declaration_key((const char*)&elements[0],
                                      elements.size() * sizeof(elements[0]));
    std::map<std::string, IDirect3DVertexDeclaration9*>::iterator existing_decl =
        vertex_declarations.find(declaration_key);
    if (existing_decl != vertex_declarations.end()) {
        shader->declaration = existing_decl->second;
        shader->declaration->AddRef();
    } else {
        Shipyard_Check(xenos_device->CreateVertexDeclaration(&elements[0], &shader->declaration),
                       "CreateVertexDeclaration");
        vertex_declarations.insert(std::make_pair(declaration_key, shader->declaration));
    }
    if (offset != shader->floats * sizeof(float))
        Shipyard_Fatal("Shipyard vertex stride", E_FAIL);
    shaders[key] = shader;
    LoadShader(shader);
    return shader;
}
ShaderProgram* Lookup(const ShaderProgramKey& key) {
    std::map<ShaderProgramKey, ShaderProgram*>::iterator it = shaders.find(key);
    return it == shaders.end() ? NULL : it->second;
}
void ShaderInfo(ShaderProgram* shader, uint8_t* inputs, bool used[2]) {
    *inputs = (uint8_t)shader->features.numInputs;
    used[0] = shader->features.usedTextures[0];
    used[1] = shader->features.usedTextures[1];
}
uint32_t NewTexture() {
    draw_state_prepared = false;
    textures.push_back(Texture());
    return (uint32_t)textures.size() - 1;
}
void CollectTextures(bool wait) {
    for (size_t i = 0; i < pending_texture_releases.size();) {
        PendingTextureRelease& pending = pending_texture_releases[i];
        if (wait || !pending.texture->IsBusy()) {
            pending.texture->Release();
            pending_texture_bytes -= pending.bytes;
            pending_texture_releases[i] = pending_texture_releases.back();
            pending_texture_releases.pop_back();
        } else {
            ++i;
        }
    }
}
void ReleaseTexture(IDirect3DTexture9* texture) {
    // XDK Release waits for the GPU when dropping the last reference. Keep the
    // replaced immutable image alive until the SDK reports it unused, so a
    // small texture upload doesn't drain the GPU halfway through a frame.
    // Keep this reference through EndTiling as well: recorded tile draws have
    // not all reached the GPU yet. Collection occurs at the next frame's start.
    UINT base = 0, mips = 0;
    XGGetTextureLayout(texture, NULL, &base, NULL, NULL, 0, NULL, &mips, NULL, NULL, 0);
    size_t bytes = XGNextMultiple(base, 4096) + XGNextMultiple(mips, 4096);
    const size_t budget = 8 * 1024 * 1024;
    if (bytes > budget || pending_texture_releases.size() >= 128 ||
        pending_texture_bytes + bytes > budget) {
        FinishFramebuffer();
        CollectTextures(true);
        if (bytes > budget) {
            texture->Release();
            return;
        }
    }
    pending_texture_releases.push_back(PendingTextureRelease(texture, bytes));
    pending_texture_bytes += bytes;
}
void SelectTexture(int tile, uint32_t id) {
    draw_state_prepared = false;
    if (tile < 0 || tile > 5 || id >= textures.size())
        Shipyard_Fatal("texture index", E_INVALIDARG);
    upload_tile = tile;
    selected[tile] = id;
    selected_fb[tile] = -1;
    Shipyard_Check(SetNativeTexture(tile, textures[id].native), "SetTexture");
    // Fast3D caches sampler settings per texture and does not repeat Sampler()
    // on a cache hit. Restore them whenever that texture is selected again.
    ApplySampler(tile, textures[id].sampler);
}
void VerifyTextureUpload(Texture& tex, const uint8_t* bytes) {
    // Shipyard_VerifyFast3DReuse enables this readback after an unchanged upload.
    // Complete tiling before the CPU lock; never wait inside a tile bracket.
    const bool resume = live_framebuffer;
    FinishFramebuffer();
    xenos_device->BlockUntilIdle();
    D3DLOCKED_RECT lock;
    Shipyard_Check(tex.native->LockRect(0, &lock, NULL, D3DLOCK_READONLY),
                   "verify retained texture LockRect");
    for (uint32_t y = 0; y < tex.height; ++y) {
        const DWORD* row = (const DWORD*)((const BYTE*)lock.pBits + y * lock.Pitch);
        for (uint32_t x = 0; x < tex.width; ++x) {
            const BYTE* p = bytes + (y * tex.width + x) * 4;
            const DWORD expected =
                ((DWORD)p[3] << 24) | ((DWORD)p[0] << 16) | ((DWORD)p[1] << 8) | p[2];
            if (row[x] != expected)
                Shipyard_Fatal("retained texture differs from requested pixels", E_FAIL);
        }
    }
    Shipyard_Check(tex.native->UnlockRect(0), "verify retained texture UnlockRect");
    if (resume)
        BeginFramebuffer();
}
void Upload(const uint8_t* bytes, uint32_t width, uint32_t height) {
    NativeTimingScope timing(timing_counts.uploadTicks, timing_counts.uploadCalls);
    const uint32_t id = selected[upload_tile];
    Texture& tex = textures[id];
    if (!width || !height || width > 4096 || height > 4096)
        Shipyard_Fatal("texture dimensions", E_INVALIDARG);
    SHIPYARD_DIAGNOSTIC(++upload_counts.requests);
    const size_t size = width * height * 4;
    if (timing_enabled)
        timing_counts.uploadBytes += size;
    UploadSnapshot& snapshot = upload_snapshots[id & (UPLOAD_SNAPSHOT_SLOTS - 1)];
    if (size <= UPLOAD_SNAPSHOT_BYTES && tex.native && snapshot.native == tex.native &&
        snapshot.id == id && snapshot.width == width && snapshot.height == height &&
        snapshot.rgba.size() == size && memcmp(&snapshot.rgba[0], bytes, size) == 0) {
        const bool verify_reuse = Shipyard_VerifyFast3DReuse();
        if (verify_reuse)
            VerifyTextureUpload(tex, bytes);
        SHIPYARD_DIAGNOSTIC(++upload_counts.reused);
        SHIPYARD_DIAGNOSTIC(upload_counts.bytes_saved += size);
        const bool framebuffer_transition = selected_fb[upload_tile] >= 0;
        selected_fb[upload_tile] = -1;
        if (verify_reuse) {
            // Readback verification may have closed and reopened the render
            // target, invalidating native texture bindings.
            RebindSampledTextures();
        } else {
            if (framebuffer_transition) {
                draw_state_prepared = false;
                Shipyard_Check(SetNativeTexture(upload_tile, tex.native), "rebind upload target");
                native_state.sampler_valid[upload_tile] = false;
                ApplySampler(upload_tile, tex.sampler);
            }
        }
        return;
    }
    snapshot.native = NULL;
    for (int i = 0; i < 6; ++i)
        if (selected_fb[i] < 0 && selected[i] == id) {
            if (tex.native)
                SetNativeTexture(i, NULL);
            // The replacement may rebuild the combined Xenos fetch state.
            // Restore this slot's sampler after binding the new image.
            native_state.sampler_valid[i] = false;
        }
    if (tex.native) {
        ReleaseTexture(tex.native);
    }
    Shipyard_Check(xenos_device->CreateTexture(width, height, 1, 0, D3DFMT_LIN_A8R8G8B8,
                                               D3DPOOL_DEFAULT, &tex.native, NULL),
                   "CreateTexture");
    D3DLOCKED_RECT lock;
    Shipyard_Check(tex.native->LockRect(0, &lock, NULL, 0), "texture LockRect");
    for (uint32_t y = 0; y < height; ++y) {
        DWORD* out = (DWORD*)((BYTE*)lock.pBits + y * lock.Pitch);
        for (uint32_t x = 0; x < width; ++x) {
            const BYTE* p = bytes + (y * width + x) * 4;
            out[x] = ((DWORD)p[3] << 24) | ((DWORD)p[0] << 16) | ((DWORD)p[1] << 8) | p[2];
        }
    }
    tex.native->UnlockRect(0);
    tex.width = width;
    tex.height = height;
    tex.shader_width = (float)width;
    tex.shader_height = (float)height;
    if (size <= UPLOAD_SNAPSHOT_BYTES) {
        if (snapshot.rgba.capacity() < UPLOAD_SNAPSHOT_BYTES)
            snapshot.rgba.reserve(UPLOAD_SNAPSHOT_BYTES);
        snapshot.rgba.resize(size);
        memcpy(&snapshot.rgba[0], bytes, size);
        snapshot.id = id;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.native = tex.native;
    }
    selected_fb[upload_tile] = -1;
    RebindSampledTextures();
}
void Sampler(int tile, bool linear, uint32_t s, uint32_t t) {
    TextureSampler& sampler = selected_fb[tile] >= 0 ? buffers[selected_fb[tile]].sampler
                                                     : textures[selected[tile]].sampler;
    sampler.Set(linear, s, t);
    ApplySampler(tile, sampler);
}
void Depth(bool test, bool write) {
    xenos_device->SetRenderState(D3DRS_ZENABLE, test || write);
    xenos_device->SetRenderState(D3DRS_ZWRITEENABLE, write);
    xenos_device->SetRenderState(D3DRS_ZFUNC, test ? D3DCMP_LESSEQUAL : D3DCMP_ALWAYS);
}
void Decal(bool enable) {
    // Match the Fast3D D3D11 rasterizer: offset coplanar decals by their depth
    // slope. A fixed epsilon loses paths on steep or distant floor polygons.
    float slope = enable ? -2.0f : 0.0f;
    DWORD bits;
    memcpy(&bits, &slope, sizeof(bits));
    xenos_device->SetRenderState(D3DRS_DEPTHBIAS, 0);
    xenos_device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, bits);
}
void Viewport(int x, int y, int w, int h) {
    // XDK clamps viewport dimensions against the bound surface. Complete the
    // pending target switch (including BeginTiling) before applying Fast3D's
    // viewport, including after a switch from a smaller offscreen target.
    BeginFramebuffer();
    D3DVIEWPORT9 v = {(DWORD)x, buffers[active_fb].height - y - h, (DWORD)w, (DWORD)h, 0, 1};
    Shipyard_Check(xenos_device->SetViewport(&v), "viewport");
}
void Scissor(int x, int y, int w, int h) {
    BeginFramebuffer();
    LONG height = (LONG)buffers[active_fb].height;
    RECT r = {x, height - y - h, x + w, height - y};
    xenos_device->SetScissorRect(&r);
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
}
bool requested_alpha;
bool color_writes = true;
unsigned NativeAlphaMode(bool enable, const CCFeatures* features) {
    // Keep alpha in the combiner for rejection; opaque survivors do not blend.
    // Other alpha modes retain the configured blending path.
    return ShipyardXenos::ClassifyNativeAlphaMode(
        enable, features && features->opt_alpha,
        features && features->opt_texture_edge,
        features && features->opt_noise, features && features->opt_invisible);
}
void ApplyAlpha() {
    const unsigned mode =
        NativeAlphaMode(requested_alpha, current_shader ? &current_shader->features : NULL);
    if (native_state.alpha_valid && native_state.alpha_mode == mode)
        return;
    xenos_device->SetRenderState(D3DRS_ALPHABLENDENABLE, mode == 1);
    xenos_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    xenos_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    xenos_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    // Cutout writes RGB only; ZERO/ONE separate-alpha blending preserves the
    // destination alpha of nonopaque offscreen targets.
    xenos_device->SetRenderState(D3DRS_COLORWRITEENABLE, color_writes ? (mode == 2 ? 7 : 15) : 0);
    xenos_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
    xenos_device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
    xenos_device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
    xenos_device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    native_state.alpha_mode = mode;
    native_state.alpha_valid = true;
}
void Alpha(bool enable) {
    requested_alpha = enable;
    ApplyAlpha();
}

void VerifyPreparedDrawState() {
    VerifyNativeConstants(native_state.constants);
    VerifyNativeBindingConstants(native_state.binding_constants);
    for (int slot = 0; slot < 6; ++slot)
        if (draw_sampler_mask & (1u << slot))
            VerifyNativeSampler(slot, native_state.sampler[slot]);
}
__declspec(noinline) void RebuildDrawState() {
    if (timing_enabled)
        ++timing_counts.preparationRebuilt;
    draw_sampler_mask = 0;
    __declspec(align(16)) float constants[12] = {shader_frame_number, noise_scale, 0, 0};
    __declspec(align(16)) float binding_info[24] = {0};
    const TextureSampler* base_samplers[2] = {NULL, NULL};
    for (int i = 0; i < 2; ++i) {
        if (selected_fb[i] >= 0) {
            const Framebuffer& b = buffers[selected_fb[i]];
            binding_info[i * 4] = b.shader_width;
            binding_info[i * 4 + 1] = b.shader_height;
            base_samplers[i] = &b.sampler;
        } else if (selected[i] < textures.size()) {
            const Texture& t = textures[selected[i]];
            binding_info[i * 4] = t.shader_width;
            binding_info[i * 4 + 1] = t.shader_height;
            base_samplers[i] = &t.sampler;
        }
        if (current_shader->features.usedTextures[i]) {
            const TextureSampler* sampler = base_samplers[i];
            constants[4 + i * 4] = binding_info[i * 4];
            constants[5 + i * 4] = binding_info[i * 4 + 1];
            if (sampler) {
                // The same cached texture may occupy both slots. A sampler update
                // through either slot applies to that texture, as in D3D11/GL.
                ApplySampler(i, *sampler);
                draw_sampler_mask |= 1u << i;
                constants[6 + i * 4] =
                    filtering == FILTER_THREE_POINT && sampler->linear ? 1.0f : 0.0f;
            }
        }
    }
    SetNativeConstants(constants);
    for (int binding = 2; binding < 6; ++binding) {
        if (selected_fb[binding] >= 0) {
            const Framebuffer& b = buffers[selected_fb[binding]];
            binding_info[binding * 4] = b.shader_width;
            binding_info[binding * 4 + 1] = b.shader_height;
        } else if (selected[binding] < textures.size()) {
            const Texture& t = textures[selected[binding]];
            binding_info[binding * 4] = t.shader_width;
            binding_info[binding * 4 + 1] = t.shader_height;
        }
        const TextureSampler* sampler = base_samplers[binding % 2];
        if (sampler) {
            ApplySampler(binding, *sampler);
            draw_sampler_mask |= 1u << binding;
        }
    }
    SetNativeBindingConstants(binding_info);
    draw_state_prepared = true;
}

__forceinline void PrepareDrawState() {
    if (!draw_state_prepared)
        RebuildDrawState();
    else {
        if (timing_enabled)
            ++timing_counts.preparationReused;
        if (Shipyard_VerifyFast3DReuse())
            VerifyPreparedDrawState();
    }
}

void Draw(float* vertices, size_t count, size_t triangles) {
    NativeTimingScope timing(timing_counts.drawTicks, timing_counts.drawCalls);
    if (!current_shader || count != triangles * 3 * current_shader->floats)
        Shipyard_Fatal("Shipyard vertex buffer size", E_INVALIDARG);
    // EndTiling cannot submit a partially filled SDK ring. Retain the target and
    // restart the native bracket before it consumes its reserved command space.
    // The allowance includes state-command space for each Fast3D batch.
    bool trialContinued = false;
    bool budgetContinued = false;
    bool restoreGuardViewport = false;
    D3DVIEWPORT9 guardViewport;
    if (live_framebuffer && buffers[active_fb].tiles.size() > 1 &&
        tiling_draw_bytes + count * sizeof(float) + 8192 > 2 * 1024 * 1024) {
        budgetContinued = ContinueTilingBeforeDraw(uint64_t(count) * sizeof(float));
        trialContinued = budgetContinued || GuardTrialBeforeDraw(uint64_t(count) * sizeof(float));
        if (!trialContinued) {
            xenos_device->GetViewport(&guardViewport);
            restoreGuardViewport = true;
            ++tiling_draw_restarts;
            tiling_guard_restart_pending = timing_enabled;
            FinishFramebuffer();
            tiling_guard_restart_pending = false;
        }
    }
    BeginFramebuffer();
    if (restoreGuardViewport)
        Shipyard_Check(xenos_device->SetViewport(&guardViewport), "restore tiling restart viewport");
    bool tile1Only = false;
    if (timing_enabled || tile1_predication_trial_enabled) {
        const Framebuffer& b = buffers[active_fb];
        const bool profileTarget = timing_enabled && b.width == 1920 &&
                                   b.height == 1080 && b.tiles.size() == 2;
        const bool trialTarget = tile1_predication_trial_enabled && b.width == 1920 &&
                                 b.height == 1080 && b.tiles.size() == 2 && b.samples == 1;
        D3DVIEWPORT9 viewport = {0};
        if (profileTarget || trialTarget)
            xenos_device->GetViewport(&viewport);
        const size_t vertexCount = count / current_shader->floats;
        if (tile1_predication_trial_enabled)
            tile1Only = ShipyardXenos::ClassifyTile1Predication(
                trialTarget, vertices, vertexCount, current_shader->floats, b.height,
                viewport.Y, viewport.Height, trialTarget ? (unsigned)b.tiles[0].y2 : 0,
                &tile1_predication_trial_counts);
        if (profileTarget) {
            ++tiling_1080_batches;
            tiling_1080_vertices += (unsigned)vertexCount;
            if (vertexCount > ShipyardXenos::TileCoverageMaximumVertices)
                ++tiling_1080_oversize_batches;
            const bool profileTile1Only = trialTarget ? tile1Only :
                (viewport.Y <= b.height && viewport.Height <= b.height - viewport.Y &&
                 ShipyardXenos::BatchInsideSecondTile(
                     vertices, vertexCount, current_shader->floats, viewport.Y,
                     viewport.Height, (unsigned)b.tiles[0].y2));
            if (profileTile1Only) {
                ++tiling_1080_tile1_batches;
                tiling_1080_tile1_vertices += (unsigned)vertexCount;
            }
        }
    }
    const Framebuffer& ringFramebuffer = buffers[active_fb];
    const bool probeRing = timing_enabled && ringFramebuffer.width == 1920 &&
                           ringFramebuffer.height == 1080 && ringFramebuffer.tiles.size() == 2;
    const ShipyardXenos::RingCursor ringBefore(
        probeRing ? (uintptr_t)xenos_device->m_pRing : 0,
        probeRing ? (uintptr_t)xenos_device->m_pRingLimit : 0);
    PrepareDrawState();
    ApplyAlpha(); // Also handles shader-only changes and external state restoration.
    const HRESULT drawResult = tile1Only
        ? ShipyardXenos::SubmitTile1PredicatedDraw(
              xenos_device, true, (UINT)triangles, vertices,
              (UINT)(current_shader->floats * 4), &tile1_predication_trial_counts)
        : xenos_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)triangles, vertices,
                                       (UINT)(current_shader->floats * 4));
    tiling_budget.ChargeVertices(uint64_t(count) * sizeof(float));
    Shipyard_Check(drawResult, "Shipyard DrawPrimitiveUP");
    if (timing_enabled)
        SHIPYARD_DIAGNOSTIC(alpha_histogram.Record(native_state.alpha_mode, (unsigned)triangles));
    const bool trialNeedsFinish = trialContinued &&
        !(budgetContinued ? ContinueTilingAfterDraw() : GuardTrialAfterDraw());
    if (probeRing) {
        const ShipyardXenos::RingCursor ringAfter(
            (uintptr_t)xenos_device->m_pRing, (uintptr_t)xenos_device->m_pRingLimit);
        size_t observedBytes;
        if (ShipyardXenos::SameRingSegmentDelta(ringBefore, ringAfter,
                                                8 * 1024 * 1024 / 32, &observedBytes)) {
            ++ring_same_segment_draws;
            ring_same_segment_bytes += observedBytes;
            ring_same_segment_estimated_bytes += count * sizeof(float) + 8192;
            ring_max_same_segment_bytes = std::max(ring_max_same_segment_bytes,
                                                   (unsigned)observedBytes);
        } else {
            ++ring_ambiguous_draws;
            if (ringBefore.segmentLimit != ringAfter.segmentLimit)
                ++ring_segment_changed_draws;
        }
    }
    tiling_draw_bytes += count * sizeof(float) + 8192;
    if (buffers[active_fb].tiles.size() > 1 && tiling_draw_bytes > tiling_peak_bytes)
        tiling_peak_bytes = (unsigned)tiling_draw_bytes;
    if (trialNeedsFinish) {
        // A rejected post-draw reservation must preserve the logical viewport
        // across both SDK tiling boundaries, just like the pre-draw restart.
        xenos_device->GetViewport(&guardViewport);
        FinishFramebuffer();
        BeginFramebuffer();
        Shipyard_Check(xenos_device->SetViewport(&guardViewport), "restore tiling fallback viewport");
    }
}
void ValidateFramebuffer(int id) {
    if (id < 0 || (size_t)id >= buffers.size() ||
        std::find(free_framebuffers.begin(), free_framebuffers.end(), id) !=
            free_framebuffers.end())
        Shipyard_Fatal("framebuffer index", E_INVALIDARG);
}
void InitializeRestore() {
    const std::string source =
        "struct V {float2 p:POSITION;float2 uv:TEXCOORD0;};\n"
        "struct O {float4 p:POSITION;float2 uv:TEXCOORD0;};\n"
        "O VSMain(V v){O o;o.p=float4(v.p,0,1);o.uv=v.uv;return o;}\n"
        "sampler2D colorImage:register(s0);sampler2D depthImage:register(s1);\n"
        "float4 ColorMain(O v):COLOR0{return tex2D(colorImage,v.uv);}\n"
        "struct D {float4 color:COLOR0;float depth:DEPTH;};\n"
        "D DepthMain(O v){D "
        "o;o.color=tex2D(colorImage,v.uv);o.depth=tex2D(depthImage,v.uv).r;return o;}\n";
    ID3DXBuffer* code = Compile(source, "VSMain", "vs_3_0");
    Shipyard_Check(
        xenos_device->CreateVertexShader((DWORD*)code->GetBufferPointer(), &restore_vertex),
        "framebuffer restore VS");
    code->Release();
    code = Compile(source, "ColorMain", "ps_3_0");
    Shipyard_Check(
        xenos_device->CreatePixelShader((DWORD*)code->GetBufferPointer(), &restore_color),
        "framebuffer copy PS");
    code->Release();
    code = Compile(source, "DepthMain", "ps_3_0");
    Shipyard_Check(
        xenos_device->CreatePixelShader((DWORD*)code->GetBufferPointer(), &restore_depth),
        "framebuffer depth restore PS");
    code->Release();
    D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()};
    Shipyard_Check(xenos_device->CreateVertexDeclaration(elements, &restore_declaration),
                   "framebuffer restore declaration");
    Shipyard_Check(xenos_device->CreateStateBlock(D3DSBT_ALL, &restore_state),
                   "framebuffer restore state");
}
void StopTileConstants() {
    if (tile_constants_owned) {
        xenos_device->GpuDisownPixelShaderConstantF(4, 4);
        tile_constants_owned = false;
    }
}
void SetTileConstants() {
    // Tile-owned c4..c7 are disjoint from the cached CPU draw block c0..c2.
    const Framebuffer& b = buffers[active_fb];
    if (b.tiles.size() > 1) {
        xenos_device->GpuOwnPixelShaderConstantF(4, 4);
        tile_constants_owned = true;
        for (size_t tile = 0; tile < b.tiles.size(); ++tile) {
            xenos_device->SetPredication(D3DPRED_TILE(tile));
            D3DVECTOR4* values = NULL;
            Shipyard_Check(xenos_device->GpuBeginPixelShaderConstantF4(4, &values, 4),
                           "tile pixel constants");
            ZeroMemory(values, 4 * sizeof(D3DVECTOR4));
            values[0].x = (float)b.tiles[tile].x1;
            values[0].y = (float)b.tiles[tile].y1;
            xenos_device->GpuEndPixelShaderConstantF4();
        }
        xenos_device->SetPredication(0);
    } else {
        float zero[4] = {0};
        xenos_device->SetPixelShaderConstantF(4, zero, 1);
    }
}
bool BeginRestoreQuad(uint32_t width, uint32_t height, bool has_depth) {
    InvalidateNativeState();
    const bool restore_constants = tile_constants_owned;
    StopTileConstants();
    Shipyard_Check(restore_state->Capture(), "capture framebuffer copy state");
    D3DVIEWPORT9 viewport = {0, 0, width, height, 0, 1};
    xenos_device->SetViewport(&viewport);
    xenos_device->SetRenderState(D3DRS_VIEWPORTENABLE, TRUE);
    xenos_device->SetRenderState(D3DRS_HALFPIXELOFFSET, TRUE);
    xenos_device->SetVertexShader(restore_vertex);
    xenos_device->SetPixelShader(has_depth ? restore_depth : restore_color);
    xenos_device->SetVertexDeclaration(restore_declaration);
    xenos_device->SetRenderState(D3DRS_ZENABLE, has_depth);
    xenos_device->SetRenderState(D3DRS_ZWRITEENABLE, has_depth);
    xenos_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    xenos_device->SetRenderState(D3DRS_HIZENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_HIZWRITEENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_HISTENCILENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_HISTENCILWRITEENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    xenos_device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    xenos_device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    xenos_device->SetRenderState(D3DRS_DEPTHBIAS, 0);
    xenos_device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
    xenos_device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
    return restore_constants;
}
void DrawRestoreQuadSample(IDirect3DTexture9* color, IDirect3DTexture9* depth,
                           DWORD sample_mask, const float* copy_quad, bool configure_sampler) {
    xenos_device->SetRenderState(D3DRS_MULTISAMPLEMASK, sample_mask);
    SetNativeTexture(0, color);
    SetNativeTexture(1, depth);
    if (configure_sampler)
        for (int sampler = 0; sampler < 2; ++sampler) {
            xenos_device->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            xenos_device->SetSamplerState(sampler, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            xenos_device->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            xenos_device->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            xenos_device->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        }
    // Xenos HALFPIXELOFFSET already aligns pixel and texel centers. Adding a
    // desktop D3D9 half-pixel translation again shifts retained color/depth on
    // every restore (notably when a newly drawn glyph replaces a cached texture).
    // Match XDK ATG::PostProcess::DrawFullScreenQuad's unshifted clip-space quad.
    float left = -1.0f, right = 1.0f, top = 1.0f, bottom = -1.0f;
    const float quad[] = {left, top,    0, 0, right, top, 1, 0, left,  bottom, 0, 1,
                          left, bottom, 0, 1, right, top, 1, 0, right, bottom, 1, 1};
    Shipyard_Check(xenos_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2,
                                                 copy_quad ? copy_quad : quad, 4 * sizeof(float)),
                   "restore framebuffer samples");
    tiling_budget.ChargeVertices(sizeof(quad));
}
void EndRestoreQuad(bool restore_constants) {
    Shipyard_Check(restore_state->Apply(), "restore framebuffer copy state");
    InvalidateNativeState();
    if (restore_constants)
        SetTileConstants();
}
void RestoreQuad(IDirect3DTexture9* color, IDirect3DTexture9* depth, uint32_t width,
                 uint32_t height, DWORD sample_mask, const float* copy_quad = NULL) {
    const bool restore_constants = BeginRestoreQuad(width, height, depth != NULL);
    DrawRestoreQuadSample(color, depth, sample_mask, copy_quad, true);
    EndRestoreQuad(restore_constants);
}
void ClearFramebufferTargets(DWORD flags) {
    NativeTimingScope timing(timing_counts.framebufferClearTicks, timing_counts.framebufferClearCalls);
    const Framebuffer& b = buffers[active_fb];
    D3DVIEWPORT9 previous;
    DWORD scissor;
    xenos_device->GetViewport(&previous);
    xenos_device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
    // XDK Clear clips even explicit rectangles to the current viewport. The
    // clear_framebuffer contract clears the complete target, like D3D11/GL.
    D3DVIEWPORT9 full = {0, 0, b.width, b.height, 0, 1};
    Shipyard_Check(xenos_device->SetViewport(&full), "full framebuffer clear viewport");
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    Shipyard_Check(xenos_device->Clear(0, NULL, flags, 0xff000000, 1, 0), "clear framebuffer");
    Shipyard_Check(xenos_device->SetViewport(&previous), "restore framebuffer clear viewport");
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE, scissor);
}
bool ProfileTilingSpace() {
    const Framebuffer& b = buffers[active_fb];
    return timing_enabled && b.width == 1920 && b.height == 1080 && b.tiles.size() == 2;
}
void QueryTilingSpace(DWORD* used, DWORD* remaining) {
    NativeTimingScope timing(timing_counts.tilingSpaceQueryTicks,
                             timing_counts.tilingSpaceQueryCalls);
    *used = *remaining = 0;
    xenos_device->QueryBufferSpace(used, remaining);
}
bool ContinueTilingBeforeDraw(uint64_t inputBytes) {
    if (!tiling_budget.active) return false;
    DWORD used = 0, remaining = 0;
    QueryTilingSpace(&used, &remaining);
    return tiling_budget.BeforeDraw(used, remaining, (uintptr_t)xenos_device->m_pRing,
                                   (uintptr_t)xenos_device->m_pRingLimit,
                                   inputBytes, &tiling_draw_bytes);
}
bool ContinueTilingAfterDraw() {
    DWORD used = 0, remaining = 0;
    QueryTilingSpace(&used, &remaining);
    return tiling_budget.AfterDraw(used, remaining, (uintptr_t)xenos_device->m_pRing,
                                  (uintptr_t)xenos_device->m_pRingLimit);
}
bool GuardTrialBeforeDraw(uint64_t inputBytes) {
    if (!tiling_guard_trial_enabled) return false;
    DWORD used = 0, remaining = 0;
    if (tiling_guard_trial_window.active)
        QueryTilingSpace(&used, &remaining);
    return tiling_guard_trial_window.BeforeDraw(used, remaining, inputBytes) ==
           ShipyardXenos::GuardTrialContinue;
}
bool GuardTrialAfterDraw() {
    DWORD used, remaining;
    QueryTilingSpace(&used, &remaining);
    return tiling_guard_trial_window.AfterDraw(used, remaining);
}
void TilingSpaceBegin() {
    const Framebuffer& b = buffers[active_fb];
    const bool profile = ProfileTilingSpace();
    const bool trial = tiling_guard_trial_enabled && b.width == 1920 && b.height == 1080 &&
                       b.tiles.size() == 2 && b.samples == 1;
    const bool budget = b.width == 1920 && b.height == 1080 &&
                        b.tiles.size() == 2 && b.samples == 1;
    tiling_budget.Close();
    if (!profile && !trial && !budget) return;
    DWORD used, remaining;
    QueryTilingSpace(&used, &remaining);
    if (budget) tiling_budget.Begin(used, remaining);
    if (profile) tiling_space_probe.Begin(used, remaining);
    if (trial) tiling_guard_trial_window.Begin(used, remaining);
}
void TilingSpaceSetup() {
    if (!timing_enabled || !tiling_space_probe.active) return;
    DWORD used, remaining;
    QueryTilingSpace(&used, &remaining);
    tiling_space_probe.Setup(used, remaining);
}
void TilingSpaceBeforeResolve() {
    if (!timing_enabled || !tiling_space_probe.active) return;
    DWORD used, remaining;
    QueryTilingSpace(&used, &remaining);
    tiling_space_probe.BeforeResolve(used, remaining, tiling_guard_restart_pending);
}
void TilingSpaceBeforeEnd() {
    if (!timing_enabled || !tiling_space_probe.active) return;
    DWORD used, remaining;
    QueryTilingSpace(&used, &remaining);
    tiling_space_probe.BeforeEnd(used, remaining);
}
bool FramebufferHiZCapable(bool owns_depth, uint32_t physical_width, uint32_t physical_height,
                           D3DMULTISAMPLE_TYPE samples) {
    return owns_depth &&
           XGHierarchicalZSize(physical_width, physical_height, samples) <=
               GPU_HIERARCHICAL_Z_TILES;
}
bool FreshFramebufferHiZ(bool capable, bool has_depth, bool retained_depth) {
    return capable && has_depth && !retained_depth;
}
void BeginFramebuffer() {
    if (live_framebuffer)
        return;
    if (pending_game_depth_clear == active_fb)
        pending_game_depth_clear = -1;
    NativeTimingScope timing(timing_counts.framebufferSetupTicks, timing_counts.framebufferSetupCalls);
    InvalidateNativeState();
    Framebuffer& b = buffers[active_fb];
    if (!b.color)
        Shipyard_Fatal("framebuffer render surface unavailable", E_INVALIDARG);
    const bool fresh_hiz = FreshFramebufferHiZ(b.hiz_capable, b.depth != NULL, b.depth_stored);
    // The previous target may have owned the shared HiZ allocation. Keep tests
    // disabled while the fresh depth clear seeds its metadata; force writes
    // only across this setup bracket, then use automatic gating for game draws.
    xenos_device->SetRenderState(D3DRS_HIZENABLE, D3DHIZ_DISABLE);
    xenos_device->SetRenderState(D3DRS_HIZWRITEENABLE,
                                 fresh_hiz ? D3DHIZ_ENABLE : D3DHIZ_DISABLE);
    D3DVIEWPORT9 viewport;
    xenos_device->GetViewport(&viewport);
    Shipyard_Check(xenos_device->SetRenderTarget(0, b.color), "bind framebuffer color");
    Shipyard_Check(xenos_device->SetDepthStencilSurface(b.depth), "bind framebuffer depth");
    Shipyard_Check(xenos_device->SetViewport(&viewport), "restore framebuffer viewport");
    xenos_device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
    xenos_device->SetRenderState(D3DRS_MULTISAMPLEMASK, ~0u);
    D3DVECTOR4 clear = {0, 0, 0, 1};
    if (b.tiles.size() > 1) {
        xenos_device->BeginTiling(0, (DWORD)b.tiles.size(), &b.tiles[0], &clear, 1, 0);
        TilingSpaceBegin();
    } else if (!b.stored || (b.depth && !b.depth_stored)) {
        DWORD flags =
            (!b.stored ? D3DCLEAR_TARGET : 0) | (b.depth ? D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL : 0);
        ClearFramebufferTargets(flags);
    }
    // Each MSAA fragment is retained independently. Resolving only the averaged
    // image here would destroy edge coverage and subsequent depth tests.
    if (b.stored) {
        const bool restore_constants = BeginRestoreQuad(b.width, b.height, b.depth_stored);
        for (uint32_t sample = 0; sample < b.samples; ++sample) {
            DrawRestoreQuadSample(b.samples > 1 ? b.color_samples[sample] : b.image,
                                  b.depth_stored ? b.depth_samples[sample] : NULL,
                                  b.samples > 1 ? 1u << sample : ~0u, NULL, sample == 0);
        }
        EndRestoreQuad(restore_constants);
    }
    // Retained depth is copied without rebuilding HiZ metadata. A fresh depth
    // clear initializes the metadata, including after a color-only restore.
    xenos_device->SetRenderState(D3DRS_HIZENABLE,
                                 fresh_hiz ? D3DHIZ_AUTOMATIC : D3DHIZ_DISABLE);
    xenos_device->SetRenderState(D3DRS_HIZWRITEENABLE,
                                 fresh_hiz ? D3DHIZ_AUTOMATIC : D3DHIZ_DISABLE);
    live_framebuffer = true;
    tiling_draw_bytes = 0;
    SetTileConstants();
    TilingSpaceSetup();
}
void RebindSampledTextures() {
    draw_state_prepared = false;
    for (int i = 0; i < 6; ++i) {
        IDirect3DTexture9* texture = NULL;
        const TextureSampler* sampler = NULL;
        if (selected_fb[i] >= 0) {
            texture = buffers[selected_fb[i]].image;
            sampler = &buffers[selected_fb[i]].sampler;
        } else if (selected[i] < textures.size()) {
            texture = textures[selected[i]].native;
            sampler = &textures[selected[i]].sampler;
        }
        SetNativeTexture(i, texture);
        if (sampler)
            ApplySampler(i, *sampler);
    }
}
// A depth resolve is about to overwrite the published texture: give the
// framebuffer other sample-zero storage. The published reference stays valid.
void ProtectPublishedDepth(Framebuffer& b) {
    IDirect3DTexture9* storage = published_depth.spare;
    published_depth.spare = NULL;
    if (storage && (published_depth.spare_width != b.width || published_depth.spare_height != b.height)) {
        storage->Release();
        storage = NULL;
    }
    if (!storage)
        Shipyard_Check(xenos_device->CreateTexture(b.width, b.height, 1, 0, D3DFMT_D24S8,
                                                   D3DPOOL_DEFAULT, &storage, NULL),
                       "published depth copy storage");
    b.depth_samples[0]->Release();
    b.depth_samples[0] = storage;
}
void FinishFramebuffer(bool preserveDepth, bool preserveColor) {
    // Reads, copies and target switches must observe the requested depth clear
    // even when no game draw has materialized the target yet.
    if (pending_game_depth_clear == active_fb)
        BeginFramebuffer();
    if (!live_framebuffer)
        return;
    NativeTimingScope timing(timing_counts.framebufferResolveTicks, timing_counts.framebufferResolveCalls);
    Framebuffer& b = buffers[active_fb];
    if (!preserveColor && (preserveDepth || active_fb != 0 || b.color != presentation_color ||
                           b.tiles.size() != 1))
        Shipyard_Fatal("presentation color discard contract", E_INVALIDARG);
    TilingSpaceBeforeResolve();
    StopTileConstants();
    for (int binding = 0; binding < 6; ++binding)
        SetNativeTexture(binding, NULL);
    if (b.depth && preserveDepth && published_depth.texture &&
        b.depth_samples[0] == published_depth.texture)
        ProtectPublishedDepth(b);
    const DWORD fragments[] = {D3DRESOLVE_FRAGMENT0, D3DRESOLVE_FRAGMENT1, D3DRESOLVE_FRAGMENT2,
                               D3DRESOLVE_FRAGMENT3};
    D3DVECTOR4 clear = {0, 0, 0, 1};
    for (size_t tile = 0; tile < b.tiles.size(); ++tile) {
        if (b.tiles.size() > 1)
            xenos_device->SetPredication(D3DPRED_TILE(tile));
        D3DRECT rectangle = b.tiles[tile];
        rectangle.x2 = std::min(rectangle.x2, (LONG)b.width);
        rectangle.y2 = std::min(rectangle.y2, (LONG)b.height);
        D3DPOINT destination = {rectangle.x1, rectangle.y1};
        for (uint32_t sample = 0; sample < b.samples; ++sample) {
            if (b.depth && preserveDepth) {
                Shipyard_Check(xenos_device->Resolve(D3DRESOLVE_DEPTHSTENCIL | fragments[sample],
                                                     &rectangle, b.depth_samples[sample],
                                                     &destination, 0, 0, NULL, 1, 0, NULL),
                               "retain depth fragment");
                ++framebuffer_depth_resolves;
            } else if (b.depth) {
                ++framebuffer_depth_resolves_skipped;
            }
            if (b.samples > 1)
                Shipyard_Check(xenos_device->Resolve(D3DRESOLVE_RENDERTARGET0 | fragments[sample],
                                                     &rectangle, b.color_samples[sample],
                                                     &destination, 0, 0, NULL, 1, 0, NULL),
                               "retain color fragment");
        }
        DWORD flags = D3DRESOLVE_RENDERTARGET0 | D3DRESOLVE_ALLFRAGMENTS;
        if (b.tiles.size() > 1)
            flags |= D3DRESOLVE_CLEARRENDERTARGET | (b.depth ? D3DRESOLVE_CLEARDEPTHSTENCIL : 0);
        if (preserveColor)
            Shipyard_Check(xenos_device->Resolve(flags, &rectangle, b.image, &destination, 0, 0,
                                                 &clear, 1, 0, NULL),
                           "resolve framebuffer color");
        else
            ++presentation_color_resolves_skipped;
    }
    if (b.tiles.size() > 1) {
        xenos_device->SetPredication(0);
        TilingSpaceBeforeEnd();
        // Resolves above are explicitly predicated per tile, including all depth
        // fragments. Disable EndTiling's automatic single-target resolve.
        Shipyard_Check(xenos_device->EndTiling(0, NULL, NULL, &clear, 1, 0, NULL),
                       "finish framebuffer tiling");
        tiling_budget.Close();
        tiling_guard_trial_window.Close();
    }
    // Seal the resolved depth resource's SDK fence before later presentation
    // commands enter the same interval. PixelDepth may lock this texture during
    // the next game update; this fence submits work but does not wait for it.
    // EndTiling must be complete before inserting a fence.
    if (b.tiles.size() > 1 && b.depth && preserveDepth) {
        xenos_device->InsertFence();
        ++framebuffer_early_depth_fences;
    }
    b.stored = preserveColor;
    b.depth_stored = preserveDepth && b.depth != NULL;
    live_framebuffer = false;
    InvalidateNativeState();
    RebindSampledTextures();
}
void Resolve(int id) {
    ValidateFramebuffer(id);
    if (active_fb == id && (live_framebuffer || pending_game_depth_clear == id))
        FinishFramebuffer();
}
int NewFramebuffer() {
    if (!free_framebuffers.empty()) {
        int id = free_framebuffers.back();
        free_framebuffers.pop_back();
        return id;
    }
    buffers.push_back(Framebuffer());
    return (int)buffers.size() - 1;
}
void ReleaseFramebuffer(Framebuffer& b) {
    if (b.color) {
        b.color->Release();
    }
    if (b.depth) {
        b.depth->Release();
    }
    if (b.image) {
        b.image->Release();
    }
    for (int i = 0; i < 4; ++i) {
        if (b.color_samples[i]) {
            b.color_samples[i]->Release();
        }
        if (b.depth_samples[i]) {
            b.depth_samples[i]->Release();
        }
    }
}
void UpdateFramebuffer(int id, uint32_t width, uint32_t height, uint32_t requested_samples, bool,
                       bool target, bool depth, bool) {
    // flip and readDepth are Fast3D compatibility arguments. Xenos uses native
    // orientation and retains depth fragments whenever depth is enabled.
    ValidateFramebuffer(id);
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    uint32_t samples = requested_samples >= 4 ? 4 : requested_samples >= 2 ? 2 : 1;
    if (width > 4096 || height > 4096)
        Shipyard_Fatal("framebuffer texture dimensions", E_INVALIDARG);
    Framebuffer& b = buffers[id];
    if (b.width == width && b.height == height && b.samples == samples &&
        (b.depth != NULL) == depth) {
        b.render_target = target;
        return;
    }
    bool resume = live_framebuffer;
    FinishFramebuffer();
    InvalidateNativeState();
    for (int binding = 0; binding < 6; ++binding) {
        SetNativeTexture(binding, NULL);
    }
    xenos_device->SetRenderTarget(0, NULL);
    xenos_device->SetDepthStencilSurface(NULL);
    xenos_device->BlockUntilIdle();
    // A state block may retain the last restore inputs. Drop those references
    // before resizing, so old and new full-size sample storage need not coexist.
    Shipyard_Check(restore_state->Capture(), "release framebuffer copy references");
    Framebuffer previous = b;
    b = Framebuffer();
    b.width = width;
    b.height = height;
    b.shader_width = (float)width;
    b.shader_height = (float)height;
    b.samples = samples;
    b.render_target = target;
    b.sampler = previous.sampler;
    bool retain_color =
        previous.width == width && previous.height == height && previous.samples == samples;
    if (retain_color) {
        b.image = previous.image;
        previous.image = NULL;
        b.stored = previous.stored;
        for (int i = 0; i < 4; ++i) {
            b.color_samples[i] = previous.color_samples[i];
            previous.color_samples[i] = NULL;
        }
    }
    ReleaseFramebuffer(previous);
    D3DMULTISAMPLE_TYPE msaa = samples == 4   ? D3DMULTISAMPLE_4_SAMPLES
                               : samples == 2 ? D3DMULTISAMPLE_2_SAMPLES
                                              : D3DMULTISAMPLE_NONE;
    UINT edram_width = samples == 4   ? GPU_EDRAM_TILE_WIDTH_4X
                       : samples == 2 ? GPU_EDRAM_TILE_WIDTH_2X
                                      : GPU_EDRAM_TILE_WIDTH_1X;
    UINT edram_height = samples == 4   ? GPU_EDRAM_TILE_HEIGHT_4X
                        : samples == 2 ? GPU_EDRAM_TILE_HEIGHT_2X
                                       : GPU_EDRAM_TILE_HEIGHT_1X;
    uint32_t tile_width = XGNextMultiple(width, edram_width),
             tile_height = XGNextMultiple(height, edram_height);
    UINT full_color = XGSurfaceSize(tile_width, tile_height, D3DFMT_A8R8G8B8, msaa);
    UINT full_depth = depth ? XGSurfaceSize(tile_width, tile_height, D3DFMT_D24S8, msaa) : 0;
    bool tiled = full_color + full_depth > 2048;
    if (tiled) {
        // Follow the SDK's PredicatedTiling sample: surface headers cover both
        // EDRAM alignment and texture-tile alignment used by predicated resolves.
        tile_width = XGNextMultiple(tile_width, 32);
        tile_height = XGNextMultiple(height, 32);
        do {
            if (tile_height <= 32)
                Shipyard_Fatal("framebuffer tile dimensions", E_INVALIDARG);
            tile_height -= 32;
        } while (XGSurfaceSize(tile_width, tile_height, D3DFMT_A8R8G8B8, msaa) +
                     (depth ? XGSurfaceSize(tile_width, tile_height, D3DFMT_D24S8, msaa) : 0) >
                 2048);
    }
    uint32_t virtual_width = tiled ? XGNextMultiple(width, 32) : width,
             virtual_height = tiled ? XGNextMultiple(height, 32) : height;
    for (uint32_t y = 0; y < height; y += tile_height) {
        D3DRECT tile = {0, (LONG)y, (LONG)virtual_width,
                        (LONG)std::min(y + tile_height, virtual_height)};
        b.tiles.push_back(tile);
    }
    // XDK predication addresses at most 15 tile rectangles per framebuffer.
    if (b.tiles.size() > 15)
        Shipyard_Fatal("framebuffer exceeds XDK tiling rectangle limit", E_INVALIDARG);
    UINT color_tiles = XGSurfaceSize(tile_width, tile_height, D3DFMT_A8R8G8B8, msaa);
    D3DSURFACE_PARAMETERS parameters = {0};
    parameters.Base = 0;
    if (id == 0 && width == 1280 && height == 720 && samples == 1) {
        b.color = presentation_color;
        b.color->AddRef();
    } else
        Shipyard_Check(xenos_device->CreateRenderTarget(tile_width, tile_height, D3DFMT_A8R8G8B8,
                                                        msaa, 0, FALSE, &b.color, &parameters),
                       "framebuffer color surface");
    if (depth) {
        parameters.Base = color_tiles;
        const bool owns_depth = !(id == 0 && width == 1280 && height == 720 && samples == 1);
        b.hiz_capable = FramebufferHiZCapable(owns_depth, tile_width, tile_height, msaa);
        if (!b.hiz_capable)
            parameters.HierarchicalZBase = 0xffffffffu;
        if (!owns_depth) {
            b.depth = presentation_depth;
            b.depth->AddRef();
        } else
            Shipyard_Check(xenos_device->CreateDepthStencilSurface(tile_width, tile_height,
                                                                   D3DFMT_D24S8, msaa, 0, FALSE,
                                                                   &b.depth, &parameters),
                           "framebuffer depth surface");
    }
    if (!b.image)
        Shipyard_Check(xenos_device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8,
                                                   D3DPOOL_DEFAULT, &b.image, NULL),
                       "framebuffer image");
    for (uint32_t sample = 0; sample < samples; ++sample) {
        if (samples > 1 && !b.color_samples[sample])
            Shipyard_Check(xenos_device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8,
                                                       D3DPOOL_DEFAULT, &b.color_samples[sample],
                                                       NULL),
                           "framebuffer color fragment storage");
        if (depth)
            Shipyard_Check(xenos_device->CreateTexture(width, height, 1, 0, D3DFMT_D24S8,
                                                       D3DPOOL_DEFAULT, &b.depth_samples[sample],
                                                       NULL),
                           "framebuffer depth fragment storage");
    }
    RebindSampledTextures();
    Shipyard_Log("framebuffer %d: %ux%u, requested %ux / native %ux MSAA, %u EDRAM tile passes", id,
                 width, height, requested_samples, samples, (unsigned)b.tiles.size());
    if (resume)
        BeginFramebuffer();
}
void BindFramebuffer(int id, float scale) {
    ValidateFramebuffer(id);
    if (!buffers[id].color || !buffers[id].render_target)
        Shipyard_Fatal("bind framebuffer", E_INVALIDARG);
    if (active_fb != id)
        FinishFramebuffer();
    active_fb = id;
    if (scale != 0 && scale != noise_scale_input) {
        draw_state_prepared = false;
        noise_scale = 1.0f / scale;
        noise_scale_input = scale;
    }
    // Defer restoration until drawing or setting target-dependent viewport /
    // scissor state. A full color/depth clear may follow this bind.
}
void BeginGameDepthClear() {
    if (live_framebuffer || !buffers[active_fb].depth) {
        Clear(false, true);
        return;
    }
    Framebuffer& b = buffers[active_fb];
    ++framebuffer_clear_count;
    if (b.stored && b.depth_stored)
        framebuffer_depth_restore_avoided += b.samples;
    b.depth_stored = false;
    pending_game_depth_clear = active_fb;
}
bool TryDiscardGameColor() {
    // The interpreter proves full opaque coverage before emitting its first
    // color draw. Native state can still reject the hint; MSAA stays conservative.
    if (pending_game_depth_clear != active_fb || live_framebuffer || !color_writes ||
        !buffers[active_fb].depth || buffers[active_fb].samples != 1)
        return false;
    Framebuffer& b = buffers[active_fb];
    if (b.stored)
        framebuffer_restore_avoided += b.samples;
    b.stored = false;
    return true;
}
void Clear(bool color, bool depth) {
    if (!color && !depth)
        return;
    ++framebuffer_clear_count;
    const bool wasLive = live_framebuffer;
    const bool begin_clears_depth = !wasLive && depth && !color && buffers[active_fb].depth;
    const bool begin_clears_both = !wasLive && color && depth &&
                                   buffers[active_fb].depth && buffers[active_fb].tiles.size() == 1;
    if (!live_framebuffer) {
        Framebuffer& b = buffers[active_fb];
        // Discard retained samples before a full clear while preserving Fast3D's
        // independent attachment contract. Color-only clears must
        // still restore depth. Depth-only clears retain every color sample.
        if (depth) {
            if (b.stored && b.depth_stored)
                framebuffer_depth_restore_avoided += b.samples;
            b.depth_stored = false;
        }
        if (color && (depth || !b.depth)) {
            if (b.stored)
                framebuffer_restore_avoided += b.samples;
            b.stored = false;
        }
    }
    BeginFramebuffer();
    // A fresh tiled bracket clears depth/stencil for every tile; a fresh
    // single-tile bracket clears depth when the retained depth was discarded.
    // The existing full-clear shortcut applies only to single-tile targets.
    if (begin_clears_depth || begin_clears_both)
        return;
    const DWORD flags =
        (color ? D3DCLEAR_TARGET : 0) |
        ((depth && buffers[active_fb].depth) ? D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL : 0);
    if (flags)
        ClearFramebufferTargets(flags);
}
void ResolveMSAA(int dst, int src) {
    ValidateFramebuffer(dst);
    ValidateFramebuffer(src);
    if (dst == src) {
        Resolve(src);
        return;
    }
    int previous = active_fb;
    FinishFramebuffer();
    Framebuffer& source = buffers[src];
    Framebuffer& target = buffers[dst];
    if (source.width != target.width || source.height != target.height || target.samples != 1 ||
        !source.stored) {
        Shipyard_Log("MSAA resolve src=%d %ux%u samples=%u stored=%u dst=%d %ux%u samples=%u", src,
                     source.width, source.height, source.samples, (unsigned)source.stored, dst,
                     target.width, target.height, target.samples);
        Shipyard_Fatal("MSAA resolve dimensions/source", E_INVALIDARG);
    }
    active_fb = dst;
    BeginFramebuffer();
    RestoreQuad(source.image, NULL, target.width, target.height, ~0u);
    FinishFramebuffer();
    active_fb = previous;
    BeginFramebuffer();
}
std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
PixelDepth(int id, const std::set<std::pair<float, float> >& coords) {
    ValidateFramebuffer(id);
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> result;
    if (coords.empty())
        return result;
    NativeTimingScope depthReading(timing_counts.depthReadTicks, timing_counts.depthReadCalls);
    if (timing_enabled)
        timing_counts.depthSamples += (unsigned)coords.size();
    bool resume = false;
    IDirect3DTexture9* depth_texture;
    uint32_t width, height;
    if (published_depth.texture && published_depth.framebuffer == id) {
        // The previous update's final replay. Later replays of this update
        // never wrote this texture, so no framebuffer work is needed; the lock
        // waits only for that replay's depth resolve.
        depth_texture = published_depth.texture;
        width = published_depth.width;
        height = published_depth.height;
    } else {
        resume = live_framebuffer;
        FinishFramebuffer();
        Framebuffer& b = buffers[id];
        // A caller may read depth before its next draw after switching targets.
        // Xenos retains actual depth fragments regardless of the draw-side hint.
        if (!b.depth_samples[0])
            Shipyard_Fatal("depth readback unavailable", E_FAIL);
        if (!b.depth_stored) {
            // A depth query can precede the first draw after an MSAA or resolution
            // change. Initialize real GPU color/depth storage through the same
            // clear/resolve path used for the first draw, then read sample zero.
            int previous = active_fb;
            active_fb = id;
            BeginFramebuffer();
            FinishFramebuffer();
            active_fb = previous;
            Shipyard_Log("initialized depth before first draw: framebuffer=%d samples=%u", id,
                         b.samples);
        }
        depth_texture = b.depth_samples[0];
        width = b.width;
        height = b.height;
    }
    if (timing_enabled) {
        // IsBusy is a nonblocking state sample. Do not pre-wait here: that
        // would change the LockRect schedule being measured.
        const bool busy = depth_texture->IsBusy() != FALSE;
        const bool is1080 = width == 1920 && height == 1080;
        if (busy) {
            ++timing_counts.depthBusyCalls;
            if (is1080)
                ++timing_counts.depthBusy1080Calls;
        } else {
            ++timing_counts.depthIdleCalls;
            if (is1080)
                ++timing_counts.depthIdle1080Calls;
        }
    }
    D3DLOCKED_RECT lock;
    {
        NativeTimingScope depthLock(timing_counts.depthLockTicks, timing_counts.depthLockCalls);
        Shipyard_Check(depth_texture->LockRect(0, &lock, NULL, D3DLOCK_READONLY), "depth LockRect");
    }
    XGTEXTURE_DESC desc;
    XGGetTextureDesc(depth_texture, 0, &desc);
    // The caller asks for individual occlusion samples. Let the SDK address
    // those texels directly instead of untiling an entire framebuffer.
    // The source rectangle retains texture packing and the bottom-left contract.
    for (std::set<std::pair<float, float> >::const_iterator it = coords.begin(); it != coords.end();
         ++it) {
        int x = (int)it->first, y = (int)it->second;
        if (x < 0 || y < 0 || x >= (int)width || y >= (int)height) {
            result[*it] = 65532;
            continue;
        }
        DWORD value;
        POINT destination = {0, 0};
        RECT source = {x, (LONG)height - 1 - y, x + 1, (LONG)height - y};
        XGUntileTextureLevel(width, height, 0, XGGetGpuFormat(desc.Format), 0, &value,
                             sizeof(value), &destination, lock.pBits, &source);
        // Match the Fast3D D3D11 backend's sample-zero read for multisampled depth.
        result[*it] = (uint16_t)(((value >> 8) / 16777215.0) * 65532.0);
    }
    depth_texture->UnlockRect(0);
    if (resume)
        BeginFramebuffer();
    return result;
}
void* FramebufferImage(int id) {
    Resolve(id);
    if (id == 0)
        presentation_image_ever_published = true;
    IDirect3DTexture9* image = buffers[id].image;
    if (image != published_framebuffer_image) {
        if (image)
            image->AddRef();
        IDirect3DTexture9* previous = published_framebuffer_image;
        published_framebuffer_image = image;
        if (previous)
            ReleaseTexture(previous);
    }
    return image;
}
void SelectFramebuffer(int id) {
    Resolve(id);
    selected_fb[0] = id;
    SetNativeTexture(0, buffers[id].image);
    ApplySampler(0, buffers[id].sampler);
}

void DeleteTexture(uint32_t id) {
    draw_state_prepared = false;
    UploadSnapshot& snapshot = upload_snapshots[id & (UPLOAD_SNAPSHOT_SLOTS - 1)];
    if (snapshot.id == id)
        snapshot.native = NULL;
    if (id < textures.size() && textures[id].native) {
        for (int i = 0; i < 6; ++i)
            if (selected_fb[i] < 0 && selected[i] == id)
                SetNativeTexture(i, NULL);
        ReleaseTexture(textures[id].native);
        textures[id] = Texture();
    }
}
void Filter(FilteringMode mode) {
    if (filtering == mode)
        return;
    draw_state_prepared = false;
    filtering = mode;
    filtering_key = unsigned(mode) << 4;
}
FilteringMode GetFilter() {
    return filtering;
}
void InitializeDisplay() {
    if (xenos_device)
        return;
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d)
        Shipyard_Fatal("Direct3DCreate9", E_FAIL);
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.FrontBufferFormat = D3DFMT_LE_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    // The XDK owns this ring; two bounded tiling brackets can coexist while the
    // CPU records the next one.
    pp.RingBufferParameters.SecondarySize = 8 * 1024 * 1024;
    // Presentation owns its front buffers and swaps asynchronously; see
    // presentation/xenos_swap_chain.h. The device keeps its EDRAM back buffer.
    pp.DisableAutoFrontBuffer = TRUE;
    Shipyard_Check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                     &pp, &xenos_device),
                   "CreateDevice 720p");
    Shipyard_Check(::Shipyard::XenosPresentationChain().Create(xenos_device, pp),
                   "presentation front buffers");
    d3d->Release();
    Shipyard_Log("Xenos native display ready before system UI");
}
void Init() {
    InvalidateNativeState();
    memset(&native_state_counts, 0, sizeof(native_state_counts));
    InitializeDisplay();
    Shipyard_Check(xenos_device->GetRenderTarget(0, &presentation_color), "presentation color");
    Shipyard_Check(xenos_device->GetDepthStencilSurface(&presentation_depth), "presentation depth");
    InitializeRestore();
    // Fast3D's zero-initialized depth-state cache means disabled test/write.
    // Xenos defaults to both enabled. Match the cache before the first draw,
    // even if the caller never explicitly sets depth state.
    Depth(false, false);
    // Aliased targets restore exact per-pixel depth; do not use stale hierarchical
    // depth metadata from a different EDRAM owner.
    xenos_device->SetRenderState(D3DRS_HIZENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_HIZWRITEENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_HISTENCILENABLE, FALSE);
    xenos_device->SetRenderState(D3DRS_HISTENCILWRITEENABLE, FALSE);
    NewFramebuffer();
    xenos_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    Shipyard_Log("Xenos 1280x720: shared EDRAM, tiled 1x/2x/4x MSAA, color/depth retention");
}
void Resize() {
    FinishFramebuffer();
}
void Start() {
    ++frame_number;
    shader_frame_number = (float)frame_number;
    InvalidateNativeState();
    CollectTextures(false);
}
void ReadFramebufferPixels(IDirect3DTexture9* image, uint32_t width, uint32_t height,
                           std::vector<DWORD>& pixels) {
    D3DLOCKED_RECT lock;
    Shipyard_Check(image->LockRect(0, &lock, NULL, D3DLOCK_READONLY),
                   "runtime framebuffer readback");
    pixels.resize(width * height);
    XGTEXTURE_DESC desc;
    XGGetTextureDesc(image, 0, &desc);
    XGUntileTextureLevel(width, height, 0, XGGetGpuFormat(desc.Format), 0, &pixels[0], width * 4,
                         NULL, lock.pBits, NULL);
    image->UnlockRect(0);
}
void End() {
    const bool discardPresentationDepth = discard_presentation_depth_on_end;
    discard_presentation_depth_on_end = false;
    const bool discardPresentationColor = discard_presentation_color_on_end;
    discard_presentation_color_on_end = false;
    bool canDiscardPresentationColor = false;
    if (discardPresentationColor && discardPresentationDepth && buffers[0].color == presentation_color &&
        buffers[0].tiles.size() == 1 && !presentation_image_ever_published) {
        canDiscardPresentationColor = true;
        for (int binding = 0; binding < 6; ++binding)
            if (selected_fb[binding] == 0)
                canDiscardPresentationColor = false;
    }
    if (discardPresentationColor && !canDiscardPresentationColor)
        ++presentation_color_discard_blocked;
    if (live_framebuffer || pending_game_depth_clear == active_fb || !buffers[active_fb].stored) {
        BeginFramebuffer();
        if (active_fb == 0)
            FinishFramebuffer(!discardPresentationDepth, !canDiscardPresentationColor);
        else
            FinishFramebuffer();
    } else if (active_fb == 0) {
        // A read-only framebuffer image already completed the resolve. Keep
        // that result until the next actual draw, and apply end-of-frame
        // discard policy without restoring its old contents first.
        if (discardPresentationDepth)
            buffers[0].depth_stored = false;
        if (canDiscardPresentationColor)
            buffers[0].stored = false;
    }
    if (active_fb != 0) {
        active_fb = 0;
        // Another target may have occupied the shared EDRAM even when the
        // presentation image was already resolved. It still needs restoration.
        BeginFramebuffer();
        FinishFramebuffer(!discardPresentationDepth, !canDiscardPresentationColor);
    }
    if (buffers[0].color != presentation_color) {
        Shipyard_Check(xenos_device->SetRenderTarget(0, presentation_color), "presentation target");
        xenos_device->SetDepthStencilSurface(NULL);
        RestoreQuad(buffers[0].image, NULL, 1280, 720, ~0u);
    }
    // Close the SDK's resource-use interval before Present inserts its vblank
    // synchronization. Otherwise LockRect on the resolved previous-frame depth
    // can wait for unrelated presentation commands in that same interval.
    // InsertFence kicks queued draws/resolves and updates native resource fences;
    // LockRect retains its normal SDK wait.
    // All predicated tiling has ended above, as required by the SDK.
    xenos_device->InsertFence();
}
void Finish() {
    // End() completes the framebuffer resolve and inserts the native fence.
}
} // namespace
void Shipyard_ShutdownGraphics() {
    InvalidateNativeState();
    if (!xenos_device)
        return;
    FinishFramebuffer();
    InvalidateNativeState();
    for (int binding = 0; binding < 6; ++binding) {
        SetNativeTexture(binding, NULL);
    }
    xenos_device->SetVertexShader(NULL);
    xenos_device->SetPixelShader(NULL);
    xenos_device->SetVertexDeclaration(NULL);
    xenos_device->SetRenderTarget(0, NULL);
    xenos_device->SetDepthStencilSurface(NULL);
    xenos_device->BlockUntilIdle();
    CollectTextures(true);
    if (published_framebuffer_image) {
        published_framebuffer_image->Release();
        published_framebuffer_image = NULL;
    }
    ShipyardXenos::RetireFramebufferDepth();
    if (published_depth.spare) {
        published_depth.spare->Release();
        published_depth.spare = NULL;
    }
    // State blocks retain resources too. Release them before the owned textures
    // and surfaces, then destroy the device to unregister XDK GPU callbacks.
    if (restore_state) {
        restore_state->Release();
        restore_state = NULL;
    }
    for (size_t i = 0; i < buffers.size(); ++i) {
        ReleaseFramebuffer(buffers[i]);
    }
    buffers.clear();
    free_framebuffers.clear();
    for (size_t i = 0; i < textures.size(); ++i) {
        if (textures[i].native) {
            textures[i].native->Release();
        }
    }
    textures.clear();
    for (unsigned i = 0; i < UPLOAD_SNAPSHOT_SLOTS; ++i) {
        upload_snapshots[i].native = NULL;
        std::vector<BYTE>().swap(upload_snapshots[i].rgba);
    }
    for (std::map<ShaderProgramKey, ShaderProgram*>::iterator it = shaders.begin();
         it != shaders.end(); ++it) {
        ShaderProgram* shader = it->second;
        shader->vertex->Release();
        shader->pixel->Release();
        shader->declaration->Release();
        delete shader;
    }
    shaders.clear();
    vertex_programs.clear();
    vertex_declarations.clear();
    current_shader = NULL;
    if (restore_vertex) {
        restore_vertex->Release();
        restore_vertex = NULL;
    }
    if (restore_color) {
        restore_color->Release();
        restore_color = NULL;
    }
    if (restore_depth) {
        restore_depth->Release();
        restore_depth = NULL;
    }
    if (restore_declaration) {
        restore_declaration->Release();
        restore_declaration = NULL;
    }
    if (presentation_color) {
        presentation_color->Release();
        presentation_color = NULL;
    }
    if (presentation_depth) {
        presentation_depth->Release();
        presentation_depth = NULL;
    }
    ::Shipyard::XenosPresentationChain().Release();
    ULONG remaining = xenos_device->Release();
    xenos_device = NULL;
    Shipyard_Log("graphics shutdown device_references=%lu", remaining);
}

namespace {
void CopyRegion(int dst, int src, int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1,
                int dy1) {
    ValidateFramebuffer(src);
    ValidateFramebuffer(dst);
    if (sx1 <= sx0 || sy1 <= sy0 || dx1 <= dx0 || dy1 <= dy0)
        return;
    Framebuffer& source = buffers[src];
    Framebuffer& target = buffers[dst];
    if (sx0 < 0 || sy0 < 0 || sx1 > (int)source.width || sy1 > (int)source.height || dx0 < 0 ||
        dy0 < 0 || dx1 > (int)target.width || dy1 > (int)target.height)
        Shipyard_Fatal("framebuffer copy rectangle", E_INVALIDARG);
    const int previous = active_fb;
    const bool resume = live_framebuffer;
    FinishFramebuffer();
    if (!source.stored) {
        active_fb = src;
        BeginFramebuffer();
        FinishFramebuffer();
    }
    // All reads come from the stored image. EDRAM is separate, so overlapping
    // self-copy also samples an immutable snapshot until the final resolve.
    active_fb = dst;
    BeginFramebuffer();
    const float left = 2.0f * dx0 / target.width - 1.0f, right = 2.0f * dx1 / target.width - 1.0f;
    const float top = 1.0f - 2.0f * dy0 / target.height, bottom = 1.0f - 2.0f * dy1 / target.height;
    const float u0 = (float)sx0 / source.width, u1 = (float)sx1 / source.width;
    const float v0 = (float)sy0 / source.height, v1 = (float)sy1 / source.height;
    const float quad[] = {left, top,    u0, v0, right, top, u1, v0, left,  bottom, u0, v1,
                          left, bottom, u0, v1, right, top, u1, v0, right, bottom, u1, v1};
    RestoreQuad(source.image, NULL, target.width, target.height, ~0u, quad);
    FinishFramebuffer();
    active_fb = previous;
    if (resume)
        BeginFramebuffer();
}
void ReadColor(int id, uint32_t width, uint32_t height, uint16_t* rgba16) {
    ValidateFramebuffer(id);
    if (!rgba16 || !width || !height)
        return;
    if (width > 4096 || height > 4096)
        Shipyard_Fatal("framebuffer readback dimensions", E_INVALIDARG);
    const int previous = active_fb;
    const bool resume = live_framebuffer;
    FinishFramebuffer();
    Framebuffer& b = buffers[id];
    if (!b.stored) {
        active_fb = id;
        BeginFramebuffer();
        FinishFramebuffer();
    }
    std::vector<DWORD> pixels;
    ReadFramebufferPixels(b.image, b.width, b.height, pixels);
    memset(rgba16, 0, (size_t)width * height * sizeof(uint16_t));
    for (uint32_t y = 0; y < std::min(height, b.height); ++y)
        for (uint32_t x = 0; x < std::min(width, b.width); ++x) {
            const DWORD p = pixels[y * b.width + x];
            // Same rounded RGB5551 conversion as pinned DX11, with Xenos' ARGB
            // native readback channels. Caller owns native uint16_t words.
            rgba16[(size_t)y * width + x] = Shipyard::Xenos::PackReadbackPixel(p);
        }
    active_fb = previous;
    if (resume)
        BeginFramebuffer();
}
void DestroyFramebufferNative(int id) {
    if (id < 0 || (size_t)id >= buffers.size() ||
        std::find(free_framebuffers.begin(), free_framebuffers.end(), id) !=
            free_framebuffers.end())
        return;
    const bool resume = live_framebuffer;
    FinishFramebuffer();
    for (int slot = 0; slot < 6; ++slot)
        if (selected_fb[slot] == id) {
            selected_fb[slot] = -1;
            selected[slot] = UINT32_MAX;
            SetNativeTexture(slot, NULL);
        }
    xenos_device->SetRenderTarget(0, NULL);
    xenos_device->SetDepthStencilSurface(NULL);
    xenos_device->BlockUntilIdle();
    Shipyard_Check(restore_state->Capture(), "release destroyed framebuffer references");
    ReleaseFramebuffer(buffers[id]);
    buffers[id] = Framebuffer();
    free_framebuffers.push_back(id);
    if (active_fb == id) {
        active_fb = -1;
        for (size_t candidate = 0; candidate < buffers.size(); ++candidate)
            if (std::find(free_framebuffers.begin(), free_framebuffers.end(), (int)candidate) ==
                    free_framebuffers.end() &&
                buffers[candidate].color) {
                active_fb = (int)candidate;
                break;
            }
    }
    InvalidateNativeState();
    if (resume && active_fb >= 0)
        BeginFramebuffer();
}
void ClearRegion(float x0, float y0, float x1, float y1) {
    BeginFramebuffer();
    const Framebuffer& b = buffers[active_fb];
    x0 = std::max(0.0f, std::min(1.0f, x0));
    x1 = std::max(0.0f, std::min(1.0f, x1));
    y0 = std::max(0.0f, std::min(1.0f, y0));
    y1 = std::max(0.0f, std::min(1.0f, y1));
    if (x1 <= x0 || y1 <= y0)
        return;
    D3DRECT rect = {(LONG)(x0 * b.width), (LONG)(y0 * b.height), (LONG)(x1 * b.width),
                    (LONG)(y1 * b.height)};
    D3DVIEWPORT9 previous;
    DWORD scissor;
    xenos_device->GetViewport(&previous);
    xenos_device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
    D3DVIEWPORT9 full = {0, 0, b.width, b.height, 0, 1};
    xenos_device->SetViewport(&full);
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    Shipyard_Check(xenos_device->Clear(1, &rect, D3DCLEAR_TARGET, 0xff000000, 1, 0),
                   "framebuffer color region clear");
    xenos_device->SetViewport(&previous);
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE, scissor);
}
} // namespace

namespace ShipyardXenos {
void DiscardPresentationDepthOnEnd() {
    discard_presentation_depth_on_end = true;
}
void DiscardPresentationColorOnEnd() {
    discard_presentation_color_on_end = true;
}
void DiscardFramebufferDepth(int id) {
    if (live_framebuffer && active_fb == id && buffers[id].depth)
        FinishFramebuffer(false);
}
void RetireFramebufferDepth() {
    IDirect3DTexture9* texture = published_depth.texture;
    if (!texture)
        return;
    const int id = published_depth.framebuffer;
    published_depth.texture = NULL;
    published_depth.framebuffer = -1;
    const bool owned = id >= 0 && (size_t)id < buffers.size() && buffers[id].depth_samples[0] == texture;
    if (owned || published_depth.spare) {
        texture->Release();
        return;
    }
    // Copied away from: keep it as storage for the next copy.
    published_depth.spare = texture;
    published_depth.spare_width = published_depth.width;
    published_depth.spare_height = published_depth.height;
}
void PublishFramebufferDepth(int id) {
    RetireFramebufferDepth();
    if (id < 0 || (size_t)id >= buffers.size() ||
        std::find(free_framebuffers.begin(), free_framebuffers.end(), id) != free_framebuffers.end())
        return;
    // Only depth the final replay retained is published. Otherwise reads take
    // the framebuffer path, exactly as before publication existed.
    const Framebuffer& b = buffers[id];
    if (live_framebuffer && active_fb == id)
        return;
    if (!b.depth || !b.depth_stored || !b.depth_samples[0])
        return;
    b.depth_samples[0]->AddRef();
    published_depth.texture = b.depth_samples[0];
    published_depth.framebuffer = id;
    published_depth.width = b.width;
    published_depth.height = b.height;
}
void SetShaderCache(const ShaderCacheOps* operations) {
    if (operations)
        shader_cache_ops = *operations;
    else {
        shader_cache_ops.load = NULL;
        shader_cache_ops.store = NULL;
    }
}
void SetTimingEnabled(bool enabled) {
#if !SHIPYARD_XENOS_DIAGNOSTICS
    // The normal XEX contains no hot-path diagnostic writes or timing reads.
    // An explicitly instrumented build defines SHIPYARD_XENOS_DIAGNOSTICS=1.
    (void)enabled;
    timing_enabled = false;
    return;
#else
    if (enabled == timing_enabled)
        return;
    memset(&timing_counts, 0, sizeof(timing_counts));
    if (!enabled) tiling_space_probe.active = false;
    timing_enabled = enabled;
    if (enabled && !timing_frequency) {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        timing_frequency = (uint64_t)frequency.QuadPart;
    }
#endif
}
void SetTilingGuardTrialEnabled(bool enabled) {
    if (tiling_guard_trial_enabled == enabled) return;
    tiling_guard_trial_enabled = enabled;
    if (!enabled) tiling_guard_trial_window.Close();
}
void SetTile1PredicationTrialEnabled(bool enabled) {
    tile1_predication_trial_enabled = enabled;
}
void CaptureAndResetTimingCounters(NativeTimingCounters* counters) {
    if (!counters)
        return;
    *counters = timing_counts;
    counters->enabled = timing_enabled;
    counters->frequency = timing_frequency;
    if (active_fb >= 0 && (size_t)active_fb < buffers.size()) {
        const Framebuffer& b = buffers[active_fb];
        counters->activeWidth = b.width;
        counters->activeHeight = b.height;
        counters->activeSamples = b.samples;
        counters->activeTiles = (unsigned)b.tiles.size();
    }
    if (xenos_device) {
        DWORD value;
        xenos_device->GetRenderState(D3DRS_BUFFER2FRAMES, &value);
        counters->buffer2Frames = value;
        xenos_device->GetRenderState(D3DRS_PRESENTINTERVAL, &value);
        counters->presentationInterval = value;
    }
    if (presentation_color) {
        D3DSURFACE_DESC description;
        if (SUCCEEDED(presentation_color->GetDesc(&description))) {
            counters->presentationWidth = description.Width;
            counters->presentationHeight = description.Height;
        }
    }
    memset(&timing_counts, 0, sizeof(timing_counts));
}
void CaptureAndResetCounters(NativeCounters* counters) {
    if (!counters)
        return;
    memset(counters, 0, sizeof(*counters));
    alpha_histogram.CopyAndReset(counters->alphaDraws, counters->alphaTriangles,
                                 counters->alphaVertices);
    counters->samplerAttempted = native_state_counts.sampler_attempted;
    counters->samplerWritten = native_state_counts.sampler_written;
    counters->samplerSuppressed = native_state_counts.sampler_suppressed;
    counters->constantsAttempted = native_state_counts.constants_attempted;
    counters->constantsWritten = native_state_counts.constants_written;
    counters->constantsSuppressed = native_state_counts.constants_suppressed;
    counters->bindingAttempted = binding_attempted;
    counters->bindingWritten = binding_written;
    counters->bindingSuppressed = binding_suppressed;
    counters->uploadRequests = upload_counts.requests;
    counters->uploadReused = upload_counts.reused;
    counters->uploadSavedBytes = upload_counts.bytes_saved;
    counters->clears = framebuffer_clear_count;
    counters->restoresAvoided = framebuffer_restore_avoided;
    counters->depthRestoresAvoided = framebuffer_depth_restore_avoided;
    counters->depthResolves = framebuffer_depth_resolves;
    counters->depthResolvesSkipped = framebuffer_depth_resolves_skipped;
    counters->earlyDepthFences = framebuffer_early_depth_fences;
    counters->tilingDrawRestarts = tiling_draw_restarts;
    counters->tilingPeakBytes = tiling_peak_bytes;
    counters->tiling1080Batches = tiling_1080_batches;
    counters->tiling1080Tile1Batches = tiling_1080_tile1_batches;
    counters->tiling1080Vertices = tiling_1080_vertices;
    counters->tiling1080Tile1Vertices = tiling_1080_tile1_vertices;
    counters->tiling1080OversizeBatches = tiling_1080_oversize_batches;
    counters->ringSameSegmentDraws = ring_same_segment_draws;
    counters->ringAmbiguousDraws = ring_ambiguous_draws;
    counters->ringSegmentChangedDraws = ring_segment_changed_draws;
    counters->ringMaxSameSegmentBytes = ring_max_same_segment_bytes;
    counters->ringSameSegmentBytes = ring_same_segment_bytes;
    counters->ringSameSegmentEstimatedBytes = ring_same_segment_estimated_bytes;
    counters->tilingSpace = tiling_space_probe.summary;
    counters->tilingGuardTrial = tiling_guard_trial_window.stats;
    counters->tile1PredicationTrial = tile1_predication_trial_counts;
    counters->presentationColorResolvesSkipped = presentation_color_resolves_skipped;
    counters->presentationColorDiscardBlocked = presentation_color_discard_blocked;
    counters->pendingTextures = (unsigned)pending_texture_releases.size();
    counters->pendingBytes = (unsigned)pending_texture_bytes;
    for (size_t i = 0; i < buffers.size(); ++i)
        if (published_framebuffer_image && buffers[i].image == published_framebuffer_image) {
            counters->width = buffers[i].width;
            counters->height = buffers[i].height;
            counters->samples = buffers[i].samples;
            counters->tiles = (unsigned)buffers[i].tiles.size();
            break;
        }
    memset(&native_state_counts, 0, sizeof(native_state_counts));
    memset(&upload_counts, 0, sizeof(upload_counts));
    binding_attempted = binding_written = binding_suppressed = 0;
    framebuffer_clear_count = framebuffer_restore_avoided = framebuffer_depth_restore_avoided = 0;
    framebuffer_depth_resolves = framebuffer_depth_resolves_skipped = 0;
    framebuffer_early_depth_fences = 0;
    tiling_draw_restarts = tiling_peak_bytes = 0;
    tiling_1080_batches = tiling_1080_tile1_batches = 0;
    tiling_1080_vertices = tiling_1080_tile1_vertices = 0;
    tiling_1080_oversize_batches = 0;
    ring_same_segment_draws = ring_ambiguous_draws = ring_segment_changed_draws = 0;
    ring_max_same_segment_bytes = 0;
    ring_same_segment_bytes = ring_same_segment_estimated_bytes = 0;
    tiling_space_probe.ResetSummary();
    tiling_guard_trial_window.ResetStats();
    memset(&tile1_predication_trial_counts, 0, sizeof(tile1_predication_trial_counts));
    presentation_color_resolves_skipped = presentation_color_discard_blocked = 0;
}
GfxClipParameters ClipParameters() {
    return ::Fast::ClipParameters();
}
void UnloadShader(ShaderProgram* shader) {
    ::Fast::UnloadShader(shader);
}
void LoadShader(ShaderProgram* shader) {
    ::Fast::LoadShader(shader);
}
ShaderProgram* CreateShader(const ShaderProgramKey& key) {
    return ::Fast::CreateShader(key);
}
ShaderProgram* LookupShader(const ShaderProgramKey& key) {
    return ::Fast::Lookup(key);
}
ShaderProgramKey ShaderKey(const ShaderProgram* shader) {
    return shader->key;
}
ShaderProgram* CurrentShader() {
    return current_shader;
}
void ShaderInfo(ShaderProgram* shader, uint8_t* inputs, bool used[2]) {
    ::Fast::ShaderInfo(shader, inputs, used);
}
uint32_t NewTexture() {
    return ::Fast::NewTexture();
}
void SelectTexture(int slot, uint32_t id) {
    ::Fast::SelectTexture(slot, id);
}
void UploadTexture(const uint8_t* pixels, uint32_t width, uint32_t height) {
    ::Fast::Upload(pixels, width, height);
}
void SetSamplerParameters(int slot, bool linear, uint32_t addressS, uint32_t addressT) {
    ::Fast::Sampler(slot, linear, addressS, addressT);
}
void SetDepthTestAndMask(bool test, bool write) {
    ::Fast::Depth(test, write);
}
void SetZmodeDecal(bool decal) {
    ::Fast::Decal(decal);
}
void SetViewport(int x, int y, int width, int height) {
    ::Fast::Viewport(x, y, width, height);
}
void SetScissor(int x, int y, int width, int height) {
    ::Fast::Scissor(x, y, width, height);
}
void SetUseAlpha(bool alpha) {
    ::Fast::Alpha(alpha);
}
void DrawTriangles(float* vertices, size_t count, size_t triangles) {
    ::Fast::Draw(vertices, count, triangles);
}
void InitializeDisplay() {
    ::Fast::InitializeDisplay();
}
HRESULT PresentStartupUI() {
    if (!xenos_device || !buffers.empty())
        return E_UNEXPECTED;
    HRESULT result = xenos_device->Clear(
        0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xff000000, 1, 0);
    return FAILED(result) ? result : ::Shipyard::XenosPresentationChain().Present();
}
void BeginGuiDraw() {
    ::Fast::BeginFramebuffer();
}
void InvalidateGuiState() {
    ::Fast::InvalidateNativeState();
}
void Init() {
    ::Fast::Init();
}
void Resize() {
    ::Fast::Resize();
}
void StartFrame() {
    ::Fast::Start();
}
void EndFrame() {
    ::Fast::End();
}
void FinishRender() {
    ::Fast::Finish();
}
void Shutdown() {
    ::Fast::Shipyard_ShutdownGraphics();
}
int CreateFramebuffer() {
    return ::Fast::NewFramebuffer();
}
void DestroyFramebuffer(int id) {
    ::Fast::DestroyFramebufferNative(id);
}
void UpdateFramebuffer(int id, uint32_t width, uint32_t height, uint32_t msaa, bool flip,
                       bool target, bool depth, bool readDepth) {
    ::Fast::UpdateFramebuffer(id, width, height, msaa, flip, target, depth, readDepth);
}
void StartDrawToFramebuffer(int id, float noiseScale) {
    ::Fast::BindFramebuffer(id, noiseScale);
}
void CopyFramebuffer(int destination, int source, int sourceX0, int sourceY0, int sourceX1,
                     int sourceY1, int destinationX0, int destinationY0, int destinationX1,
                     int destinationY1) {
    ::Fast::CopyRegion(destination, source, sourceX0, sourceY0, sourceX1, sourceY1, destinationX0,
                       destinationY0, destinationX1, destinationY1);
}
void ClearFramebuffer(bool color, bool depth) {
    ::Fast::Clear(color, depth);
}
void BeginGameFramebufferDepthClear() {
    ::Fast::BeginGameDepthClear();
}
bool TryDiscardGameFramebufferColor() {
    return ::Fast::TryDiscardGameColor();
}
void SetColorWriteMask(bool enabled) {
    color_writes = enabled;
    native_state.alpha_valid = false;
    ::Fast::ApplyAlpha();
}
void ClearColorRegion(float left, float top, float right, float bottom) {
    ::Fast::ClearRegion(left, top, right, bottom);
}
void ReadFramebufferToCPU(int id, uint32_t width, uint32_t height, uint16_t* pixels) {
    ::Fast::ReadColor(id, width, height, pixels);
}
void ResolveMSAAColorBuffer(int destination, int source) {
    ::Fast::ResolveMSAA(destination, source);
}
std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
GetPixelDepth(int id, const std::set<std::pair<float, float> >& coordinates) {
    return ::Fast::PixelDepth(id, coordinates);
}
void* GetFramebufferTextureId(int id) {
    return ::Fast::FramebufferImage(id);
}
void SelectTextureFb(int id) {
    ::Fast::SelectFramebuffer(id);
}
bool FbNeedsSampleVFlip(int id) {
    ::Fast::ValidateFramebuffer(id);
    // Native framebuffer samples are already in the orientation expected by Fast3D.
    return false;
}
void DeleteTexture(uint32_t id) {
    ::Fast::DeleteTexture(id);
}
void SetTextureFilter(FilteringMode mode) {
    ::Fast::Filter(mode);
}
FilteringMode GetTextureFilter() {
    return ::Fast::GetFilter();
}
ImTextureID GetTextureById(int id) {
    if (id < 0 || (size_t)id >= textures.size())
        Shipyard_Fatal("GUI texture index", E_INVALIDARG);
    return textures[id].native;
}
bool ReadPresentedFrameRGBA(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) {
    if (!xenos_device || buffers.empty() || live_framebuffer)
        return false;
    const Framebuffer& frame = buffers[0];
    if (!frame.stored || !frame.image || !frame.width || !frame.height)
        return false;
    width = frame.width;
    height = frame.height;
    std::vector<DWORD> pixels((size_t)width * height);
    rgba.resize((size_t)width * height * 4);
    D3DLOCKED_RECT lock;
    if (FAILED(frame.image->LockRect(0, &lock, NULL, D3DLOCK_READONLY)))
        return false;
    XGTEXTURE_DESC desc;
    XGGetTextureDesc(frame.image, 0, &desc);
    XGUntileTextureLevel(width, height, 0, XGGetGpuFormat(desc.Format), 0, &pixels[0], width * 4,
                         NULL, lock.pBits, NULL);
    if (FAILED(frame.image->UnlockRect(0)))
        return false;
    for (size_t i = 0; i < pixels.size(); ++i) {
        const DWORD p = pixels[i];
        rgba[i * 4] = (uint8_t)(p >> 16);
        rgba[i * 4 + 1] = (uint8_t)(p >> 8);
        rgba[i * 4 + 2] = (uint8_t)p;
        rgba[i * 4 + 3] = (uint8_t)(p >> 24);
    }
    return true;
}
} // namespace ShipyardXenos
} // namespace Fast
