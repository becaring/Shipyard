#include "platform.h"
#include "assets.h"
#include "perf_trace.h"
#include "gfx_cc.h"
#include "shader_cache.h"
#include "gfx_direct3d_common.h"
#include <xgraphics.h>
#include <vectorintrinsics.h>
#include <stddef.h>
#include <stdio.h>
#include <map>
#include <vector>
#include <algorithm>

IDirect3DDevice9* soh_device;
struct ShaderProgram {
    IDirect3DVertexShader9* vertex;
    IDirect3DPixelShader9* pixel;
    IDirect3DVertexDeclaration9* declaration;
    CCFeatures features;
    size_t floats;
};
namespace {
struct TextureSampler {
    bool linear;
    uint32_t s, t, key;
    TextureSampler(bool filter=false) : linear(filter), s(0), t(0), key(filter?128:0) {}
    void Set(bool filter,uint32_t u,uint32_t v) {
        linear=filter;s=u;t=v;key=(u&3)|((v&3)<<2)|(filter?128:0);
    }
};
struct Texture {
    IDirect3DTexture9* native;
    uint32_t width, height;
    float shader_width,shader_height;
    TextureSampler sampler;
    Texture() : native(NULL), width(0), height(0), shader_width(0), shader_height(0) {}
};
struct Framebuffer {
    IDirect3DSurface9* color;
    IDirect3DSurface9* depth;
    IDirect3DTexture9* image;
    IDirect3DTexture9* depth_image; // Non-owning alias of depth_samples[0].
    IDirect3DTexture9* color_samples[4];
    IDirect3DTexture9* depth_samples[4];
    std::vector<D3DRECT> tiles;
    uint32_t width, height, samples;
    float shader_width,shader_height;
    TextureSampler sampler;
    bool dirty, stored, depth_stored, render_target, read_depth;
    Framebuffer() : color(NULL), depth(NULL), image(NULL), depth_image(NULL), width(0), height(0), samples(1), shader_width(0), shader_height(0), sampler(true),
        dirty(false), stored(false), depth_stored(false), render_target(false), read_depth(false) {
        for (int i=0;i<4;++i) color_samples[i]=depth_samples[i]=NULL;
    }
};
std::map<std::pair<uint64_t, uint32_t>, ShaderProgram*> shaders;
std::vector<Texture> textures;
// SoH invalidates mutable HUD/text textures each presentation. Invalidation
// must observe changed pixels, but identical uploads need no new GPU resource.
// Keep exact source bytes for at most 32 small textures (512 KiB total). Slots
// own no D3D references; collisions only cause a normal upload.
enum { UPLOAD_SNAPSHOT_SLOTS=32, UPLOAD_SNAPSHOT_BYTES=16*1024 };
struct UploadSnapshot {
    uint32_t id,width,height;
    IDirect3DTexture9* native;
    std::vector<BYTE> rgba;
    UploadSnapshot():id(0),width(0),height(0),native(NULL) {}
};
UploadSnapshot upload_snapshots[UPLOAD_SNAPSHOT_SLOTS];
SoHTextureUploadCounts upload_counts;
struct PendingTextureRelease {
    IDirect3DTexture9* texture;
    size_t bytes;
    PendingTextureRelease(IDirect3DTexture9* value,size_t size):texture(value),bytes(size) {}
};
std::vector<PendingTextureRelease> pending_texture_releases;
size_t pending_texture_bytes;
std::vector<Framebuffer> buffers;
ShaderProgram* current_shader;
uint32_t selected[2];
int selected_fb[2] = {-1, -1};
int upload_tile, active_fb;
IDirect3DSurface9* presentation_color;
IDirect3DSurface9* presentation_depth;
IDirect3DVertexShader9* restore_vertex;
IDirect3DPixelShader9* restore_color;
IDirect3DPixelShader9* restore_depth;
IDirect3DVertexDeclaration9* restore_declaration;
IDirect3DStateBlock9* restore_state;
bool live_framebuffer, tile_constants_owned;
size_t tiling_draw_bytes;
void BeginFramebuffer();
void FinishFramebuffer();
void RebindSampledTextures();
void Resolve(int id);
unsigned frame_number;
float shader_frame_number;
float noise_scale = 1;
FilteringMode filtering = FILTER_THREE_POINT;
unsigned filtering_key=unsigned(FILTER_THREE_POINT)<<4;

// Only this thread owns these values. Cache the actual native sampler fields,
// not texture identity: a sampler edit through either alias must reach both
// bound slots. External/native drawing invalidates this state explicitly.
struct NativeStateCache {
    DWORD sampler[2][5];
    __declspec(align(16)) float constants[12];
    bool sampler_valid[2],constants_valid,alpha_valid;
    unsigned alpha_mode;
    unsigned sampler_key[2];
    IDirect3DVertexShader9* vertex;
    IDirect3DPixelShader9* pixel;
    IDirect3DVertexDeclaration9* declaration;
} native_state;
static_assert(__alignof(NativeStateCache)>=16 && offsetof(NativeStateCache,constants)%16==0,
              "native pixel constants must be 16-byte aligned");
SoHNativeStateCounts native_state_counts;
const D3DSAMPLERSTATETYPE sampler_fields[5]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV};

void InvalidateNativeState() {
    native_state.sampler_valid[0]=native_state.sampler_valid[1]=false;
    native_state.constants_valid=false;native_state.alpha_valid=false;
    native_state.vertex=NULL;native_state.pixel=NULL;native_state.declaration=NULL;
}
HRESULT SetNativeTexture(int tile,IDirect3DTexture9* texture) {
    // SetTexture changes the image, not the slot's sampler values. The SDK
    // merges both into its fetch constant. Keep full invalidation at external
    // drawing/state-block boundaries. Literal slots avoid XDK variable-mask work.
    if(tile==0)return soh_device->SetTexture(0,texture);
    return soh_device->SetTexture(1,texture);
}
__declspec(noinline) void VerifyNativeSampler(int tile,const DWORD* desired) {
    for(unsigned field=0;field<5;++field) {
        DWORD actual;
        SoH_Check(soh_device->GetSamplerState(tile,sampler_fields[field],&actual),"verify native sampler");
        if(actual!=desired[field])SoH_Fatal("reused native sampler differs from requested state",E_FAIL);
    }
    ++native_state_counts.sampler_verified;
}
__declspec(noinline) void VerifyNativeConstants(const float* constants) {
    float actual[12];
    SoH_Check(soh_device->GetPixelShaderConstantF(0,actual,3),"verify native pixel constants");
    if(memcmp(actual,constants,sizeof(actual)))SoH_Fatal("reused native pixel constants differ from requested values",E_FAIL);
    ++native_state_counts.constants_verified;
}
__forceinline bool SameNativeConstants(const float* constants) {
    // Draw's sole source and the cache member are explicitly 16-byte aligned.
    // Compare all 48 bytes as integer bits, preserving signed zero and NaNs.
    const __vector4 d0=__vxor(__lvx(native_state.constants,0),__lvx(constants,0));
    const __vector4 d1=__vxor(__lvx(native_state.constants,16),__lvx(constants,16));
    const __vector4 d2=__vxor(__lvx(native_state.constants,32),__lvx(constants,32));
    unsigned condition;
    __vcmpequwR(__vor(__vor(d0,d1),d2),__vspltisw(0),&condition);
    return (condition&0x80)!=0; // XDK XM_CRMASK_CR6TRUE: all lanes equal.
}
void SetNativeConstants(const float* constants) {
    ++native_state_counts.constants_attempted;
    if(native_state.constants_valid && SameNativeConstants(constants)) {
        if(SoH_VerifyFast3DReuse())VerifyNativeConstants(constants);
        ++native_state_counts.constants_suppressed;
        return;
    }
    // One small upload is cheaper than scalar range scans and split SDK calls.
    // Retain the unchanged-block fast path across compatible shader switches.
    soh_device->SetPixelShaderConstantF(0,constants,3);
    __stvx(__lvx(constants,0),native_state.constants,0);
    __stvx(__lvx(constants,16),native_state.constants,16);
    __stvx(__lvx(constants,32),native_state.constants,32);
    native_state.constants_valid=true;
    ++native_state_counts.constants_written;
}

