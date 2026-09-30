#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <stdexcept>
typedef void IDirect3DTexture9;
struct TextureSampler {
    bool linear; uint32_t s,t,key;
    TextureSampler(bool value=false):linear(value),s(0),t(0),key(value?128:0) {}
    void Set(bool value,uint32_t u,uint32_t v) { linear=value;s=u;t=v;key=(u&3)|((v&3)<<2)|(value?128:0); }
};
struct Texture { void* native; float shader_width,shader_height; TextureSampler sampler;
    Texture():native(NULL),shader_width(0),shader_height(0) {} };
struct Framebuffer { void* color; void* image; bool render_target; float shader_width,shader_height; TextureSampler sampler;
    Framebuffer():color((void*)1),image((void*)2),render_target(true),shader_width(640),shader_height(480),sampler(true) {} };
struct Features { bool usedTextures[2]; };
struct ShaderProgram { Features features; void* declaration;void* vertex;void* pixel; };
enum FilteringMode { FILTER_POINT,FILTER_LINEAR,FILTER_THREE_POINT };
FilteringMode filtering=FILTER_THREE_POINT;
unsigned filtering_key=unsigned(FILTER_THREE_POINT)<<4;
ShaderProgram shaders[3]={{{true,true}},{{false,true}},{{false,false}}};
ShaderProgram* current_shader=&shaders[0];
std::vector<Texture> textures;
std::vector<Framebuffer> buffers(2);
uint32_t selected[6]={}; int selected_fb[6]={-1,-1,-1,-1,-1,-1};
int upload_tile,active_fb;
float shader_frame_number=1,noise_scale=1,noise_scale_input=1;
bool draw_state_prepared=false;
#include "xenos/sampler_certificate.h"
Fast::ShipyardXenos::SamplerCertificate sampler_certificate;
unsigned draw_sampler_mask=0;
struct NativeState { unsigned sampler[6][5];float constants[12],binding_constants[24];
    bool sampler_valid[6],constants_valid,alpha_valid,binding_constants_valid;void *vertex,*pixel,*declaration;
} native_state={};
unsigned constantCalls,bindingCalls,samplerCalls,verifyCalls;
std::vector<int> productionOrder, referenceOrder;
bool verifyReuse;
void require(bool b,const char* message) { if(!b) { std::fprintf(stderr,"FAIL %s\n",message);std::exit(1); } }
void Shipyard_Fatal(const char*,int) { throw std::runtime_error("fatal"); }
void Shipyard_Check(int,const char*) {}
enum { E_INVALIDARG=1, UPLOAD_SNAPSHOT_SLOTS=32 };
struct UploadSnapshot { uint32_t id;void* native; } upload_snapshots[UPLOAD_SNAPSHOT_SLOTS]={};
struct Device { int SetVertexDeclaration(void*){return 0;} int SetVertexShader(void*){return 0;}int SetPixelShader(void*){return 0;} } device;
Device* xenos_device=&device;
int SetNativeTexture(int,void*) { return 0; }
void ReleaseTexture(void*, bool = false) {}
void FinishFramebuffer() {}
void Resolve(int) {}
void ValidateFramebuffer(int id) { require(id>=0 && size_t(id)<buffers.size(),"valid framebuffer"); }
bool Shipyard_VerifyFast3DReuse(){return verifyReuse;}
void VerifyNativeConstants(const float* values) { ++verifyCalls;require(!std::memcmp(values,native_state.constants,sizeof(native_state.constants)),"verify constants"); }
void VerifyNativeBindingConstants(const float* values) { ++verifyCalls;require(!std::memcmp(values,native_state.binding_constants,sizeof(native_state.binding_constants)),"verify bindings"); }
void VerifyNativeSampler(int slot,const unsigned* values) { ++verifyCalls;require(!std::memcmp(values,native_state.sampler[slot],sizeof(native_state.sampler[slot])),"verify sampler"); }
void SamplerValues(unsigned* dst,const TextureSampler& s) { dst[0]=s.linear;dst[1]=s.s;dst[2]=s.t;dst[3]=filtering;dst[4]=0; }
void ApplySampler(int slot,const TextureSampler& s) { productionOrder.push_back(slot);draw_state_prepared=false;++samplerCalls;SamplerValues(native_state.sampler[slot],s);native_state.sampler_valid[slot]=true; }
void SetNativeConstants(const float* values) { productionOrder.push_back(10);++constantCalls;std::memcpy(native_state.constants,values,sizeof(native_state.constants)); }
void SetNativeBindingConstants(const float* values) { productionOrder.push_back(11);++bindingCalls;std::memcpy(native_state.binding_constants,values,sizeof(native_state.binding_constants)); }
namespace Oracle {
float constants[12],bindings[24];unsigned samplers[6][5];
void ApplySampler(int slot,const TextureSampler& s) { referenceOrder.push_back(slot);SamplerValues(samplers[slot],s); }
void SetNativeConstants(const float* values) { referenceOrder.push_back(10);std::memcpy(constants,values,sizeof(constants)); }
void SetNativeBindingConstants(const float* values) { referenceOrder.push_back(11);std::memcpy(bindings,values,sizeof(bindings)); }
#include "draw_preparation_reference.inc"
}
bool timing_enabled;
struct { unsigned preparationRebuilt, preparationReused; } timing_counts;
#include "production.inc"
void compare() {
    bool rebuilt = !draw_state_prepared;
    productionOrder.clear();referenceOrder.clear();
    PrepareDrawState();Oracle::ReferencePrepareDrawState();
    if (rebuilt) require(productionOrder == referenceOrder,"native preparation order differs");
    require(!std::memcmp(native_state.constants,Oracle::constants,sizeof(Oracle::constants)),"original constants differ");
    require(!std::memcmp(native_state.binding_constants,Oracle::bindings,sizeof(Oracle::bindings)),"original bindings differ");
    // Only slots written by original preparation are authoritative for this draw.
    for(int slot=0;slot<6;++slot) {
        int base=slot<2?slot:slot%2;
        bool available=selected_fb[base]>=0 || selected[base]<textures.size();
        if(available && (slot>=2 || current_shader->features.usedTextures[slot]))
            require(!std::memcmp(native_state.sampler[slot],Oracle::samplers[slot],sizeof(Oracle::samplers[slot])),"original sampler differs");
    }
}
void repeated() { compare();compare(); }
int main() {
    require(sizeof(void*)==4,"ILP32 fixture");
    NewTexture();NewTexture();
    for(unsigned i=0;i<2;++i) { textures[i].native=(void*)(i+1);textures[i].shader_width=float(32+i);textures[i].shader_height=float(64+i); }
    for(int slot=0;slot<6;++slot)SelectTexture(slot,slot%2);
    constantCalls=bindingCalls=samplerCalls=0;
    timing_enabled=true;
    for(unsigned i=0;i<100;++i)compare();
    require(timing_counts.preparationRebuilt==1 && timing_counts.preparationReused==99,"actual opt-in preparation counts");
    timing_enabled=false;
    require(constantCalls==1 && bindingCalls==1 && samplerCalls==6,"100 stable draws must prepare constants/bindings once and samplers six times");
    verifyReuse=true;verifyCalls=0;compare();require(verifyCalls==8,"reuse verification must cover both blocks and six samplers");verifyReuse=false;
    Sampler(1,true,1,2);repeated(); SelectTexture(0,1);repeated();Sampler(0,false,2,1);repeated();
    Filter(FILTER_LINEAR);repeated();Filter(FILTER_POINT);repeated();Filter(FILTER_THREE_POINT);repeated();
    LoadShader(&shaders[1]);repeated();LoadShader(&shaders[2]);repeated();LoadShader(&shaders[0]);repeated();
    BindFramebuffer(1,2);repeated();SelectFramebuffer(1);repeated();Sampler(0,true,1,1);repeated();
    // Resize/resolve/external rendering all traverse native invalidation.
    InvalidateNativeState();buffers[1].shader_width=123;buffers[1].shader_height=456;RebindSampledTextures();repeated();
    selected_fb[0]=-1;selected[0]=UINT32_MAX;InvalidateNativeState();repeated();
    for(int slot=0;slot<6;++slot)SelectTexture(slot,1);
    DeleteTexture(1);repeated();
    // Resource-vector growth can make a previously unbound default ID available.
    selected[0]=uint32_t(textures.size());InvalidateNativeState();repeated();NewTexture();repeated();
    shader_frame_number=2;InvalidateNativeState();repeated();
    unsigned nan=0x7fc01234;std::memcpy(&noise_scale,&nan,4);InvalidateNativeState();repeated();
    noise_scale=-0.0f;InvalidateNativeState();repeated();
    for(unsigned i=0;i<3000;++i) {
        unsigned id=i%textures.size(); textures[id].shader_width=float(i%4096);textures[id].shader_height=float((i*17)%4096);
        RebindSampledTextures();SelectTexture(i%6,id);Sampler(i%6,(i&1)!=0,i%4,(i/4)%4);
        Filter(FilteringMode(i%3));LoadShader(&shaders[i%3]);repeated();
    }
    ShaderProgram exhaustive = {};
    current_shader = &exhaustive;
    for (unsigned pattern = 0; pattern < 729; ++pattern) {
        unsigned value = pattern;
        for (unsigned slot = 0; slot < 6; ++slot) {
            unsigned kind = value % 3; value /= 3;
            selected_fb[slot] = kind == 1 ? int(slot % 2) : -1;
            selected[slot] = kind == 0 ? slot % textures.size() : UINT32_MAX;
        }
        for (unsigned mask = 0; mask < 4; ++mask) {
            exhaustive.features.usedTextures[0] = (mask & 1) != 0;
            exhaustive.features.usedTextures[1] = (mask & 2) != 0;
            for (unsigned filter = 0; filter < 3; ++filter) {
                filtering = FilteringMode(filter);
                InvalidateNativeState();repeated();
            }
        }
    }
    std::puts("PASS original draw preparation differential: stable reuse and 8748 texture/framebuffer/unbound, shader-mask and filter combinations; exact sampler/constant order");
}
