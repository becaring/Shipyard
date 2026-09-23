#include "core.h"
#include "services.h"
#include "pixels.h"
#include "shader_provider.h"

#include "xenos_backend.h"
#include "metrics.h"
#include <xgraphics.h>
#include <vectorintrinsics.h>
#include <stddef.h>
#include <stdio.h>
#include <map>
#include <algorithm>
__declspec(thread) ShipyardPerfSample shipyard_graphics_samples[SHIPYARD_PERF_COUNT]={};
namespace Fast {
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
ShipyardXenos::ShaderCacheOps shader_cache_ops={NULL,NULL};
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
std::map<ShaderProgramKey, ShaderProgram*> shaders;
std::vector<Texture> textures;
// Shipyard invalidates mutable HUD/text textures each presentation. Invalidation
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
ShipyardTextureUploadCounts upload_counts;
struct PendingTextureRelease {
    IDirect3DTexture9* texture;
    size_t bytes;
    PendingTextureRelease(IDirect3DTexture9* value,size_t size):texture(value),bytes(size) {}
};
std::vector<PendingTextureRelease> pending_texture_releases;
size_t pending_texture_bytes;
std::vector<Framebuffer> buffers;
std::vector<int> free_framebuffers;
bool framebuffer_clear_reuse_disabled=false;
unsigned framebuffer_clear_count,framebuffer_restore_avoided,framebuffer_depth_restore_avoided;
ShaderProgram* current_shader;
uint32_t selected[6];
int selected_fb[6] = {-1,-1,-1,-1,-1,-1};
int upload_tile, active_fb;
IDirect3DTexture9* published_framebuffer_image=NULL;
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
    DWORD sampler[6][5];
    __declspec(align(16)) float constants[12];
    __declspec(align(16)) float binding_constants[24];
    bool binding_constants_valid;
    bool sampler_valid[6],constants_valid,alpha_valid;
    unsigned alpha_mode;
    unsigned sampler_key[6];
    IDirect3DVertexShader9* vertex;
    IDirect3DPixelShader9* pixel;
    IDirect3DVertexDeclaration9* declaration;
} native_state;
typedef char NativeConstantsAligned[(__alignof(NativeStateCache)>=16 && offsetof(NativeStateCache,constants)%16==0)?1:-1];
ShipyardNativeStateCounts native_state_counts;
unsigned binding_attempted,binding_written,binding_suppressed;
const D3DSAMPLERSTATETYPE sampler_fields[5]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV};

void InvalidateNativeState() {
    for(int i=0;i<6;++i)native_state.sampler_valid[i]=false;
    native_state.constants_valid=false;native_state.alpha_valid=false;
    native_state.binding_constants_valid=false;
    native_state.vertex=NULL;native_state.pixel=NULL;native_state.declaration=NULL;
}
HRESULT SetNativeTexture(int tile,IDirect3DTexture9* texture) {
    // SetTexture changes the image, not the slot's sampler values. The SDK
    // merges both into its fetch constant. Keep full invalidation at external
    // drawing/state-block boundaries. Literal slots avoid XDK variable-mask work.
    if(tile==0)return xenos_device->SetTexture(0,texture);
    if(tile==1)return xenos_device->SetTexture(1,texture);
    if(tile==2)return xenos_device->SetTexture(2,texture);
    if(tile==3)return xenos_device->SetTexture(3,texture);
    if(tile==4)return xenos_device->SetTexture(4,texture);
    if(tile==5)return xenos_device->SetTexture(5,texture);
    return E_INVALIDARG;
}
__declspec(noinline) void VerifyNativeSampler(int tile,const DWORD* desired) {
    for(unsigned field=0;field<5;++field) {
        DWORD actual;
        Shipyard_Check(xenos_device->GetSamplerState(tile,sampler_fields[field],&actual),"verify native sampler");
        if(actual!=desired[field])Shipyard_Fatal("reused native sampler differs from requested state",E_FAIL);
    }
    ++native_state_counts.sampler_verified;
}
__declspec(noinline) void VerifyNativeConstants(const float* constants) {
    float actual[12];
    Shipyard_Check(xenos_device->GetPixelShaderConstantF(0,actual,3),"verify native pixel constants");
    if(memcmp(actual,constants,sizeof(actual)))Shipyard_Fatal("reused native pixel constants differ from requested values",E_FAIL);
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
        if(Shipyard_VerifyFast3DReuse())VerifyNativeConstants(constants);
        ++native_state_counts.constants_suppressed;
        return;
    }
    // One small upload is cheaper than scalar range scans and split SDK calls.
    // Retain the unchanged-block fast path across compatible shader switches.
    xenos_device->SetPixelShaderConstantF(0,constants,3);
    __stvx(__lvx(constants,0),native_state.constants,0);
    __stvx(__lvx(constants,16),native_state.constants,16);
    __stvx(__lvx(constants,32),native_state.constants,32);
    native_state.constants_valid=true;
    ++native_state_counts.constants_written;
}

void SetNativeBindingConstants(const float* values) {
    ++binding_attempted;
    float* cached=native_state.binding_constants;
    typedef char BindingConstantsAligned[(offsetof(NativeStateCache,binding_constants)%16==0)?1:-1];
    const __vector4 d0=__vxor(__lvx(cached,0),__lvx(values,0));
    const __vector4 d1=__vxor(__lvx(cached,16),__lvx(values,16));
    const __vector4 d2=__vxor(__lvx(cached,32),__lvx(values,32));
    const __vector4 d3=__vxor(__lvx(cached,48),__lvx(values,48));
    const __vector4 d4=__vxor(__lvx(cached,64),__lvx(values,64));
    const __vector4 d5=__vxor(__lvx(cached,80),__lvx(values,80));
    unsigned condition;
    __vcmpequwR(__vor(__vor(__vor(d0,d1),__vor(d2,d3)),__vor(d4,d5)),__vspltisw(0),&condition);
    if(native_state.binding_constants_valid && (condition&0x80)!=0) {
        if(Shipyard_VerifyFast3DReuse()) {
            float actual[24];
            Shipyard_Check(xenos_device->GetPixelShaderConstantF(8,actual,6),"verify native binding constants");
            if(memcmp(actual,values,sizeof(actual)))Shipyard_Fatal("reused binding constants differ",E_FAIL);
        }
        ++binding_suppressed;
        return;
    }
    Shipyard_Check(xenos_device->SetPixelShaderConstantF(8,values,6),"binding constants");
    __stvx(__lvx(values,0),cached,0);
    __stvx(__lvx(values,16),cached,16);
    __stvx(__lvx(values,32),cached,32);
    __stvx(__lvx(values,48),cached,48);
    __stvx(__lvx(values,64),cached,64);
    __stvx(__lvx(values,80),cached,80);
    native_state.binding_constants_valid=true;
    ++binding_written;
}