template<int tile> void ApplySamplerSlot(const TextureSampler& sampler) {
    const unsigned key=sampler.key|filtering_key;
    if(native_state.sampler_valid[tile] && native_state.sampler_key[tile]==key) {
        native_state_counts.sampler_attempted+=5;native_state_counts.sampler_suppressed+=5;
        if(SoH_VerifyFast3DReuse())VerifyNativeSampler(tile,native_state.sampler[tile]);
        return;
    }
    native_state.sampler_key[tile]=key;
    const D3DTEXTUREFILTERTYPE filter=filtering==FILTER_LINEAR && sampler.linear?D3DTEXF_LINEAR:D3DTEXF_POINT;
    const DWORD address_u=(sampler.s&2)?D3DTADDRESS_CLAMP:(sampler.s&1)?D3DTADDRESS_MIRROR:D3DTADDRESS_WRAP;
    const DWORD address_v=(sampler.t&2)?D3DTADDRESS_CLAMP:(sampler.t&1)?D3DTADDRESS_MIRROR:D3DTADDRESS_WRAP;
    DWORD* cached=native_state.sampler[tile];
    native_state_counts.sampler_attempted+=5;
    if(native_state.sampler_valid[tile] && cached[0]==filter && cached[1]==filter &&
       cached[2]==D3DTEXF_NONE && cached[3]==address_u && cached[4]==address_v) {
        if(SoH_VerifyFast3DReuse())VerifyNativeSampler(tile,cached);
        native_state_counts.sampler_suppressed+=5;
        return;
    }
    // A real change must not rewrite the other four independent fields.
#define SOH_SAMPLER_FIELD(index,field,value) \
    if(!native_state.sampler_valid[tile] || cached[index]!=(DWORD)(value)) { \
        soh_device->SetSamplerState(tile,field,value);cached[index]=(DWORD)(value); \
        ++native_state_counts.sampler_written; \
    } else ++native_state_counts.sampler_suppressed
    SOH_SAMPLER_FIELD(0,D3DSAMP_MINFILTER,filter);
    SOH_SAMPLER_FIELD(1,D3DSAMP_MAGFILTER,filter);
    SOH_SAMPLER_FIELD(2,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    SOH_SAMPLER_FIELD(3,D3DSAMP_ADDRESSU,address_u);
    SOH_SAMPLER_FIELD(4,D3DSAMP_ADDRESSV,address_v);
#undef SOH_SAMPLER_FIELD
    native_state.sampler_valid[tile]=true;
}

void ApplySampler(int tile,const TextureSampler& sampler) {
    if(tile==0)ApplySamplerSlot<0>(sampler);else ApplySamplerSlot<1>(sampler);
}

void Replace(std::string& text, const std::string& from, const std::string& to) {
    size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
}

// Retain SoH's generated combiner equations; lower only D3D11 shader syntax to Xenos SM3.
std::string XboxShader(const CCFeatures& features, size_t& floats) {
    char source[32768];
    size_t length;
    gfx_direct3d_common_build_shader(source, length, floats, features, false, true);
    std::string text(source, length);
    Replace(text, "SV_POSITION", "POSITION");
    Replace(text, "SV_TARGET", "COLOR0");
    // XDK's compiler exposes pixel rejection through clip(), not HLSL discard.
    // Leave the original alpha-test conditions and thresholds intact.
    Replace(text, "discard;", "clip(-1.0);");
    Replace(text, "float4 screenSpace : SV_Position", "float2 screenSpace : VPOS");
    Replace(text, "cbuffer PerFrameCB : register(b0) {\r\n    uint noise_frame;\r\n    float noise_scale;\r\n}",
                  "float4 frameInfo : register(c0);\n#define noise_frame frameInfo.x\n#define noise_scale frameInfo.y\n");
    Replace(text, "cbuffer PerDrawCB : register(b1) {\r\n    struct {\r\n        uint width;\r\n        uint height;\r\n        bool linear_filtering;\r\n    } textures[2];\r\n}",
                  "float4 texInfo[2] : register(c1);");
    for (int i = 0; i < 2; ++i) {
        char from[200], to[200];
        sprintf(from, "Texture2D g_texture%d : register(t%d);\r\nSamplerState g_sampler%d : register(s%d);", i, i, i, i);
        sprintf(to, "sampler2D g_texture%d : register(s%d);", i, i);
        Replace(text, from, to);
        const char* member[] = {"width", "height", "linear_filtering"};
        const char* component[] = {"x", "y", "z"};
        for (int j = 0; j < 3; ++j) {
            sprintf(from, "textures[%d].%s", i, member[j]); sprintf(to, "texInfo[%d].%s", i, component[j]); Replace(text, from, to);
        }
        sprintf(from, "g_texture%d.Sample(g_sampler%d, ", i, i); sprintf(to, "tex2D(g_texture%d, ", i); Replace(text, from, to);
        sprintf(from, "g_texture%d, g_sampler%d, ", i, i); sprintf(to, "g_texture%d, ", i); Replace(text, from, to);
        sprintf(from, "int2 texSize%d;", i); sprintf(to, "float2 texSize%d = texInfo[%d].xy;", i, i); Replace(text, from, to);
        sprintf(from, "g_texture%d.GetDimensions(texSize%d.x, texSize%d.y);", i, i, i); Replace(text, from, "");
        for (int j = 0; j < 2; ++j) {
            sprintf(from, ": TEXCLAMP%s%d", j ? "T" : "S", i); sprintf(to, ": TEXCOORD%d", 11 + i * 2 + j); Replace(text, from, to);
        }
    }
    Replace(text, "tex.Sample(tSampler, ", "tex2D(tex, ");
    Replace(text, "in Texture2D tex, in SamplerState tSampler,", "in sampler2D tex,");
    Replace(text, ": FOG", ": TEXCOORD9");
    Replace(text, ": GRAYSCALE", ": TEXCOORD10");
    for (int i = 0; i < 7; ++i) {
        char from[32], to[32]; sprintf(from, ": INPUT%d", i); sprintf(to, ": TEXCOORD%d", i + 2); Replace(text, from, to);
    }
    Replace(text, "screenSpace.xy * noise_scale", "(screenSpace.xy + framebufferTile.xy) * noise_scale");
    return "float4 framebufferTile : register(c4);\n" + text;
}


GfxClipParameters ClipParameters() { GfxClipParameters p = {true, false}; return p; }
void UnloadShader(ShaderProgram*) { current_shader = NULL; }
void LoadShader(ShaderProgram* shader) {
    // Application c0-c2 are explicit in every generated program. Shader binds
    // preserve those registers; external rendering invalidates the whole cache.
    current_shader = shader;
    if(native_state.declaration!=shader->declaration) {
        SoH_Check(soh_device->SetVertexDeclaration(shader->declaration), "vertex declaration");
        native_state.declaration=shader->declaration;
    }
    if(native_state.vertex!=shader->vertex) {
        SoH_Check(soh_device->SetVertexShader(shader->vertex), "vertex shader");native_state.vertex=shader->vertex;
    }
    if(native_state.pixel!=shader->pixel) {
        SoH_Check(soh_device->SetPixelShader(shader->pixel), "pixel shader");native_state.pixel=shader->pixel;
    }
}
ID3DXBuffer* Compile(const std::string& source, const char* entry, const char* profile) {
    ID3DXBuffer *code = SoH_LoadShaderCode(source,entry,profile), *errors = NULL;
    if(code)return code;
    HRESULT hr = D3DXCompileShader(source.c_str(), (UINT)source.size(), NULL, NULL, entry, profile, 0, &code, &errors, NULL);
    if (FAILED(hr)) {
        SoH_Log("shader %s: %s", entry, errors ? (char*)errors->GetBufferPointer() : "unknown");
        SoH_WriteFile("failed-shader.hlsl", source.data(), source.size());
    }
    if (errors) errors->Release();
    SoH_Check(hr, "compile SoH combiner");
    SoH_StoreShaderCode(source,entry,profile,code);
    return code;
}
void Element(std::vector<D3DVERTEXELEMENT9>& elements, WORD& offset, int count, BYTE usage, BYTE index) {
    D3DVERTEXELEMENT9 e = {0, offset, (BYTE)(D3DDECLTYPE_FLOAT1 + count - 1), D3DDECLMETHOD_DEFAULT, usage, index};
    // Xenos declaration types are encoded tokens, not consecutive PC D3D9 enums.
    const DWORD types[] = {D3DDECLTYPE_FLOAT1, D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4};
    e.Type = types[count - 1];
    elements.push_back(e); offset += count * sizeof(float);
}
// Non-owning indices: each ShaderProgram owns one reference, including shared
// exact binaries/declarations. Shutdown releases those references then indices.
std::map<std::string,IDirect3DVertexShader9*> vertex_programs;
std::map<std::string,IDirect3DVertexDeclaration9*> vertex_declarations;
ShaderProgram* CreateShader(uint64_t id0, uint32_t id1) {
    SoHPerfScope measure(SOH_PERF_SHADER);
    ShaderProgram* shader = new ShaderProgram();
    gfx_cc_get_features(id0, id1, &shader->features);
    std::string source = XboxShader(shader->features, shader->floats);
    ID3DXBuffer* code = Compile(source, "VSMain", "vs_3_0");
    const std::string vertex_key((const char*)code->GetBufferPointer(),code->GetBufferSize());
    std::map<std::string,IDirect3DVertexShader9*>::iterator existing_vertex=vertex_programs.find(vertex_key);
    if(existing_vertex!=vertex_programs.end()) {shader->vertex=existing_vertex->second;shader->vertex->AddRef();}
    else {
        SoH_Check(soh_device->CreateVertexShader((DWORD*)code->GetBufferPointer(), &shader->vertex), "CreateVertexShader");
        vertex_programs.insert(std::make_pair(vertex_key,shader->vertex));
    }
    code->Release();
    code = Compile(source, "PSMain", "ps_3_0");
    SoH_Check(soh_device->CreatePixelShader((DWORD*)code->GetBufferPointer(), &shader->pixel), "CreatePixelShader"); code->Release();
    std::vector<D3DVERTEXELEMENT9> elements;
    WORD offset = 0;
    Element(elements, offset, 4, D3DDECLUSAGE_POSITION, 0);
    for (int i = 0; i < 2; ++i) if (shader->features.used_textures[i]) {
        Element(elements, offset, 2, D3DDECLUSAGE_TEXCOORD, i);
        for (int j = 0; j < 2; ++j) if (shader->features.clamp[i][j]) Element(elements, offset, 1, D3DDECLUSAGE_TEXCOORD, 11+i*2+j);
    }
    if (shader->features.opt_fog) Element(elements, offset, 4, D3DDECLUSAGE_TEXCOORD, 9);
    if (shader->features.opt_grayscale) Element(elements, offset, 4, D3DDECLUSAGE_TEXCOORD, 10);
    for (int i = 0; i < shader->features.num_inputs; ++i) Element(elements, offset, shader->features.opt_alpha ? 4 : 3, D3DDECLUSAGE_TEXCOORD, i+2);
    D3DVERTEXELEMENT9 end = D3DDECL_END(); elements.push_back(end);
    const std::string declaration_key((const char*)&elements[0],elements.size()*sizeof(elements[0]));
    std::map<std::string,IDirect3DVertexDeclaration9*>::iterator existing_decl=vertex_declarations.find(declaration_key);
    if(existing_decl!=vertex_declarations.end()) {shader->declaration=existing_decl->second;shader->declaration->AddRef();}
    else {
        SoH_Check(soh_device->CreateVertexDeclaration(&elements[0], &shader->declaration), "CreateVertexDeclaration");
        vertex_declarations.insert(std::make_pair(declaration_key,shader->declaration));
    }
    if (offset != shader->floats * sizeof(float)) SoH_Fatal("SoH vertex stride", E_FAIL);
    shaders[std::make_pair(id0,id1)] = shader; LoadShader(shader);
    return shader;
}
ShaderProgram* Lookup(uint64_t id0, uint32_t id1) {
    std::map<std::pair<uint64_t,uint32_t>, ShaderProgram*>::iterator it = shaders.find(std::make_pair(id0,id1));
    return it == shaders.end() ? NULL : it->second;
}
void ShaderInfo(ShaderProgram* shader, uint8_t* inputs, bool used[2]) { *inputs = (uint8_t)shader->features.num_inputs; used[0] = shader->features.used_textures[0]; used[1] = shader->features.used_textures[1]; }
uint32_t NewTexture() { textures.push_back(Texture()); return (uint32_t)textures.size()-1; }
void CollectTextures(bool wait) {
    for(size_t i=0;i<pending_texture_releases.size();) {
        PendingTextureRelease& pending=pending_texture_releases[i];
        if(wait || !pending.texture->IsBusy()) {
            pending.texture->Release();pending_texture_bytes-=pending.bytes;
            pending_texture_releases[i]=pending_texture_releases.back();pending_texture_releases.pop_back();
        } else ++i;
    }
}
void ReleaseTexture(IDirect3DTexture9* texture) {
    // XDK Release waits for the GPU when dropping the last reference. Keep the
    // replaced immutable image alive until the SDK reports it unused, so a
    // glyph/item upload doesn't drain the GPU halfway through a game frame.
    // Keep this reference through EndTiling as well: recorded tile draws have
    // not all reached the GPU yet. Collection occurs at the next frame's start.
    UINT base=0,mips=0;
    XGGetTextureLayout(texture,NULL,&base,NULL,NULL,0,NULL,&mips,NULL,NULL,0);
    size_t bytes=XGNextMultiple(base,4096)+XGNextMultiple(mips,4096);
    const size_t budget=8*1024*1024;
    if(bytes>budget || pending_texture_releases.size()>=128 || pending_texture_bytes+bytes>budget) {
        FinishFramebuffer();CollectTextures(true);
        if(bytes>budget) {texture->Release();return;}
    }
    pending_texture_releases.push_back(PendingTextureRelease(texture,bytes));pending_texture_bytes+=bytes;
}
void SelectTexture(int tile, uint32_t id) {
    if (tile < 0 || tile > 1 || id >= textures.size()) SoH_Fatal("texture index", E_INVALIDARG);
    upload_tile=tile;selected[tile]=id;selected_fb[tile]=-1;
    SoH_Check(SetNativeTexture(tile,textures[id].native),"SetTexture");
    // Fast3D caches sampler settings per texture and does not repeat Sampler()
    // on a cache hit. Restore them whenever that texture is selected again.
    ApplySampler(tile,textures[id].sampler);
}
void VerifyTextureUpload(Texture& tex,const uint8_t* bytes) {
    // Readback is restricted to the existing opt-in, pre-measurement game
    // verification. Complete tiling before a CPU lock; never wait mid-bracket.
    const bool resume=live_framebuffer;
    FinishFramebuffer();soh_device->BlockUntilIdle();
    D3DLOCKED_RECT lock;
    SoH_Check(tex.native->LockRect(0,&lock,NULL,D3DLOCK_READONLY),"verify retained texture LockRect");
    for(uint32_t y=0;y<tex.height;++y) {
        const DWORD* row=(const DWORD*)((const BYTE*)lock.pBits+y*lock.Pitch);
        for(uint32_t x=0;x<tex.width;++x) {
            const BYTE* p=bytes+(y*tex.width+x)*4;
            const DWORD expected=((DWORD)p[3]<<24)|((DWORD)p[0]<<16)|((DWORD)p[1]<<8)|p[2];
            if(row[x]!=expected)SoH_Fatal("retained texture differs from requested pixels",E_FAIL);
        }
    }
    SoH_Check(tex.native->UnlockRect(0),"verify retained texture UnlockRect");
    ++upload_counts.verified;
    if(resume)BeginFramebuffer();
}
void Upload(const uint8_t* bytes, uint32_t width, uint32_t height) {
    SoHPerfScope measure(SOH_PERF_UPLOAD);
    const uint32_t id=selected[upload_tile];
    Texture& tex = textures[id];
    if (!width || !height || width > 4096 || height > 4096) SoH_Fatal("texture dimensions", E_INVALIDARG);
    ++upload_counts.requests;
    const size_t size=width*height*4;
    UploadSnapshot& snapshot=upload_snapshots[id&(UPLOAD_SNAPSHOT_SLOTS-1)];
    if(size<=UPLOAD_SNAPSHOT_BYTES && tex.native && snapshot.native==tex.native && snapshot.id==id &&
       snapshot.width==width && snapshot.height==height && snapshot.rgba.size()==size &&
       memcmp(&snapshot.rgba[0],bytes,size)==0) {
        if(SoH_VerifyFast3DReuse())VerifyTextureUpload(tex,bytes);
        ++upload_counts.reused;upload_counts.bytes_saved+=size;
        SelectTexture(upload_tile,id);RebindSampledTextures();return;
    }
    snapshot.native=NULL;
    if (tex.native) {
        for (int i=0;i<2;++i) if (selected_fb[i]<0 && selected[i]==selected[upload_tile]) SetNativeTexture(i,NULL);
        ReleaseTexture(tex.native);
    }
    SoH_Check(soh_device->CreateTexture(width, height, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, &tex.native, NULL), "CreateTexture");
    D3DLOCKED_RECT lock;
    SoH_Check(tex.native->LockRect(0, &lock, NULL, 0), "texture LockRect");
    for (uint32_t y=0; y<height; ++y) {
        DWORD* out = (DWORD*)((BYTE*)lock.pBits + y*lock.Pitch);
        for (uint32_t x=0; x<width; ++x) { const BYTE* p = bytes+(y*width+x)*4; out[x] = ((DWORD)p[3]<<24)|((DWORD)p[0]<<16)|((DWORD)p[1]<<8)|p[2]; }
    }
    tex.native->UnlockRect(0); tex.width=width; tex.height=height;
    tex.shader_width=(float)width;tex.shader_height=(float)height;
    if(size<=UPLOAD_SNAPSHOT_BYTES) {
        if(snapshot.rgba.capacity()<UPLOAD_SNAPSHOT_BYTES)snapshot.rgba.reserve(UPLOAD_SNAPSHOT_BYTES);
        snapshot.rgba.resize(size);memcpy(&snapshot.rgba[0],bytes,size);
        snapshot.id=id;snapshot.width=width;snapshot.height=height;snapshot.native=tex.native;
    }
    SelectTexture(upload_tile, selected[upload_tile]);
    RebindSampledTextures();
}
void Sampler(int tile, bool linear, uint32_t s, uint32_t t) {
    TextureSampler& sampler=selected_fb[tile]>=0?buffers[selected_fb[tile]].sampler:textures[selected[tile]].sampler;
    sampler.Set(linear,s,t);
    ApplySampler(tile,sampler);
}
void Depth(bool test,bool write) { soh_device->SetRenderState(D3DRS_ZENABLE,test); soh_device->SetRenderState(D3DRS_ZWRITEENABLE,write); soh_device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL); }
void Decal(bool enable) {
    // Match SoH's D3D11 rasterizer: offset coplanar decals by their depth
    // slope. A fixed epsilon loses paths on steep or distant floor polygons.
    float slope=enable?-2.0f:0.0f;DWORD bits;memcpy(&bits,&slope,sizeof(bits));
    soh_device->SetRenderState(D3DRS_DEPTHBIAS,0);
    soh_device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,bits);
}
void Viewport(int x,int y,int w,int h) {
    // XDK clamps viewport dimensions against the bound surface. Complete the
    // pending target switch (including BeginTiling) before applying Fast3D's
    // viewport, especially after Link's small pause-preview framebuffer.
    BeginFramebuffer();
    D3DVIEWPORT9 v={(DWORD)x,buffers[active_fb].height-y-h,(DWORD)w,(DWORD)h,0,1};
    SoH_Check(soh_device->SetViewport(&v),"viewport");
}
void Scissor(int x,int y,int w,int h) {
    BeginFramebuffer();
    LONG height=(LONG)buffers[active_fb].height; RECT r={x,height-y-h,x+w,height-y};
    soh_device->SetScissorRect(&r); soh_device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
}
bool requested_alpha;
unsigned NativeAlphaMode(bool enable,const CCFeatures* features) {
    if(!enable)return 0;
    // Keep alpha in the combiner for rejection; opaque survivors do not blend.
    // Later alpha modifiers retain the original blending path.
    return features && features->opt_alpha && features->opt_texture_edge &&
        !features->opt_noise && !features->opt_invisible ? 2 : 1;
}
void ApplyAlpha() {
    const unsigned mode=NativeAlphaMode(requested_alpha,current_shader?&current_shader->features:NULL);
    if(native_state.alpha_valid && native_state.alpha_mode==mode)return;
    soh_device->SetRenderState(D3DRS_ALPHABLENDENABLE,mode==1);
    soh_device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    soh_device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    soh_device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    // Cutout writes RGB only: preserve destination alpha exactly as the old
    // ZERO/ONE separate-alpha blend, including nonopaque offscreen targets.
    soh_device->SetRenderState(D3DRS_COLORWRITEENABLE,mode==2?7:15);
    soh_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);
    soh_device->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ZERO);
    soh_device->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_ONE);
    soh_device->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
    native_state.alpha_mode=mode;native_state.alpha_valid=true;
}
void Alpha(bool enable) {requested_alpha=enable;ApplyAlpha();}