template<int tile> void ApplySamplerSlot(const TextureSampler& sampler) {
    const unsigned key=sampler.key|filtering_key;
    if(native_state.sampler_valid[tile] && native_state.sampler_key[tile]==key) {
        native_state_counts.sampler_attempted+=5;native_state_counts.sampler_suppressed+=5;
        if(Shipyard_VerifyFast3DReuse())VerifyNativeSampler(tile,native_state.sampler[tile]);
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
        if(Shipyard_VerifyFast3DReuse())VerifyNativeSampler(tile,cached);
        native_state_counts.sampler_suppressed+=5;
        return;
    }
    // A real change must not rewrite the other four independent fields.
#define SHIPYARD_SAMPLER_FIELD(index,field,value) \
    if(!native_state.sampler_valid[tile] || cached[index]!=(DWORD)(value)) { \
        xenos_device->SetSamplerState(tile,field,value);cached[index]=(DWORD)(value); \
        ++native_state_counts.sampler_written; \
    } else ++native_state_counts.sampler_suppressed
    SHIPYARD_SAMPLER_FIELD(0,D3DSAMP_MINFILTER,filter);
    SHIPYARD_SAMPLER_FIELD(1,D3DSAMP_MAGFILTER,filter);
    SHIPYARD_SAMPLER_FIELD(2,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    SHIPYARD_SAMPLER_FIELD(3,D3DSAMP_ADDRESSU,address_u);
    SHIPYARD_SAMPLER_FIELD(4,D3DSAMP_ADDRESSV,address_v);
#undef SHIPYARD_SAMPLER_FIELD
    native_state.sampler_valid[tile]=true;
}

void ApplySampler(int tile,const TextureSampler& sampler) {
    switch(tile) {
    case 0:ApplySamplerSlot<0>(sampler);break;case 1:ApplySamplerSlot<1>(sampler);break;
    case 2:ApplySamplerSlot<2>(sampler);break;case 3:ApplySamplerSlot<3>(sampler);break;
    case 4:ApplySamplerSlot<4>(sampler);break;case 5:ApplySamplerSlot<5>(sampler);break;
    default:Shipyard_Fatal("texture sampler index",E_INVALIDARG);
    }
}

void Replace(std::string& text, const std::string& from, const std::string& to) {
    size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
}

GfxClipParameters ClipParameters() { GfxClipParameters p = {true, false}; return p; }
void UnloadShader(ShaderProgram*) { current_shader = NULL; }
void LoadShader(ShaderProgram* shader) {
    // Application c0-c2 are explicit in every generated program. Shader binds
    // preserve those registers; external rendering invalidates the whole cache.
    current_shader = shader;
    if(native_state.declaration!=shader->declaration) {
        Shipyard_Check(xenos_device->SetVertexDeclaration(shader->declaration), "vertex declaration");
        native_state.declaration=shader->declaration;
    }
    if(native_state.vertex!=shader->vertex) {
        Shipyard_Check(xenos_device->SetVertexShader(shader->vertex), "vertex shader");native_state.vertex=shader->vertex;
    }
    if(native_state.pixel!=shader->pixel) {
        Shipyard_Check(xenos_device->SetPixelShader(shader->pixel), "pixel shader");native_state.pixel=shader->pixel;
    }
}
ID3DXBuffer* Compile(const std::string& source, const char* entry, const char* profile) {
    if(shader_cache_ops.load) {
        ID3DXBuffer* cached=shader_cache_ops.load(source,entry,profile);
        if(cached)return cached;
    }
    ID3DXBuffer *code = NULL, *errors = NULL;
    HRESULT hr = D3DXCompileShader(source.c_str(), (UINT)source.size(), NULL, NULL, entry, profile, 0, &code, &errors, NULL);
    if (FAILED(hr)) {
        Shipyard_Log("shader %s: %s", entry, errors ? (char*)errors->GetBufferPointer() : "unknown");
        Shipyard_WriteFile("failed-shader.hlsl", source.data(), source.size());
    }
    if (errors) errors->Release();
    Shipyard_Check(hr, "compile Shipyard combiner");
    if(shader_cache_ops.store)shader_cache_ops.store(source,entry,profile,code);
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
ShaderProgram* CreateShader(const ShaderProgramKey& key) {
    ShipyardPerfScope measure(SHIPYARD_PERF_SHADER);
    ShaderProgram* shader = new ShaderProgram();
    shader->key=key;
    std::string source, hlsl, error;
    if(!Shipyard_BuildFast3DShader(key,shader->features,shader->floats,hlsl,error) ||
       !Shipyard_LowerShader(hlsl,source,error)) {
        Shipyard_Log("Fast3D shader generation/lowering failed: %s",error.c_str());
        delete shader; Shipyard_Fatal(error.c_str(),E_FAIL);
    }
    ID3DXBuffer* code = Compile(source, "VSMain", "vs_3_0");
    const std::string vertex_key((const char*)code->GetBufferPointer(),code->GetBufferSize());
    std::map<std::string,IDirect3DVertexShader9*>::iterator existing_vertex=vertex_programs.find(vertex_key);
    if(existing_vertex!=vertex_programs.end()) {shader->vertex=existing_vertex->second;shader->vertex->AddRef();}
    else {
        Shipyard_Check(xenos_device->CreateVertexShader((DWORD*)code->GetBufferPointer(), &shader->vertex), "CreateVertexShader");
        vertex_programs.insert(std::make_pair(vertex_key,shader->vertex));
    }
    code->Release();
    code = Compile(source, "PSMain", "ps_3_0");
    Shipyard_Check(xenos_device->CreatePixelShader((DWORD*)code->GetBufferPointer(), &shader->pixel), "CreatePixelShader"); code->Release();
    std::vector<D3DVERTEXELEMENT9> elements;
    WORD offset = 0;
    Element(elements, offset, 4, D3DDECLUSAGE_POSITION, 0);
    for (int i = 0; i < 2; ++i) if (shader->features.usedTextures[i]) {
        Element(elements, offset, 2, D3DDECLUSAGE_TEXCOORD, i);
        for (int j = 0; j < 2; ++j) if (shader->features.clamp[i][j]) Element(elements, offset, 1, D3DDECLUSAGE_TEXCOORD, 11+i*2+j);
    }
    if (shader->features.opt_fog) Element(elements, offset, 4, D3DDECLUSAGE_TEXCOORD, 9);
    if (shader->features.opt_grayscale) Element(elements, offset, 4, D3DDECLUSAGE_TEXCOORD, 10);
    for (int i = 0; i < shader->features.numInputs; ++i) Element(elements, offset, shader->features.opt_alpha ? 4 : 3, D3DDECLUSAGE_TEXCOORD, i+2);
    D3DVERTEXELEMENT9 end = D3DDECL_END(); elements.push_back(end);
    const std::string declaration_key((const char*)&elements[0],elements.size()*sizeof(elements[0]));
    std::map<std::string,IDirect3DVertexDeclaration9*>::iterator existing_decl=vertex_declarations.find(declaration_key);
    if(existing_decl!=vertex_declarations.end()) {shader->declaration=existing_decl->second;shader->declaration->AddRef();}
    else {
        Shipyard_Check(xenos_device->CreateVertexDeclaration(&elements[0], &shader->declaration), "CreateVertexDeclaration");
        vertex_declarations.insert(std::make_pair(declaration_key,shader->declaration));
    }
    if (offset != shader->floats * sizeof(float)) Shipyard_Fatal("Shipyard vertex stride", E_FAIL);
    shaders[key] = shader; LoadShader(shader);
    return shader;
}
ShaderProgram* Lookup(const ShaderProgramKey& key) {
    std::map<ShaderProgramKey, ShaderProgram*>::iterator it = shaders.find(key);
    return it == shaders.end() ? NULL : it->second;
}
void ShaderInfo(ShaderProgram* shader, uint8_t* inputs, bool used[2]) { *inputs = (uint8_t)shader->features.numInputs; used[0] = shader->features.usedTextures[0]; used[1] = shader->features.usedTextures[1]; }
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
    if (tile < 0 || tile > 5 || id >= textures.size()) Shipyard_Fatal("texture index", E_INVALIDARG);
    upload_tile=tile;selected[tile]=id;selected_fb[tile]=-1;
    Shipyard_Check(SetNativeTexture(tile,textures[id].native),"SetTexture");
    // Fast3D caches sampler settings per texture and does not repeat Sampler()
    // on a cache hit. Restore them whenever that texture is selected again.
    ApplySampler(tile,textures[id].sampler);
}
void VerifyTextureUpload(Texture& tex,const uint8_t* bytes) {
    // Readback is restricted to the existing opt-in, pre-measurement game
    // verification. Complete tiling before a CPU lock; never wait mid-bracket.
    const bool resume=live_framebuffer;
    FinishFramebuffer();xenos_device->BlockUntilIdle();
    D3DLOCKED_RECT lock;
    Shipyard_Check(tex.native->LockRect(0,&lock,NULL,D3DLOCK_READONLY),"verify retained texture LockRect");
    for(uint32_t y=0;y<tex.height;++y) {
        const DWORD* row=(const DWORD*)((const BYTE*)lock.pBits+y*lock.Pitch);
        for(uint32_t x=0;x<tex.width;++x) {
            const BYTE* p=bytes+(y*tex.width+x)*4;
            const DWORD expected=((DWORD)p[3]<<24)|((DWORD)p[0]<<16)|((DWORD)p[1]<<8)|p[2];
            if(row[x]!=expected)Shipyard_Fatal("retained texture differs from requested pixels",E_FAIL);
        }
    }
    Shipyard_Check(tex.native->UnlockRect(0),"verify retained texture UnlockRect");
    ++upload_counts.verified;
    if(resume)BeginFramebuffer();
}
void Upload(const uint8_t* bytes, uint32_t width, uint32_t height) {
    ShipyardPerfScope measure(SHIPYARD_PERF_UPLOAD);
    const uint32_t id=selected[upload_tile];
    Texture& tex = textures[id];
    if (!width || !height || width > 4096 || height > 4096) Shipyard_Fatal("texture dimensions", E_INVALIDARG);
    ++upload_counts.requests;
    const size_t size=width*height*4;
    UploadSnapshot& snapshot=upload_snapshots[id&(UPLOAD_SNAPSHOT_SLOTS-1)];
    if(size<=UPLOAD_SNAPSHOT_BYTES && tex.native && snapshot.native==tex.native && snapshot.id==id &&
       snapshot.width==width && snapshot.height==height && snapshot.rgba.size()==size &&
       memcmp(&snapshot.rgba[0],bytes,size)==0) {
        if(Shipyard_VerifyFast3DReuse())VerifyTextureUpload(tex,bytes);
        ++upload_counts.reused;upload_counts.bytes_saved+=size;
        SelectTexture(upload_tile,id);RebindSampledTextures();return;
    }
    snapshot.native=NULL;
    if (tex.native) {
        for (int i=0;i<6;++i) if (selected_fb[i]<0 && selected[i]==selected[upload_tile]) SetNativeTexture(i,NULL);
        ReleaseTexture(tex.native);
    }
    Shipyard_Check(xenos_device->CreateTexture(width, height, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, &tex.native, NULL), "CreateTexture");
    D3DLOCKED_RECT lock;
    Shipyard_Check(tex.native->LockRect(0, &lock, NULL, 0), "texture LockRect");
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
void Depth(bool test,bool write) { xenos_device->SetRenderState(D3DRS_ZENABLE,test||write); xenos_device->SetRenderState(D3DRS_ZWRITEENABLE,write); xenos_device->SetRenderState(D3DRS_ZFUNC,test?D3DCMP_LESSEQUAL:D3DCMP_ALWAYS); }
void Decal(bool enable) {
    // Match Shipyard's D3D11 rasterizer: offset coplanar decals by their depth
    // slope. A fixed epsilon loses paths on steep or distant floor polygons.
    float slope=enable?-2.0f:0.0f;DWORD bits;memcpy(&bits,&slope,sizeof(bits));
    xenos_device->SetRenderState(D3DRS_DEPTHBIAS,0);
    xenos_device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,bits);
}
void Viewport(int x,int y,int w,int h) {
    // XDK clamps viewport dimensions against the bound surface. Complete the
    // pending target switch (including BeginTiling) before applying Fast3D's
    // viewport, especially after Link's small pause-preview framebuffer.
    BeginFramebuffer();
    D3DVIEWPORT9 v={(DWORD)x,buffers[active_fb].height-y-h,(DWORD)w,(DWORD)h,0,1};
    Shipyard_Check(xenos_device->SetViewport(&v),"viewport");
}
void Scissor(int x,int y,int w,int h) {
    BeginFramebuffer();
    LONG height=(LONG)buffers[active_fb].height; RECT r={x,height-y-h,x+w,height-y};
    xenos_device->SetScissorRect(&r); xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
}
bool requested_alpha;
bool color_writes=true;
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
    xenos_device->SetRenderState(D3DRS_ALPHABLENDENABLE,mode==1);
    xenos_device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    xenos_device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    xenos_device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    // Cutout writes RGB only: preserve destination alpha exactly as the old
    // ZERO/ONE separate-alpha blend, including nonopaque offscreen targets.
    xenos_device->SetRenderState(D3DRS_COLORWRITEENABLE,color_writes?(mode==2?7:15):0);
    xenos_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);
    xenos_device->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ZERO);
    xenos_device->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_ONE);
    xenos_device->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
    native_state.alpha_mode=mode;native_state.alpha_valid=true;
}
void Alpha(bool enable) {requested_alpha=enable;ApplyAlpha();}