void Draw(float* vertices,size_t count,size_t triangles) {
    SoHPerfScope measure(SOH_PERF_DRAW);
    if (!current_shader || count != triangles*3*current_shader->floats) SoH_Fatal("SoH vertex buffer size",E_INVALIDARG);
    // EndTiling cannot submit a partially filled SDK ring. Retain the target and
    // restart the native bracket before it consumes its reserved command space.
    // The allowance includes generous state-command space for each SoH batch.
    if (live_framebuffer && buffers[active_fb].tiles.size()>1 && tiling_draw_bytes+count*sizeof(float)+8192>2*1024*1024)
        FinishFramebuffer();
    BeginFramebuffer();
    __declspec(align(16)) float constants[12] = {shader_frame_number,noise_scale,0,0};
    for (int i=0; i<2; ++i) if(current_shader->features.used_textures[i]) {
        const TextureSampler* sampler=NULL;
        if(selected_fb[i]>=0) {const Framebuffer& b=buffers[selected_fb[i]];constants[4+i*4]=b.shader_width;constants[5+i*4]=b.shader_height;sampler=&b.sampler;}
        else if(selected[i]<textures.size()) {const Texture& t=textures[selected[i]];constants[4+i*4]=t.shader_width;constants[5+i*4]=t.shader_height;sampler=&t.sampler;}
        if(sampler) {
            // The same cached texture may occupy both slots. A sampler update
            // through either slot applies to that texture, as in D3D11/GL.
            ApplySampler(i,*sampler);
            constants[6+i*4]=filtering==FILTER_THREE_POINT && sampler->linear?1.0f:0.0f;
        }
    }
    SetNativeConstants(constants);
    ApplyAlpha(); // Also handles shader-only changes and external state restoration.
    SoH_Check(soh_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,(UINT)triangles,vertices,(UINT)(current_shader->floats*4)),"SoH DrawPrimitiveUP");
    tiling_draw_bytes+=count*sizeof(float)+8192;
    buffers[active_fb].dirty=true;
}
void ValidateFramebuffer(int id) {
    if (id<0 || (size_t)id>=buffers.size()) SoH_Fatal("framebuffer index",E_INVALIDARG);
}
void InitializeRestore() {
    const std::string source=
        "struct V {float2 p:POSITION;float2 uv:TEXCOORD0;};\n"
        "struct O {float4 p:POSITION;float2 uv:TEXCOORD0;};\n"
        "O VSMain(V v){O o;o.p=float4(v.p,0,1);o.uv=v.uv;return o;}\n"
        "sampler2D colorImage:register(s0);sampler2D depthImage:register(s1);\n"
        "float4 ColorMain(O v):COLOR0{return tex2D(colorImage,v.uv);}\n"
        "struct D {float4 color:COLOR0;float depth:DEPTH;};\n"
        "D DepthMain(O v){D o;o.color=tex2D(colorImage,v.uv);o.depth=tex2D(depthImage,v.uv).r;return o;}\n";
    ID3DXBuffer* code=Compile(source,"VSMain","vs_3_0");
    SoH_Check(soh_device->CreateVertexShader((DWORD*)code->GetBufferPointer(),&restore_vertex),"framebuffer restore VS");code->Release();
    code=Compile(source,"ColorMain","ps_3_0");
    SoH_Check(soh_device->CreatePixelShader((DWORD*)code->GetBufferPointer(),&restore_color),"framebuffer copy PS");code->Release();
    code=Compile(source,"DepthMain","ps_3_0");
    SoH_Check(soh_device->CreatePixelShader((DWORD*)code->GetBufferPointer(),&restore_depth),"framebuffer depth restore PS");code->Release();
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    SoH_Check(soh_device->CreateVertexDeclaration(elements,&restore_declaration),"framebuffer restore declaration");
    SoH_Check(soh_device->CreateStateBlock(D3DSBT_ALL,&restore_state),"framebuffer restore state");
}
void StopTileConstants() {
    if (tile_constants_owned) {soh_device->GpuDisownPixelShaderConstantF(4,4);tile_constants_owned=false;}
}
void SetTileConstants() {
    // Tile-owned c4..c7 are disjoint from the cached CPU draw block c0..c2.
    const Framebuffer& b=buffers[active_fb];
    if (b.tiles.size()>1) {
        soh_device->GpuOwnPixelShaderConstantF(4,4);tile_constants_owned=true;
        for (size_t tile=0;tile<b.tiles.size();++tile) {
            soh_device->SetPredication(D3DPRED_TILE(tile));
            D3DVECTOR4* values=NULL;
            SoH_Check(soh_device->GpuBeginPixelShaderConstantF4(4,&values,4),"tile pixel constants");
            ZeroMemory(values,4*sizeof(D3DVECTOR4));
            values[0].x=(float)b.tiles[tile].x1;values[0].y=(float)b.tiles[tile].y1;
            soh_device->GpuEndPixelShaderConstantF4();
        }
        soh_device->SetPredication(0);
    } else {float zero[4]={0};soh_device->SetPixelShaderConstantF(4,zero,1);}
}
void RestoreQuad(IDirect3DTexture9* color,IDirect3DTexture9* depth,uint32_t width,uint32_t height,DWORD sample_mask) {
    InvalidateNativeState();
    bool restore_constants=tile_constants_owned;
    StopTileConstants();
    SoH_Check(restore_state->Capture(),"capture framebuffer copy state");
    D3DVIEWPORT9 viewport={0,0,width,height,0,1};soh_device->SetViewport(&viewport);
    soh_device->SetRenderState(D3DRS_VIEWPORTENABLE,TRUE);
    soh_device->SetRenderState(D3DRS_HALFPIXELOFFSET,TRUE);
    soh_device->SetVertexShader(restore_vertex);soh_device->SetPixelShader(depth?restore_depth:restore_color);
    soh_device->SetVertexDeclaration(restore_declaration);
    soh_device->SetRenderState(D3DRS_ZENABLE,depth!=NULL);
    soh_device->SetRenderState(D3DRS_ZWRITEENABLE,depth!=NULL);
    soh_device->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);
    soh_device->SetRenderState(D3DRS_HIZENABLE,FALSE);soh_device->SetRenderState(D3DRS_HIZWRITEENABLE,FALSE);
    soh_device->SetRenderState(D3DRS_HISTENCILENABLE,FALSE);soh_device->SetRenderState(D3DRS_HISTENCILWRITEENABLE,FALSE);
    soh_device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    soh_device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);soh_device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    soh_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    soh_device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);soh_device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    soh_device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);soh_device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    soh_device->SetRenderState(D3DRS_DEPTHBIAS,0);soh_device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
    soh_device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE);soh_device->SetRenderState(D3DRS_MULTISAMPLEMASK,sample_mask);
    SetNativeTexture(0,color);SetNativeTexture(1,depth);
    for (int sampler=0;sampler<2;++sampler) {
        soh_device->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        soh_device->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        soh_device->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        soh_device->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        soh_device->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    }
    // Xenos HALFPIXELOFFSET already aligns pixel and texel centers. Adding a
    // desktop D3D9 half-pixel translation again shifts retained color/depth on
    // every restore (notably when a newly drawn glyph replaces a cached texture).
    // Match XDK ATG::PostProcess::DrawFullScreenQuad's unshifted clip-space quad.
    float left=-1.0f,right=1.0f,top=1.0f,bottom=-1.0f;
    const float quad[]={left,top,0,0,right,top,1,0,left,bottom,0,1,
                        left,bottom,0,1,right,top,1,0,right,bottom,1,1};
    SoH_Check(soh_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,quad,4*sizeof(float)),"restore framebuffer samples");
    SoH_Check(restore_state->Apply(),"restore framebuffer copy state");
    InvalidateNativeState();
    if (restore_constants) SetTileConstants();
}
void ClearFramebufferTargets(DWORD flags) {
    const Framebuffer& b=buffers[active_fb];
    D3DVIEWPORT9 previous;DWORD scissor;
    soh_device->GetViewport(&previous);
    soh_device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);
    // XDK Clear clips even explicit rectangles to the current viewport. SoH's
    // clear_framebuffer contract clears the complete target, like D3D11/GL.
    D3DVIEWPORT9 full={0,0,b.width,b.height,0,1};
    SoH_Check(soh_device->SetViewport(&full),"full framebuffer clear viewport");
    soh_device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    SoH_Check(soh_device->Clear(0,NULL,flags,0xff000000,1,0),"clear framebuffer");
    SoH_Check(soh_device->SetViewport(&previous),"restore framebuffer clear viewport");
    soh_device->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);
}
void BeginFramebuffer() {
    if (live_framebuffer) return;
    InvalidateNativeState();
    SoHPerfScope measure(SOH_PERF_RESTORE);
    Framebuffer& b=buffers[active_fb];
    if (!b.color) SoH_Fatal("framebuffer render surface unavailable",E_INVALIDARG);
    D3DVIEWPORT9 viewport;soh_device->GetViewport(&viewport);
    SoH_Check(soh_device->SetRenderTarget(0,b.color),"bind framebuffer color");
    SoH_Check(soh_device->SetDepthStencilSurface(b.depth),"bind framebuffer depth");
    soh_device->SetViewport(&viewport);
    soh_device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE);
    soh_device->SetRenderState(D3DRS_MULTISAMPLEMASK,~0u);
    D3DVECTOR4 clear={0,0,0,1};
    if (b.tiles.size()>1) soh_device->BeginTiling(0,(DWORD)b.tiles.size(),&b.tiles[0],&clear,1,0);
    else if (!b.stored || (b.depth && !b.depth_stored)) {
        DWORD flags=(!b.stored?D3DCLEAR_TARGET:0) | (b.depth?D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL:0);
        ClearFramebufferTargets(flags);
    }
    // Each MSAA fragment is retained independently. Resolving only the averaged
    // image here would destroy edge coverage and subsequent depth tests.
    if (b.stored) for (uint32_t sample=0;sample<b.samples;++sample) {
        RestoreQuad(b.samples>1?b.color_samples[sample]:b.image,
            b.depth_stored?b.depth_samples[sample]:NULL,b.width,b.height,b.samples>1?1u<<sample:~0u);
    }
    live_framebuffer=true;
    tiling_draw_bytes=0;
    SetTileConstants();
}
void RebindSampledTextures() {
    for (int i=0;i<2;++i) {
        IDirect3DTexture9* texture=NULL;
        if (selected_fb[i]>=0) {texture=buffers[selected_fb[i]].image;ApplySampler(i,buffers[selected_fb[i]].sampler);}
        else if (selected[i]<textures.size()) {texture=textures[selected[i]].native;ApplySampler(i,textures[selected[i]].sampler);}
        SetNativeTexture(i,texture);
    }
}
void FinishFramebuffer() {
    if (!live_framebuffer) return;
    SoHPerfScope measure(SOH_PERF_STORE);
    Framebuffer& b=buffers[active_fb];
    StopTileConstants();
    SetNativeTexture(0,NULL);SetNativeTexture(1,NULL);
    const DWORD fragments[]={D3DRESOLVE_FRAGMENT0,D3DRESOLVE_FRAGMENT1,D3DRESOLVE_FRAGMENT2,D3DRESOLVE_FRAGMENT3};
    D3DVECTOR4 clear={0,0,0,1};
    for (size_t tile=0;tile<b.tiles.size();++tile) {
        if (b.tiles.size()>1) soh_device->SetPredication(D3DPRED_TILE(tile));
        D3DRECT rectangle=b.tiles[tile];
        rectangle.x2=std::min(rectangle.x2,(LONG)b.width);rectangle.y2=std::min(rectangle.y2,(LONG)b.height);
        D3DPOINT destination={rectangle.x1,rectangle.y1};
        for (uint32_t sample=0;sample<b.samples;++sample) {
            if (b.depth) SoH_Check(soh_device->Resolve(D3DRESOLVE_DEPTHSTENCIL|fragments[sample],&rectangle,
                b.depth_samples[sample],&destination,0,0,NULL,1,0,NULL),"retain depth fragment");
            if (b.samples>1) SoH_Check(soh_device->Resolve(D3DRESOLVE_RENDERTARGET0|fragments[sample],&rectangle,
                b.color_samples[sample],&destination,0,0,NULL,1,0,NULL),"retain color fragment");
        }
        DWORD flags=D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS;
        if (b.tiles.size()>1) flags|=D3DRESOLVE_CLEARRENDERTARGET|(b.depth?D3DRESOLVE_CLEARDEPTHSTENCIL:0);
        SoH_Check(soh_device->Resolve(flags,&rectangle,b.image,&destination,0,0,&clear,1,0,NULL),"resolve framebuffer color");
    }
    if (b.tiles.size()>1) {
        soh_device->SetPredication(0);
        // Resolves above are explicitly predicated per tile, including all depth
        // fragments. Disable EndTiling's automatic single-target resolve.
        SoH_Check(soh_device->EndTiling(0,NULL,NULL,&clear,1,0,NULL),"finish framebuffer tiling");
    }
    b.stored=true;b.depth_stored=b.depth!=NULL;b.dirty=false;live_framebuffer=false;
    InvalidateNativeState();
    RebindSampledTextures();
}
void Resolve(int id) {
    ValidateFramebuffer(id);
    if (live_framebuffer && active_fb==id) {FinishFramebuffer();BeginFramebuffer();}
}
int NewFramebuffer() { buffers.push_back(Framebuffer()); return (int)buffers.size()-1; }
void ReleaseFramebuffer(Framebuffer& b) {
    if(b.color)b.color->Release();if(b.depth)b.depth->Release();if(b.image)b.image->Release();
    for(int i=0;i<4;++i){if(b.color_samples[i])b.color_samples[i]->Release();if(b.depth_samples[i])b.depth_samples[i]->Release();}
}
void UpdateFramebuffer(int id,uint32_t width,uint32_t height,uint32_t requested_samples,bool,bool target,bool depth,bool read_depth) {
    ValidateFramebuffer(id);width=std::max(width,1u);height=std::max(height,1u);
    uint32_t samples=requested_samples>=4?4:requested_samples>=2?2:1;
    if (width>4096 || height>4096) SoH_Fatal("framebuffer texture dimensions",E_INVALIDARG);
    Framebuffer& b=buffers[id];
    if (b.width==width && b.height==height && b.samples==samples && (b.depth!=NULL)==depth) {
        b.render_target=target;b.read_depth=read_depth;b.depth_image=read_depth?b.depth_samples[0]:NULL;return;
    }
    bool resume=live_framebuffer;FinishFramebuffer();
    InvalidateNativeState();
    SetNativeTexture(0,NULL);SetNativeTexture(1,NULL);
    soh_device->SetRenderTarget(0,NULL);soh_device->SetDepthStencilSurface(NULL);
    soh_device->BlockUntilIdle();
    // A state block may retain the last restore inputs. Drop those references
    // before resizing, so old and new full-size sample storage need not coexist.
    SoH_Check(restore_state->Capture(),"release framebuffer copy references");
    Framebuffer previous=b;b=Framebuffer();
    b.width=width;b.height=height;b.shader_width=(float)width;b.shader_height=(float)height;b.samples=samples;b.render_target=target;b.read_depth=read_depth;
    b.sampler=previous.sampler;
    bool retain_color=previous.width==width && previous.height==height && previous.samples==samples;
    if(retain_color) {
        b.image=previous.image;previous.image=NULL;b.stored=previous.stored;
        for(int i=0;i<4;++i){b.color_samples[i]=previous.color_samples[i];previous.color_samples[i]=NULL;}
    }
    ReleaseFramebuffer(previous);
    // Package prefetch is expendable; framebuffer functionality is not. Leave
    // room for aligned color/depth fragment textures before committing a resize.
    SoH_ReserveAssetMemory((uint64_t)XGNextMultiple(width,32)*XGNextMultiple(height,32)*4*
        ((retain_color?0:1+(samples>1?samples:0))+(depth?samples:0)));
    D3DMULTISAMPLE_TYPE msaa=samples==4?D3DMULTISAMPLE_4_SAMPLES:samples==2?D3DMULTISAMPLE_2_SAMPLES:D3DMULTISAMPLE_NONE;
    UINT edram_width=samples==4?GPU_EDRAM_TILE_WIDTH_4X:samples==2?GPU_EDRAM_TILE_WIDTH_2X:GPU_EDRAM_TILE_WIDTH_1X;
    UINT edram_height=samples==4?GPU_EDRAM_TILE_HEIGHT_4X:samples==2?GPU_EDRAM_TILE_HEIGHT_2X:GPU_EDRAM_TILE_HEIGHT_1X;
    uint32_t tile_width=XGNextMultiple(width,edram_width),tile_height=XGNextMultiple(height,edram_height);
    UINT full_color=XGSurfaceSize(tile_width,tile_height,D3DFMT_A8R8G8B8,msaa);
    UINT full_depth=depth?XGSurfaceSize(tile_width,tile_height,D3DFMT_D24S8,msaa):0;
    bool tiled=full_color+full_depth>2048;
    if(tiled) {
        // Follow the SDK's PredicatedTiling sample: surface headers cover both
        // EDRAM alignment and texture-tile alignment used by predicated resolves.
        tile_width=XGNextMultiple(tile_width,32);
        tile_height=XGNextMultiple(height,32);
        do {
            if(tile_height<=32)SoH_Fatal("framebuffer tile dimensions",E_INVALIDARG);
            tile_height-=32;
        } while(XGSurfaceSize(tile_width,tile_height,D3DFMT_A8R8G8B8,msaa)+
            (depth?XGSurfaceSize(tile_width,tile_height,D3DFMT_D24S8,msaa):0)>2048);
    }
    uint32_t virtual_width=tiled?XGNextMultiple(width,32):width,virtual_height=tiled?XGNextMultiple(height,32):height;
    for(uint32_t y=0;y<height;y+=tile_height) {D3DRECT tile={0,(LONG)y,(LONG)virtual_width,(LONG)std::min(y+tile_height,virtual_height)};b.tiles.push_back(tile);}
    // The original menu's maximum 2560x1440 at 4x fits within the XDK's 15
    // predication rectangles. Larger custom requests must obey that GPU limit.
    if(b.tiles.size()>15)SoH_Fatal("framebuffer exceeds XDK tiling rectangle limit",E_INVALIDARG);
    UINT color_tiles=XGSurfaceSize(tile_width,tile_height,D3DFMT_A8R8G8B8,msaa);
    D3DSURFACE_PARAMETERS parameters={0};parameters.Base=0;
    if(id==0 && width==1280 && height==720 && samples==1) {b.color=presentation_color;b.color->AddRef();}
    else SoH_Check(soh_device->CreateRenderTarget(tile_width,tile_height,D3DFMT_A8R8G8B8,msaa,0,FALSE,&b.color,&parameters),"framebuffer color surface");
    if(depth) {
        parameters.Base=color_tiles;
        if(id==0 && width==1280 && height==720 && samples==1) {b.depth=presentation_depth;b.depth->AddRef();}
        else SoH_Check(soh_device->CreateDepthStencilSurface(tile_width,tile_height,D3DFMT_D24S8,msaa,0,FALSE,&b.depth,&parameters),"framebuffer depth surface");
    }
    if(!b.image) SoH_Check(soh_device->CreateTexture(width,height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&b.image,NULL),"framebuffer image");
    for(uint32_t sample=0;sample<samples;++sample) {
        if(samples>1 && !b.color_samples[sample]) SoH_Check(soh_device->CreateTexture(width,height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&b.color_samples[sample],NULL),"framebuffer color fragment storage");
        if(depth) SoH_Check(soh_device->CreateTexture(width,height,1,0,D3DFMT_D24S8,D3DPOOL_DEFAULT,&b.depth_samples[sample],NULL),"framebuffer depth fragment storage");
    }
    b.depth_image=read_depth?b.depth_samples[0]:NULL;
    RebindSampledTextures();
    SoH_Log("framebuffer %d: %ux%u, requested %ux / native %ux MSAA, %u EDRAM tile passes",id,width,height,requested_samples,samples,(unsigned)b.tiles.size());
    if(resume)BeginFramebuffer();
}
void BindFramebuffer(int id,float scale) {
    ValidateFramebuffer(id);
    if(!buffers[id].color || !buffers[id].render_target)SoH_Fatal("bind framebuffer",E_INVALIDARG);
    if(active_fb!=id)FinishFramebuffer();
    active_fb=id;
    if(scale!=0)noise_scale=1.0f/scale;
    // Defer restoration until drawing or setting target-dependent viewport /
    // scissor state. SoH often follows this bind with a full color/depth clear.
}
void Clear() {
    if(!live_framebuffer) {buffers[active_fb].stored=false;buffers[active_fb].depth_stored=false;}
    BeginFramebuffer();
    ClearFramebufferTargets(D3DCLEAR_TARGET|(buffers[active_fb].depth?D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL:0));
    buffers[active_fb].dirty=true;
}
void ResolveMSAA(int dst,int src) {
    ValidateFramebuffer(dst);ValidateFramebuffer(src);
    if(dst==src){Resolve(src);return;}
    int previous=active_fb;FinishFramebuffer();
    Framebuffer& source=buffers[src];Framebuffer& target=buffers[dst];
    if(source.width!=target.width || source.height!=target.height || target.samples!=1 || !source.stored) {
        SoH_Log("MSAA resolve src=%d %ux%u samples=%u stored=%u dst=%d %ux%u samples=%u",
            src,source.width,source.height,source.samples,(unsigned)source.stored,dst,target.width,target.height,target.samples);
        SoH_Fatal("MSAA resolve dimensions/source",E_INVALIDARG);
    }
    active_fb=dst;BeginFramebuffer();
    RestoreQuad(source.image,NULL,target.width,target.height,~0u);target.dirty=true;
    FinishFramebuffer();active_fb=previous;BeginFramebuffer();
}
std::unordered_map<std::pair<float,float>,uint16_t,hash_pair_ff> PixelDepth(int id,const std::set<std::pair<float,float>>& coords) {
    SoHPerfScope measure(SOH_PERF_DEPTH);
    ValidateFramebuffer(id);
    std::unordered_map<std::pair<float,float>,uint16_t,hash_pair_ff> result;
    if(coords.empty())return result;
    bool resume=live_framebuffer;FinishFramebuffer();
    Framebuffer& b=buffers[id];
    // can_extract_depth is updated by gfx_run, but Graph may read depth before
    // then when returning from MSAA to the main framebuffer. Xenos retains the
    // actual depth fragments for target switching regardless of that hint.
    IDirect3DTexture9* depth_texture=b.depth_samples[0];
    if(!depth_texture)SoH_Fatal("depth readback unavailable",E_FAIL);
    if(!b.depth_stored) {
        // Graph can query the previous frame's depth immediately after changing
        // MSAA or resolution, before gfx_run draws into the newly allocated
        // target. Initialize its real GPU color/depth storage through the same
        // clear/resolve path used for the first draw, then read sample zero.
        int previous=active_fb;active_fb=id;
        BeginFramebuffer();FinishFramebuffer();active_fb=previous;
        SoH_Log("initialized depth before first draw: framebuffer=%d samples=%u",id,b.samples);
    }
    D3DLOCKED_RECT lock;
    {
        SoHPerfScope wait(SOH_PERF_DEPTH_WAIT);
        SoH_Check(depth_texture->LockRect(0,&lock,NULL,D3DLOCK_READONLY),"depth LockRect");
    }
    XGTEXTURE_DESC desc;XGGetTextureDesc(depth_texture,0,&desc);
    // The caller asks for individual occlusion samples. Let the SDK address
    // those texels directly instead of untiling an entire framebuffer.
    // The source rectangle retains texture packing and the bottom-left contract.
    for(std::set<std::pair<float,float>>::const_iterator it=coords.begin();it!=coords.end();++it) {
        int x=(int)it->first,y=(int)it->second;
        if(x<0 || y<0 || x>=(int)b.width || y>=(int)b.height){result[*it]=65532;continue;}
        DWORD value;POINT destination={0,0};
        RECT source={x,(LONG)b.height-1-y,x+1,(LONG)b.height-y};
        XGUntileTextureLevel(b.width,b.height,0,XGGetGpuFormat(desc.Format),0,&value,sizeof(value),&destination,lock.pBits,&source);
        // Match original D3D11's sample-zero read for multisampled depth.
        result[*it]=(uint16_t)(((value>>8)/16777215.0)*65532.0);
    }
    depth_texture->UnlockRect(0);if(resume)BeginFramebuffer();return result;
}
void* FramebufferImage(int id) {Resolve(id);return buffers[id].image;}
void SelectFramebuffer(int id) {Resolve(id);selected_fb[0]=id;SetNativeTexture(0,buffers[id].image);ApplySampler(0,buffers[id].sampler);}