void Draw(float* vertices,size_t count,size_t triangles) {
    ShipyardPerfScope measure(SHIPYARD_PERF_DRAW);
    if (!current_shader || count != triangles*3*current_shader->floats) Shipyard_Fatal("Shipyard vertex buffer size",E_INVALIDARG);
    // EndTiling cannot submit a partially filled SDK ring. Retain the target and
    // restart the native bracket before it consumes its reserved command space.
    // The allowance includes generous state-command space for each Shipyard batch.
    if (live_framebuffer && buffers[active_fb].tiles.size()>1 && tiling_draw_bytes+count*sizeof(float)+8192>2*1024*1024)
        FinishFramebuffer();
    BeginFramebuffer();
    __declspec(align(16)) float constants[12] = {shader_frame_number,noise_scale,0,0};
    for (int i=0; i<2; ++i) if(current_shader->features.usedTextures[i]) {
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
    __declspec(align(16)) float binding_info[24]={0};
    for(int binding=0;binding<6;++binding) {
        if(selected_fb[binding]>=0) {const Framebuffer& b=buffers[selected_fb[binding]];binding_info[binding*4]=b.shader_width;binding_info[binding*4+1]=b.shader_height;}
        else if(selected[binding]<textures.size()) {const Texture& t=textures[selected[binding]];binding_info[binding*4]=t.shader_width;binding_info[binding*4+1]=t.shader_height;}
        if(binding>=2) {
            const int base=binding%2;
            if(selected_fb[base]>=0)ApplySampler(binding,buffers[selected_fb[base]].sampler);
            else if(selected[base]<textures.size())ApplySampler(binding,textures[selected[base]].sampler);
        }
    }
    SetNativeBindingConstants(binding_info);
    ApplyAlpha(); // Also handles shader-only changes and external state restoration.
    Shipyard_Check(xenos_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,(UINT)triangles,vertices,(UINT)(current_shader->floats*4)),"Shipyard DrawPrimitiveUP");
    tiling_draw_bytes+=count*sizeof(float)+8192;
    buffers[active_fb].dirty=true;
}
void ValidateFramebuffer(int id) {
    if (id<0 || (size_t)id>=buffers.size() ||
        std::find(free_framebuffers.begin(),free_framebuffers.end(),id)!=free_framebuffers.end())
        Shipyard_Fatal("framebuffer index",E_INVALIDARG);
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
    Shipyard_Check(xenos_device->CreateVertexShader((DWORD*)code->GetBufferPointer(),&restore_vertex),"framebuffer restore VS");code->Release();
    code=Compile(source,"ColorMain","ps_3_0");
    Shipyard_Check(xenos_device->CreatePixelShader((DWORD*)code->GetBufferPointer(),&restore_color),"framebuffer copy PS");code->Release();
    code=Compile(source,"DepthMain","ps_3_0");
    Shipyard_Check(xenos_device->CreatePixelShader((DWORD*)code->GetBufferPointer(),&restore_depth),"framebuffer depth restore PS");code->Release();
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    Shipyard_Check(xenos_device->CreateVertexDeclaration(elements,&restore_declaration),"framebuffer restore declaration");
    Shipyard_Check(xenos_device->CreateStateBlock(D3DSBT_ALL,&restore_state),"framebuffer restore state");
}
void StopTileConstants() {
    if (tile_constants_owned) {xenos_device->GpuDisownPixelShaderConstantF(4,4);tile_constants_owned=false;}
}
void SetTileConstants() {
    // Tile-owned c4..c7 are disjoint from the cached CPU draw block c0..c2.
    const Framebuffer& b=buffers[active_fb];
    if (b.tiles.size()>1) {
        xenos_device->GpuOwnPixelShaderConstantF(4,4);tile_constants_owned=true;
        for (size_t tile=0;tile<b.tiles.size();++tile) {
            xenos_device->SetPredication(D3DPRED_TILE(tile));
            D3DVECTOR4* values=NULL;
            Shipyard_Check(xenos_device->GpuBeginPixelShaderConstantF4(4,&values,4),"tile pixel constants");
            ZeroMemory(values,4*sizeof(D3DVECTOR4));
            values[0].x=(float)b.tiles[tile].x1;values[0].y=(float)b.tiles[tile].y1;
            xenos_device->GpuEndPixelShaderConstantF4();
        }
        xenos_device->SetPredication(0);
    } else {float zero[4]={0};xenos_device->SetPixelShaderConstantF(4,zero,1);}
}
void RestoreQuad(IDirect3DTexture9* color,IDirect3DTexture9* depth,uint32_t width,uint32_t height,DWORD sample_mask,const float* copy_quad=NULL) {
    InvalidateNativeState();
    bool restore_constants=tile_constants_owned;
    StopTileConstants();
    Shipyard_Check(restore_state->Capture(),"capture framebuffer copy state");
    D3DVIEWPORT9 viewport={0,0,width,height,0,1};xenos_device->SetViewport(&viewport);
    xenos_device->SetRenderState(D3DRS_VIEWPORTENABLE,TRUE);
    xenos_device->SetRenderState(D3DRS_HALFPIXELOFFSET,TRUE);
    xenos_device->SetVertexShader(restore_vertex);xenos_device->SetPixelShader(depth?restore_depth:restore_color);
    xenos_device->SetVertexDeclaration(restore_declaration);
    xenos_device->SetRenderState(D3DRS_ZENABLE,depth!=NULL);
    xenos_device->SetRenderState(D3DRS_ZWRITEENABLE,depth!=NULL);
    xenos_device->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);
    xenos_device->SetRenderState(D3DRS_HIZENABLE,FALSE);xenos_device->SetRenderState(D3DRS_HIZWRITEENABLE,FALSE);
    xenos_device->SetRenderState(D3DRS_HISTENCILENABLE,FALSE);xenos_device->SetRenderState(D3DRS_HISTENCILWRITEENABLE,FALSE);
    xenos_device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    xenos_device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);xenos_device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    xenos_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);xenos_device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    xenos_device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);xenos_device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    xenos_device->SetRenderState(D3DRS_DEPTHBIAS,0);xenos_device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
    xenos_device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE);xenos_device->SetRenderState(D3DRS_MULTISAMPLEMASK,sample_mask);
    SetNativeTexture(0,color);SetNativeTexture(1,depth);
    for (int sampler=0;sampler<2;++sampler) {
        xenos_device->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        xenos_device->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        xenos_device->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        xenos_device->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        xenos_device->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    }
    // Xenos HALFPIXELOFFSET already aligns pixel and texel centers. Adding a
    // desktop D3D9 half-pixel translation again shifts retained color/depth on
    // every restore (notably when a newly drawn glyph replaces a cached texture).
    // Match XDK ATG::PostProcess::DrawFullScreenQuad's unshifted clip-space quad.
    float left=-1.0f,right=1.0f,top=1.0f,bottom=-1.0f;
    const float quad[]={left,top,0,0,right,top,1,0,left,bottom,0,1,
                        left,bottom,0,1,right,top,1,0,right,bottom,1,1};
    Shipyard_Check(xenos_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,copy_quad?copy_quad:quad,4*sizeof(float)),"restore framebuffer samples");
    Shipyard_Check(restore_state->Apply(),"restore framebuffer copy state");
    InvalidateNativeState();
    if (restore_constants) SetTileConstants();
}
void ClearFramebufferTargets(DWORD flags) {
    const Framebuffer& b=buffers[active_fb];
    D3DVIEWPORT9 previous;DWORD scissor;
    xenos_device->GetViewport(&previous);
    xenos_device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);
    // XDK Clear clips even explicit rectangles to the current viewport. Shipyard's
    // clear_framebuffer contract clears the complete target, like D3D11/GL.
    D3DVIEWPORT9 full={0,0,b.width,b.height,0,1};
    Shipyard_Check(xenos_device->SetViewport(&full),"full framebuffer clear viewport");
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    Shipyard_Check(xenos_device->Clear(0,NULL,flags,0xff000000,1,0),"clear framebuffer");
    Shipyard_Check(xenos_device->SetViewport(&previous),"restore framebuffer clear viewport");
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);
}
void BeginFramebuffer() {
    if (live_framebuffer) return;
    InvalidateNativeState();
    ShipyardPerfScope measure(SHIPYARD_PERF_RESTORE);
    Framebuffer& b=buffers[active_fb];
    if (!b.color) Shipyard_Fatal("framebuffer render surface unavailable",E_INVALIDARG);
    D3DVIEWPORT9 viewport;xenos_device->GetViewport(&viewport);
    Shipyard_Check(xenos_device->SetRenderTarget(0,b.color),"bind framebuffer color");
    Shipyard_Check(xenos_device->SetDepthStencilSurface(b.depth),"bind framebuffer depth");
    xenos_device->SetViewport(&viewport);
    xenos_device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE);
    xenos_device->SetRenderState(D3DRS_MULTISAMPLEMASK,~0u);
    D3DVECTOR4 clear={0,0,0,1};
    if (b.tiles.size()>1) xenos_device->BeginTiling(0,(DWORD)b.tiles.size(),&b.tiles[0],&clear,1,0);
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
    for (int i=0;i<6;++i) {
        IDirect3DTexture9* texture=NULL;
        if (selected_fb[i]>=0) {texture=buffers[selected_fb[i]].image;ApplySampler(i,buffers[selected_fb[i]].sampler);}
        else if (selected[i]<textures.size()) {texture=textures[selected[i]].native;ApplySampler(i,textures[selected[i]].sampler);}
        SetNativeTexture(i,texture);
    }
}
void FinishFramebuffer() {
    if (!live_framebuffer) return;
    ShipyardPerfScope measure(SHIPYARD_PERF_STORE);
    Framebuffer& b=buffers[active_fb];
    StopTileConstants();
    for(int binding=0;binding<6;++binding)SetNativeTexture(binding,NULL);
    const DWORD fragments[]={D3DRESOLVE_FRAGMENT0,D3DRESOLVE_FRAGMENT1,D3DRESOLVE_FRAGMENT2,D3DRESOLVE_FRAGMENT3};
    D3DVECTOR4 clear={0,0,0,1};
    for (size_t tile=0;tile<b.tiles.size();++tile) {
        if (b.tiles.size()>1) xenos_device->SetPredication(D3DPRED_TILE(tile));
        D3DRECT rectangle=b.tiles[tile];
        rectangle.x2=std::min(rectangle.x2,(LONG)b.width);rectangle.y2=std::min(rectangle.y2,(LONG)b.height);
        D3DPOINT destination={rectangle.x1,rectangle.y1};
        for (uint32_t sample=0;sample<b.samples;++sample) {
            if (b.depth) Shipyard_Check(xenos_device->Resolve(D3DRESOLVE_DEPTHSTENCIL|fragments[sample],&rectangle,
                b.depth_samples[sample],&destination,0,0,NULL,1,0,NULL),"retain depth fragment");
            if (b.samples>1) Shipyard_Check(xenos_device->Resolve(D3DRESOLVE_RENDERTARGET0|fragments[sample],&rectangle,
                b.color_samples[sample],&destination,0,0,NULL,1,0,NULL),"retain color fragment");
        }
        DWORD flags=D3DRESOLVE_RENDERTARGET0|D3DRESOLVE_ALLFRAGMENTS;
        if (b.tiles.size()>1) flags|=D3DRESOLVE_CLEARRENDERTARGET|(b.depth?D3DRESOLVE_CLEARDEPTHSTENCIL:0);
        Shipyard_Check(xenos_device->Resolve(flags,&rectangle,b.image,&destination,0,0,&clear,1,0,NULL),"resolve framebuffer color");
    }
    if (b.tiles.size()>1) {
        xenos_device->SetPredication(0);
        // Resolves above are explicitly predicated per tile, including all depth
        // fragments. Disable EndTiling's automatic single-target resolve.
        Shipyard_Check(xenos_device->EndTiling(0,NULL,NULL,&clear,1,0,NULL),"finish framebuffer tiling");
    }
    b.stored=true;b.depth_stored=b.depth!=NULL;b.dirty=false;live_framebuffer=false;
    InvalidateNativeState();
    RebindSampledTextures();
}
void Resolve(int id) {
    ValidateFramebuffer(id);
    if (live_framebuffer && active_fb==id) {FinishFramebuffer();BeginFramebuffer();}
}
int NewFramebuffer() {
    if(!free_framebuffers.empty()) {int id=free_framebuffers.back();free_framebuffers.pop_back();return id;}
    buffers.push_back(Framebuffer());return (int)buffers.size()-1;
}
void ReleaseFramebuffer(Framebuffer& b) {
    if(b.color)b.color->Release();if(b.depth)b.depth->Release();if(b.image)b.image->Release();
    for(int i=0;i<4;++i){if(b.color_samples[i])b.color_samples[i]->Release();if(b.depth_samples[i])b.depth_samples[i]->Release();}
}
void UpdateFramebuffer(int id,uint32_t width,uint32_t height,uint32_t requested_samples,bool,bool target,bool depth,bool read_depth) {
    ValidateFramebuffer(id);width=std::max(width,1u);height=std::max(height,1u);
    uint32_t samples=requested_samples>=4?4:requested_samples>=2?2:1;
    if (width>4096 || height>4096) Shipyard_Fatal("framebuffer texture dimensions",E_INVALIDARG);
    Framebuffer& b=buffers[id];
    if (b.width==width && b.height==height && b.samples==samples && (b.depth!=NULL)==depth) {
        b.render_target=target;b.read_depth=read_depth;b.depth_image=read_depth?b.depth_samples[0]:NULL;return;
    }
    bool resume=live_framebuffer;FinishFramebuffer();
    InvalidateNativeState();
    for(int binding=0;binding<6;++binding)SetNativeTexture(binding,NULL);
    xenos_device->SetRenderTarget(0,NULL);xenos_device->SetDepthStencilSurface(NULL);
    xenos_device->BlockUntilIdle();
    // A state block may retain the last restore inputs. Drop those references
    // before resizing, so old and new full-size sample storage need not coexist.
    Shipyard_Check(restore_state->Capture(),"release framebuffer copy references");
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
    // No Shipyard asset cache exists here; XDK allocation failure remains fatal.
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
            if(tile_height<=32)Shipyard_Fatal("framebuffer tile dimensions",E_INVALIDARG);
            tile_height-=32;
        } while(XGSurfaceSize(tile_width,tile_height,D3DFMT_A8R8G8B8,msaa)+
            (depth?XGSurfaceSize(tile_width,tile_height,D3DFMT_D24S8,msaa):0)>2048);
    }
    uint32_t virtual_width=tiled?XGNextMultiple(width,32):width,virtual_height=tiled?XGNextMultiple(height,32):height;
    for(uint32_t y=0;y<height;y+=tile_height) {D3DRECT tile={0,(LONG)y,(LONG)virtual_width,(LONG)std::min(y+tile_height,virtual_height)};b.tiles.push_back(tile);}
    // The original menu's maximum 2560x1440 at 4x fits within the XDK's 15
    // predication rectangles. Larger custom requests must obey that GPU limit.
    if(b.tiles.size()>15)Shipyard_Fatal("framebuffer exceeds XDK tiling rectangle limit",E_INVALIDARG);
    UINT color_tiles=XGSurfaceSize(tile_width,tile_height,D3DFMT_A8R8G8B8,msaa);
    D3DSURFACE_PARAMETERS parameters={0};parameters.Base=0;
    if(id==0 && width==1280 && height==720 && samples==1) {b.color=presentation_color;b.color->AddRef();}
    else Shipyard_Check(xenos_device->CreateRenderTarget(tile_width,tile_height,D3DFMT_A8R8G8B8,msaa,0,FALSE,&b.color,&parameters),"framebuffer color surface");
    if(depth) {
        parameters.Base=color_tiles;
        if(id==0 && width==1280 && height==720 && samples==1) {b.depth=presentation_depth;b.depth->AddRef();}
        else Shipyard_Check(xenos_device->CreateDepthStencilSurface(tile_width,tile_height,D3DFMT_D24S8,msaa,0,FALSE,&b.depth,&parameters),"framebuffer depth surface");
    }
    if(!b.image) Shipyard_Check(xenos_device->CreateTexture(width,height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&b.image,NULL),"framebuffer image");
    for(uint32_t sample=0;sample<samples;++sample) {
        if(samples>1 && !b.color_samples[sample]) Shipyard_Check(xenos_device->CreateTexture(width,height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&b.color_samples[sample],NULL),"framebuffer color fragment storage");
        if(depth) Shipyard_Check(xenos_device->CreateTexture(width,height,1,0,D3DFMT_D24S8,D3DPOOL_DEFAULT,&b.depth_samples[sample],NULL),"framebuffer depth fragment storage");
    }
    b.depth_image=read_depth?b.depth_samples[0]:NULL;
    RebindSampledTextures();
    Shipyard_Log("framebuffer %d: %ux%u, requested %ux / native %ux MSAA, %u EDRAM tile passes",id,width,height,requested_samples,samples,(unsigned)b.tiles.size());
    if(resume)BeginFramebuffer();
}
void BindFramebuffer(int id,float scale) {
    ValidateFramebuffer(id);
    if(!buffers[id].color || !buffers[id].render_target)Shipyard_Fatal("bind framebuffer",E_INVALIDARG);
    if(active_fb!=id)FinishFramebuffer();
    active_fb=id;
    if(scale!=0)noise_scale=1.0f/scale;
    // Defer restoration until drawing or setting target-dependent viewport /
    // scissor state. Shipyard often follows this bind with a full color/depth clear.
}
void Clear(bool color,bool depth) {
    if(!color && !depth)return;
    ++framebuffer_clear_count;
    if(!live_framebuffer && !framebuffer_clear_reuse_disabled) {
        Framebuffer& b=buffers[active_fb];
        // Shipyard's discard-before-full-clear optimization, preserving current
        // Fast3D's independent attachment contract. Color-only clears must
        // still restore depth. Depth-only clears retain every color sample.
        if(depth) {
            if(b.stored && b.depth_stored)framebuffer_depth_restore_avoided+=b.samples;
            b.depth_stored=false;
        }
        if(color && (depth || !b.depth)) {
            if(b.stored)framebuffer_restore_avoided+=b.samples;
            b.stored=false;
        }
    }
    BeginFramebuffer();
    const DWORD flags=(color?D3DCLEAR_TARGET:0)|((depth&&buffers[active_fb].depth)?D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL:0);
    if(flags)ClearFramebufferTargets(flags);
    buffers[active_fb].dirty=true;
}
void ResolveMSAA(int dst,int src) {
    ValidateFramebuffer(dst);ValidateFramebuffer(src);
    if(dst==src){Resolve(src);return;}
    int previous=active_fb;FinishFramebuffer();
    Framebuffer& source=buffers[src];Framebuffer& target=buffers[dst];
    if(source.width!=target.width || source.height!=target.height || target.samples!=1 || !source.stored) {
        Shipyard_Log("MSAA resolve src=%d %ux%u samples=%u stored=%u dst=%d %ux%u samples=%u",
            src,source.width,source.height,source.samples,(unsigned)source.stored,dst,target.width,target.height,target.samples);
        Shipyard_Fatal("MSAA resolve dimensions/source",E_INVALIDARG);
    }
    active_fb=dst;BeginFramebuffer();
    RestoreQuad(source.image,NULL,target.width,target.height,~0u);target.dirty=true;
    FinishFramebuffer();active_fb=previous;BeginFramebuffer();
}
std::unordered_map<std::pair<float,float>,uint16_t,hash_pair_ff> PixelDepth(int id,const std::set<std::pair<float,float>>& coords) {
    ShipyardPerfScope measure(SHIPYARD_PERF_DEPTH);
    ValidateFramebuffer(id);
    std::unordered_map<std::pair<float,float>,uint16_t,hash_pair_ff> result;
    if(coords.empty())return result;
    bool resume=live_framebuffer;FinishFramebuffer();
    Framebuffer& b=buffers[id];
    // can_extract_depth is updated by gfx_run, but Graph may read depth before
    // then when returning from MSAA to the main framebuffer. Xenos retains the
    // actual depth fragments for target switching regardless of that hint.
    IDirect3DTexture9* depth_texture=b.depth_samples[0];
    if(!depth_texture)Shipyard_Fatal("depth readback unavailable",E_FAIL);
    if(!b.depth_stored) {
        // Graph can query the previous frame's depth immediately after changing
        // MSAA or resolution, before gfx_run draws into the newly allocated
        // target. Initialize its real GPU color/depth storage through the same
        // clear/resolve path used for the first draw, then read sample zero.
        int previous=active_fb;active_fb=id;
        BeginFramebuffer();FinishFramebuffer();active_fb=previous;
        Shipyard_Log("initialized depth before first draw: framebuffer=%d samples=%u",id,b.samples);
    }
    D3DLOCKED_RECT lock;
    {
        ShipyardPerfScope wait(SHIPYARD_PERF_DEPTH_WAIT);
        Shipyard_Check(depth_texture->LockRect(0,&lock,NULL,D3DLOCK_READONLY),"depth LockRect");
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
void* FramebufferImage(int id) {
    Resolve(id);
    IDirect3DTexture9* image=buffers[id].image;
    if(image!=published_framebuffer_image) {
        if(image)image->AddRef();
        IDirect3DTexture9* previous=published_framebuffer_image;
        published_framebuffer_image=image;
        if(previous)ReleaseTexture(previous);
    }
    return image;
}
void SelectFramebuffer(int id) {Resolve(id);selected_fb[0]=id;SetNativeTexture(0,buffers[id].image);ApplySampler(0,buffers[id].sampler);}

void DeleteTexture(uint32_t id) {
    UploadSnapshot& snapshot=upload_snapshots[id&(UPLOAD_SNAPSHOT_SLOTS-1)];
    if(snapshot.id==id)snapshot.native=NULL;
    if (id<textures.size() && textures[id].native) {
        for(int i=0;i<6;++i)if(selected_fb[i]<0 && selected[i]==id)SetNativeTexture(i,NULL);
        ReleaseTexture(textures[id].native);textures[id]=Texture();
    }
}
void Filter(FilteringMode mode) { filtering=mode;filtering_key=unsigned(mode)<<4; }
FilteringMode GetFilter() { return filtering; }
void InitializeDisplay() {
    if(xenos_device)return;
    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d)Shipyard_Fatal("Direct3DCreate9",E_FAIL);
    D3DPRESENT_PARAMETERS pp={0}; pp.BackBufferWidth=1280; pp.BackBufferHeight=720;
    pp.BackBufferFormat=D3DFMT_A8R8G8B8; pp.FrontBufferFormat=D3DFMT_LE_X8R8G8B8;
    pp.BackBufferCount=1; pp.EnableAutoDepthStencil=TRUE; pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    // The XDK owns this ring; two bounded tiling brackets can coexist while the
    // CPU records the next one. No title command buffers or replay queue exist.
    pp.RingBufferParameters.SecondarySize=8*1024*1024;
    Shipyard_Check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,NULL,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&xenos_device),"CreateDevice 720p");
    d3d->Release();
    Shipyard_Log("Xenos native display ready before system UI");
}
void Init() {
    InvalidateNativeState();memset(&native_state_counts,0,sizeof(native_state_counts));
    InitializeDisplay();
    Shipyard_Check(xenos_device->GetRenderTarget(0,&presentation_color),"presentation color");
    Shipyard_Check(xenos_device->GetDepthStencilSurface(&presentation_depth),"presentation depth");
    InitializeRestore();
    // Fast3D's zero-initialized depth-state cache means disabled test/write.
    // Xenos defaults to both enabled. Match the cache before the first draw:
    // title skipping can enter file select without ever changing that cache.
    Depth(false,false);
    // Aliased targets restore exact per-pixel depth; do not use stale hierarchical
    // depth metadata from a different EDRAM owner.
    xenos_device->SetRenderState(D3DRS_HIZENABLE,FALSE);
    xenos_device->SetRenderState(D3DRS_HIZWRITEENABLE,FALSE);
    xenos_device->SetRenderState(D3DRS_HISTENCILENABLE,FALSE);
    xenos_device->SetRenderState(D3DRS_HISTENCILWRITEENABLE,FALSE);
    NewFramebuffer(); xenos_device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    Shipyard_Log("Xenos 1280x720: shared EDRAM, tiled 1x/2x/4x MSAA, color/depth retention");
}
void Resize() { FinishFramebuffer(); }
void Start() { ++frame_number;shader_frame_number=(float)frame_number;InvalidateNativeState();CollectTextures(false); }
void ReadFramebufferPixels(IDirect3DTexture9* image,uint32_t width,uint32_t height,std::vector<DWORD>& pixels) {
    D3DLOCKED_RECT lock;Shipyard_Check(image->LockRect(0,&lock,NULL,D3DLOCK_READONLY),"runtime framebuffer readback");
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
        Shipyard_Check(xenos_device->SetRenderTarget(0,presentation_color),"presentation target");
        xenos_device->SetDepthStencilSurface(NULL);
        RestoreQuad(buffers[0].image,NULL,1280,720,~0u);
    }
    // Close the SDK's resource-use interval before Present inserts its vblank
    // synchronization. Otherwise LockRect on the resolved previous-frame depth
    // can wait for unrelated presentation commands in that same interval.
    // InsertFence kicks queued draws/resolves and updates native resource fences;
    // LockRect retains its normal SDK wait. No title-side fence queue or polling.
    // All predicated tiling has ended above, as required by the SDK.
    xenos_device->InsertFence();
}
void Finish() {}
}
void Shipyard_ShutdownGraphics() {
    InvalidateNativeState();
    if(!xenos_device)return;
    FinishFramebuffer();
    InvalidateNativeState();
    for(int binding=0;binding<6;++binding)SetNativeTexture(binding,NULL);
    xenos_device->SetVertexShader(NULL);xenos_device->SetPixelShader(NULL);
    xenos_device->SetVertexDeclaration(NULL);
    xenos_device->SetRenderTarget(0,NULL);xenos_device->SetDepthStencilSurface(NULL);
    xenos_device->BlockUntilIdle();
    CollectTextures(true);
    if(published_framebuffer_image){published_framebuffer_image->Release();published_framebuffer_image=NULL;}
    // State blocks retain resources too. Release them before the owned textures
    // and surfaces, then destroy the device to unregister XDK GPU callbacks.
    if(restore_state){restore_state->Release();restore_state=NULL;}
    for(size_t i=0;i<buffers.size();++i)ReleaseFramebuffer(buffers[i]);
    buffers.clear();free_framebuffers.clear();
    for(size_t i=0;i<textures.size();++i)if(textures[i].native)textures[i].native->Release();
    textures.clear();
    for(unsigned i=0;i<UPLOAD_SNAPSHOT_SLOTS;++i) {
        upload_snapshots[i].native=NULL;std::vector<BYTE>().swap(upload_snapshots[i].rgba);
    }
    for(std::map<ShaderProgramKey,ShaderProgram*>::iterator it=shaders.begin();it!=shaders.end();++it) {
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
    ULONG remaining=xenos_device->Release();xenos_device=NULL;
    Shipyard_Log("graphics shutdown device_references=%lu",remaining);
}


namespace {
void CopyRegion(int dst,int src,int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1) {
    ValidateFramebuffer(src);ValidateFramebuffer(dst);
    if(sx1<=sx0 || sy1<=sy0 || dx1<=dx0 || dy1<=dy0)return;
    Framebuffer& source=buffers[src];Framebuffer& target=buffers[dst];
    if(sx0<0 || sy0<0 || sx1>(int)source.width || sy1>(int)source.height ||
       dx0<0 || dy0<0 || dx1>(int)target.width || dy1>(int)target.height)
        Shipyard_Fatal("framebuffer copy rectangle",E_INVALIDARG);
    const int previous=active_fb;const bool resume=live_framebuffer;
    FinishFramebuffer();
    if(!source.stored) {
        active_fb=src;BeginFramebuffer();FinishFramebuffer();
    }
    // All reads come from the stored image. EDRAM is separate, so overlapping
    // self-copy also samples an immutable snapshot until the final resolve.
    active_fb=dst;BeginFramebuffer();
    const float left=2.0f*dx0/target.width-1.0f,right=2.0f*dx1/target.width-1.0f;
    const float top=1.0f-2.0f*dy0/target.height,bottom=1.0f-2.0f*dy1/target.height;
    const float u0=(float)sx0/source.width,u1=(float)sx1/source.width;
    const float v0=(float)sy0/source.height,v1=(float)sy1/source.height;
    const float quad[]={left,top,u0,v0,right,top,u1,v0,left,bottom,u0,v1,
                        left,bottom,u0,v1,right,top,u1,v0,right,bottom,u1,v1};
    RestoreQuad(source.image,NULL,target.width,target.height,~0u,quad);
    target.dirty=true;FinishFramebuffer();active_fb=previous;
    if(resume)BeginFramebuffer();
}
void ReadColor(int id,uint32_t width,uint32_t height,uint16_t* rgba16) {
    ValidateFramebuffer(id);
    if(!rgba16 || !width || !height)return;
    if(width>4096 || height>4096)Shipyard_Fatal("framebuffer readback dimensions",E_INVALIDARG);
    const int previous=active_fb;const bool resume=live_framebuffer;
    FinishFramebuffer();
    Framebuffer& b=buffers[id];
    if(!b.stored) {active_fb=id;BeginFramebuffer();FinishFramebuffer();}
    std::vector<DWORD> pixels;ReadFramebufferPixels(b.image,b.width,b.height,pixels);
    memset(rgba16,0,(size_t)width*height*sizeof(uint16_t));
    for(uint32_t y=0;y<std::min(height,b.height);++y)
        for(uint32_t x=0;x<std::min(width,b.width);++x) {
            const DWORD p=pixels[y*b.width+x];
            // Same rounded RGB5551 conversion as pinned DX11, with Xenos' ARGB
            // native readback channels. Caller owns native uint16_t words.
            rgba16[(size_t)y*width+x]=Shipyard::Xenos::PackReadbackPixel(p);
        }
    active_fb=previous;if(resume)BeginFramebuffer();
}
void DestroyFramebufferNative(int id) {
    if(id<0 || (size_t)id>=buffers.size() ||
       std::find(free_framebuffers.begin(),free_framebuffers.end(),id)!=free_framebuffers.end())return;
    const bool resume=live_framebuffer;
    FinishFramebuffer();
    for(int slot=0;slot<6;++slot)if(selected_fb[slot]==id) {
        selected_fb[slot]=-1;selected[slot]=UINT32_MAX;SetNativeTexture(slot,NULL);
    }
    xenos_device->SetRenderTarget(0,NULL);xenos_device->SetDepthStencilSurface(NULL);
    xenos_device->BlockUntilIdle();
    Shipyard_Check(restore_state->Capture(),"release destroyed framebuffer references");
    ReleaseFramebuffer(buffers[id]);buffers[id]=Framebuffer();free_framebuffers.push_back(id);
    if(active_fb==id) {
        active_fb=-1;
        for(size_t candidate=0;candidate<buffers.size();++candidate)
            if(std::find(free_framebuffers.begin(),free_framebuffers.end(),(int)candidate)==free_framebuffers.end() &&
               buffers[candidate].color){active_fb=(int)candidate;break;}
    }
    InvalidateNativeState();
    if(resume && active_fb>=0)BeginFramebuffer();
}
void ClearRegion(float x0,float y0,float x1,float y1) {
    BeginFramebuffer();
    const Framebuffer& b=buffers[active_fb];
    x0=std::max(0.0f,std::min(1.0f,x0));x1=std::max(0.0f,std::min(1.0f,x1));
    y0=std::max(0.0f,std::min(1.0f,y0));y1=std::max(0.0f,std::min(1.0f,y1));
    if(x1<=x0 || y1<=y0)return;
    D3DRECT rect={(LONG)(x0*b.width),(LONG)(y0*b.height),(LONG)(x1*b.width),(LONG)(y1*b.height)};
    D3DVIEWPORT9 previous;DWORD scissor;
    xenos_device->GetViewport(&previous);xenos_device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);
    D3DVIEWPORT9 full={0,0,b.width,b.height,0,1};xenos_device->SetViewport(&full);
    xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    Shipyard_Check(xenos_device->Clear(1,&rect,D3DCLEAR_TARGET,0xff000000,1,0),"framebuffer color region clear");
    xenos_device->SetViewport(&previous);xenos_device->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);
    buffers[active_fb].dirty=true;
}
}

namespace ShipyardXenos {
void SetShaderCache(const ShaderCacheOps* operations) {
    if(operations)shader_cache_ops=*operations;
    else {shader_cache_ops.load=NULL;shader_cache_ops.store=NULL;}
}
void CaptureAndResetCounters(NativeCounters* n) {
    if(!n)return;
    memset(n,0,sizeof(*n));
    n->samplerAttempted=native_state_counts.sampler_attempted;
    n->samplerWritten=native_state_counts.sampler_written;
    n->samplerSuppressed=native_state_counts.sampler_suppressed;
    n->constantsAttempted=native_state_counts.constants_attempted;
    n->constantsWritten=native_state_counts.constants_written;
    n->constantsSuppressed=native_state_counts.constants_suppressed;
    n->bindingAttempted=binding_attempted;
    n->bindingWritten=binding_written;
    n->bindingSuppressed=binding_suppressed;
    n->uploadRequests=upload_counts.requests;
    n->uploadReused=upload_counts.reused;
    n->uploadSavedBytes=upload_counts.bytes_saved;
    n->clears=framebuffer_clear_count;
    n->restoresAvoided=framebuffer_restore_avoided;
    n->depthRestoresAvoided=framebuffer_depth_restore_avoided;
    n->pendingTextures=(unsigned)pending_texture_releases.size();
    n->pendingBytes=(unsigned)pending_texture_bytes;
    for(size_t i=0;i<buffers.size();++i)if(published_framebuffer_image &&
        buffers[i].image==published_framebuffer_image) {
        n->width=buffers[i].width;n->height=buffers[i].height;
        n->samples=buffers[i].samples;n->tiles=(unsigned)buffers[i].tiles.size();
        break;
    }
    memset(&native_state_counts,0,sizeof(native_state_counts));
    memset(&upload_counts,0,sizeof(upload_counts));
    binding_attempted=binding_written=binding_suppressed=0;
    framebuffer_clear_count=framebuffer_restore_avoided=framebuffer_depth_restore_avoided=0;
}
GfxClipParameters ClipParameters(){return ::Fast::ClipParameters();}
void UnloadShader(ShaderProgram* p){::Fast::UnloadShader(p);}
void LoadShader(ShaderProgram* p){::Fast::LoadShader(p);}
ShaderProgram* CreateShader(const ShaderProgramKey& key){return ::Fast::CreateShader(key);}
ShaderProgram* LookupShader(const ShaderProgramKey& key){return ::Fast::Lookup(key);}
ShaderProgramKey ShaderKey(const ShaderProgram* p){return p->key;}
ShaderProgram* CurrentShader(){return current_shader;}
void ShaderInfo(ShaderProgram* p,uint8_t* n,bool used[2]){::Fast::ShaderInfo(p,n,used);}
uint32_t NewTexture(){return ::Fast::NewTexture();}
void SelectTexture(int slot,uint32_t id){::Fast::SelectTexture(slot,id);}
void UploadTexture(const uint8_t* p,uint32_t w,uint32_t h){::Fast::Upload(p,w,h);}
void SetSamplerParameters(int slot,bool linear,uint32_t s,uint32_t t){::Fast::Sampler(slot,linear,s,t);}
void SetDepthTestAndMask(bool test,bool write){::Fast::Depth(test,write);}
void SetZmodeDecal(bool decal){::Fast::Decal(decal);}
void SetViewport(int x,int y,int w,int h){::Fast::Viewport(x,y,w,h);}
void SetScissor(int x,int y,int w,int h){::Fast::Scissor(x,y,w,h);}
void SetUseAlpha(bool alpha){::Fast::Alpha(alpha);}
void DrawTriangles(float* v,size_t count,size_t triangles){::Fast::Draw(v,count,triangles);}
void InitializeDisplay(){::Fast::InitializeDisplay();}
HRESULT PresentStartupUI(){
    if(!xenos_device || !buffers.empty())return E_UNEXPECTED;
    HRESULT result=xenos_device->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff000000,1,0);
    return FAILED(result)?result:xenos_device->Present(NULL,NULL,NULL,NULL);
}
void BeginGuiDraw(){::Fast::BeginFramebuffer();}
void InvalidateGuiState(){::Fast::InvalidateNativeState();}
void Init(){::Fast::Init();}
void Resize(){::Fast::Resize();}
void StartFrame(){::Fast::Start();}
void EndFrame(){::Fast::End();}
void FinishRender(){::Fast::Finish();}
void Shutdown(){::Fast::Shipyard_ShutdownGraphics();}
int CreateFramebuffer(){return ::Fast::NewFramebuffer();}
void DestroyFramebuffer(int id){::Fast::DestroyFramebufferNative(id);}
void UpdateFramebuffer(int id,uint32_t w,uint32_t h,uint32_t msaa,bool flip,bool target,bool depth,bool read){
    ::Fast::UpdateFramebuffer(id,w,h,msaa,flip,target,depth,read);
}
void StartDrawToFramebuffer(int id,float noise){::Fast::BindFramebuffer(id,noise);}
void CopyFramebuffer(int dst,int src,int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1){
    ::Fast::CopyRegion(dst,src,sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1);
}
void ClearFramebuffer(bool color,bool depth){::Fast::Clear(color,depth);}
void SetColorWriteMask(bool enabled){color_writes=enabled;native_state.alpha_valid=false;::Fast::ApplyAlpha();}
void ClearColorRegion(float x0,float y0,float x1,float y1){::Fast::ClearRegion(x0,y0,x1,y1);}
void ReadFramebufferToCPU(int id,uint32_t w,uint32_t h,uint16_t* p){::Fast::ReadColor(id,w,h,p);}
void ResolveMSAAColorBuffer(int dst,int src){::Fast::ResolveMSAA(dst,src);}
std::unordered_map<std::pair<float,float>,uint16_t,hash_pair_ff>
GetPixelDepth(int id,const std::set<std::pair<float,float> >& xy){return ::Fast::PixelDepth(id,xy);}
void* GetFramebufferTextureId(int id){return ::Fast::FramebufferImage(id);}
void SelectTextureFb(int id){::Fast::SelectFramebuffer(id);}
bool FbNeedsSampleVFlip(int id){::Fast::ValidateFramebuffer(id);return false;}
void DeleteTexture(uint32_t id){::Fast::DeleteTexture(id);}
void SetTextureFilter(FilteringMode mode){::Fast::Filter(mode);}
FilteringMode GetTextureFilter(){return ::Fast::GetFilter();}
ImTextureID GetTextureById(int id){
    if(id<0 || (size_t)id>=textures.size())Shipyard_Fatal("GUI texture index",E_INVALIDARG);
    return textures[id].native;
}
bool ReadPresentedFrameRGBA(std::vector<uint8_t>& rgba,uint32_t& width,uint32_t& height) {
    if(!xenos_device || buffers.empty() || live_framebuffer)return false;
    const Framebuffer& frame=buffers[0];
    if(!frame.stored || !frame.image || !frame.width || !frame.height)return false;
    width=frame.width;height=frame.height;
    std::vector<DWORD> pixels((size_t)width*height);
    rgba.resize((size_t)width*height*4);
    D3DLOCKED_RECT lock;
    if(FAILED(frame.image->LockRect(0,&lock,NULL,D3DLOCK_READONLY)))return false;
    XGTEXTURE_DESC desc;XGGetTextureDesc(frame.image,0,&desc);
    XGUntileTextureLevel(width,height,0,XGGetGpuFormat(desc.Format),0,&pixels[0],width*4,NULL,lock.pBits,NULL);
    if(FAILED(frame.image->UnlockRect(0)))return false;
    for(size_t i=0;i<pixels.size();++i) {
        const DWORD p=pixels[i];
        rgba[i*4]=(uint8_t)(p>>16);rgba[i*4+1]=(uint8_t)(p>>8);
        rgba[i*4+2]=(uint8_t)p;rgba[i*4+3]=(uint8_t)(p>>24);
    }
    return true;
}
}
} // namespace Fast