void DeleteTexture(uint32_t id) {
    UploadSnapshot& snapshot=upload_snapshots[id&(UPLOAD_SNAPSHOT_SLOTS-1)];
    if(snapshot.id==id)snapshot.native=NULL;
    if (id<textures.size() && textures[id].native) {
        for(int i=0;i<2;++i)if(selected_fb[i]<0 && selected[i]==id)SetNativeTexture(i,NULL);
        ReleaseTexture(textures[id].native);textures[id]=Texture();
    }
}
void Filter(FilteringMode mode) { filtering=mode;filtering_key=unsigned(mode)<<4; }
FilteringMode GetFilter() { return filtering; }
void Init() {
    InvalidateNativeState();memset(&native_state_counts,0,sizeof(native_state_counts));
    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp={0}; pp.BackBufferWidth=1280; pp.BackBufferHeight=720;
    pp.BackBufferFormat=D3DFMT_A8R8G8B8; pp.FrontBufferFormat=D3DFMT_LE_X8R8G8B8;
    pp.BackBufferCount=1; pp.EnableAutoDepthStencil=TRUE; pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval=SoH_HeadroomEnabled()?D3DPRESENT_INTERVAL_IMMEDIATE:D3DPRESENT_INTERVAL_ONE;
    // The XDK owns this ring; two bounded tiling brackets can coexist while the
    // CPU records the next one. No title command buffers or replay queue exist.
    pp.RingBufferParameters.SecondarySize=8*1024*1024;
    SoH_Check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,NULL,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&soh_device),"CreateDevice 720p");
    d3d->Release();
    SoH_Check(soh_device->GetRenderTarget(0,&presentation_color),"presentation color");
    SoH_Check(soh_device->GetDepthStencilSurface(&presentation_depth),"presentation depth");
    InitializeRestore();
    // Fast3D's zero-initialized depth-state cache means disabled test/write.
    // Xenos defaults to both enabled. Match the cache before the first draw:
    // title skipping can enter file select without ever changing that cache.
    Depth(false,false);
    // Aliased targets restore exact per-pixel depth; do not use stale hierarchical
    // depth metadata from a different EDRAM owner.
    soh_device->SetRenderState(D3DRS_HIZENABLE,FALSE);
    soh_device->SetRenderState(D3DRS_HIZWRITEENABLE,FALSE);
    soh_device->SetRenderState(D3DRS_HISTENCILENABLE,FALSE);
    soh_device->SetRenderState(D3DRS_HISTENCILWRITEENABLE,FALSE);
    NewFramebuffer(); soh_device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    SoH_Log("Xenos 1280x720: shared EDRAM, tiled 1x/2x/4x MSAA, color/depth retention");
}
void Resize() { FinishFramebuffer(); }
void Start() { ++frame_number;shader_frame_number=(float)frame_number;InvalidateNativeState();CollectTextures(false); }
void ReadFramebufferPixels(IDirect3DTexture9* image,uint32_t width,uint32_t height,std::vector<DWORD>& pixels) {
    D3DLOCKED_RECT lock;SoH_Check(image->LockRect(0,&lock,NULL,D3DLOCK_READONLY),"runtime framebuffer readback");
    pixels.resize(width*height);
    XGTEXTURE_DESC desc;XGGetTextureDesc(image,0,&desc);
    XGUntileTextureLevel(width,height,0,XGGetGpuFormat(desc.Format),0,&pixels[0],width*4,NULL,lock.pBits,NULL);
    image->UnlockRect(0);
}
void End() {
    BeginFramebuffer();
    FinishFramebuffer();
    if (active_fb!=0) { active_fb=0; BeginFramebuffer(); FinishFramebuffer(); }
    if (buffers[0].color!=presentation_color) {
        SoH_Check(soh_device->SetRenderTarget(0,presentation_color),"presentation target");
        soh_device->SetDepthStencilSurface(NULL);
        RestoreQuad(buffers[0].image,NULL,1280,720,~0u);
    }
    // Close the SDK's resource-use interval before Present inserts its vblank
    // synchronization. Otherwise LockRect on the resolved previous-frame depth
    // can wait for unrelated presentation commands in that same interval.
    // InsertFence kicks queued draws/resolves and updates native resource fences;
    // LockRect retains its normal SDK wait. No title-side fence queue or polling.
    // All predicated tiling has ended above, as required by the SDK.
    soh_device->InsertFence();
}
void Finish() {}
}

void SoH_ShutdownGraphics() {
    InvalidateNativeState();
    if(!soh_device)return;
    FinishFramebuffer();
    InvalidateNativeState();
    SetNativeTexture(0,NULL);SetNativeTexture(1,NULL);
    soh_device->SetVertexShader(NULL);soh_device->SetPixelShader(NULL);
    soh_device->SetVertexDeclaration(NULL);
    soh_device->SetRenderTarget(0,NULL);soh_device->SetDepthStencilSurface(NULL);
    soh_device->BlockUntilIdle();
    CollectTextures(true);
    // State blocks retain resources too. Release them before the owned textures
    // and surfaces, then destroy the device to unregister XDK GPU callbacks.
    if(restore_state){restore_state->Release();restore_state=NULL;}
    for(size_t i=0;i<buffers.size();++i)ReleaseFramebuffer(buffers[i]);
    buffers.clear();
    for(size_t i=0;i<textures.size();++i)if(textures[i].native)textures[i].native->Release();
    textures.clear();
    for(unsigned i=0;i<UPLOAD_SNAPSHOT_SLOTS;++i) {
        upload_snapshots[i].native=NULL;std::vector<BYTE>().swap(upload_snapshots[i].rgba);
    }
    for(std::map<std::pair<uint64_t,uint32_t>,ShaderProgram*>::iterator it=shaders.begin();it!=shaders.end();++it) {
        ShaderProgram* shader=it->second;
        shader->vertex->Release();shader->pixel->Release();shader->declaration->Release();delete shader;
    }
    shaders.clear();vertex_programs.clear();vertex_declarations.clear();current_shader=NULL;
    if(restore_vertex){restore_vertex->Release();restore_vertex=NULL;}
    if(restore_color){restore_color->Release();restore_color=NULL;}
    if(restore_depth){restore_depth->Release();restore_depth=NULL;}
    if(restore_declaration){restore_declaration->Release();restore_declaration=NULL;}
    if(presentation_color){presentation_color->Release();presentation_color=NULL;}
    if(presentation_depth){presentation_depth->Release();presentation_depth=NULL;}
    ULONG remaining=soh_device->Release();soh_device=NULL;
    SoH_Log("graphics shutdown device_references=%lu",remaining);
}

void SoH_GetTextureUploadCounts(SoHTextureUploadCounts* counts) {
    *counts=upload_counts;memset(&upload_counts,0,sizeof(upload_counts));
}
void SoH_GetNativeStateCounts(SoHNativeStateCounts* counts) {
    *counts=native_state_counts;
    memset(&native_state_counts,0,sizeof(native_state_counts));
}
