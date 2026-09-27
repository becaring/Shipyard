#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
#include <cstdlib>
typedef unsigned long DWORD;
typedef long LONG;
enum { TRUE=1, FALSE=0, E_INVALIDARG=-1, D3DRS_MULTISAMPLEANTIALIAS=1,
       D3DRS_MULTISAMPLEMASK=2, D3DRS_SCISSORTESTENABLE=3,
       D3DCLEAR_TARGET=1, D3DCLEAR_ZBUFFER=2, D3DCLEAR_STENCIL=4,
       D3DRESOLVE_FRAGMENT0=0, D3DRESOLVE_FRAGMENT1=1, D3DRESOLVE_FRAGMENT2=2,
       D3DRESOLVE_FRAGMENT3=3, D3DRESOLVE_DEPTHSTENCIL=4, D3DRESOLVE_RENDERTARGET0=8,
       D3DRESOLVE_ALLFRAGMENTS=16, D3DRESOLVE_CLEARRENDERTARGET=32, D3DRESOLVE_CLEARDEPTHSTENCIL=64 };
#define D3DPRED_TILE(x) (x)
struct D3DVIEWPORT9 { DWORD X,Y,Width,Height; float MinZ,MaxZ; };
struct RECT { LONG left,top,right,bottom; };
struct D3DRECT { LONG x1,y1,x2,y2; };
struct D3DPOINT { LONG x,y; };
struct D3DVECTOR4 { float x,y,z,w; };
struct Device {
    D3DVIEWPORT9 viewport; RECT scissor; DWORD enabled; unsigned captures=0; bool tiled=false; DWORD surfaceHeight=640;
    void ResetBounds() { viewport={0,0,1920,1088,0,1};scissor={0,0,65535,65535};enabled=TRUE; }
    int GetViewport(D3DVIEWPORT9* p){++captures;*p=viewport;return 0;}
    int SetViewport(const D3DVIEWPORT9* p){viewport=*p; scissor={0,0,65535,65535}; if(!tiled) viewport.Height=std::min(viewport.Height,surfaceHeight-std::min(viewport.Y,surfaceHeight)); return 0;}
    int GetScissorRect(RECT* p){*p=scissor;return 0;}
    int SetScissorRect(const RECT* p){scissor=*p;return 0;}
    int GetRenderState(int,DWORD* p){*p=enabled;return 0;}
    int SetRenderState(int state,DWORD value){if(state==D3DRS_SCISSORTESTENABLE)enabled=value;return 0;}
    int SetRenderTarget(int,void*){ResetBounds();return 0;}
    int SetDepthStencilSurface(void*){return 0;}
    void BeginTiling(DWORD,DWORD,const D3DRECT*,const D3DVECTOR4*,float,DWORD){tiled=true;ResetBounds();}
    int EndTiling(DWORD,void*,void*,D3DVECTOR4*,float,DWORD,void*){tiled=false;ResetBounds();return 0;}
    void SetPredication(size_t){}
    void InsertFence(){}
    int Resolve(DWORD,const D3DRECT*,void*,const D3DPOINT*,int,int,const D3DVECTOR4*,float,int,void*){return 0;}
} device;
typedef Device IDirect3DDevice9;
Device* xenos_device=&device;
void Shipyard_Check(int result,const char*){assert(result==0);}
void Shipyard_Fatal(const char*,int){assert(false);}
#include "xenos/raster_bounds.h"
Shipyard::Xenos::RasterBounds raster_bounds;
struct Framebuffer {
    void *color,*depth,*image,*color_samples[4],*depth_samples[4];
    bool stored,depth_stored; unsigned width,height,samples; std::vector<D3DRECT> tiles;
};
std::vector<Framebuffer> buffers(1);
int active_fb=0; bool live_framebuffer=false; void* presentation_color=(void*)1;
size_t tiling_draw_bytes;
unsigned framebuffer_depth_resolves,framebuffer_depth_resolves_skipped;
unsigned presentation_color_resolves_skipped,framebuffer_early_depth_fences;
struct { unsigned framebufferSetupTicks,framebufferSetupCalls,framebufferResolveTicks,framebufferResolveCalls; } timing_counts;
struct NativeTimingScope { NativeTimingScope(unsigned&,unsigned&){} };
void InvalidateNativeState(){}
void TilingSpaceBegin(){} void TilingSpaceSetup(){} void TilingSpaceBeforeResolve(){} void TilingSpaceBeforeEnd(){}
void ClearFramebufferTargets(DWORD){} void SetTileConstants(){} void StopTileConstants(){}
void RestoreQuad(void*,void*,unsigned,unsigned,DWORD){}
void RebindSampledTextures(){} void SetNativeTexture(int,void*){}
#include "transitions.inc"
void CheckBounds(const D3DVIEWPORT9& vp,const RECT& clip){
    if(memcmp(&device.viewport,&vp,sizeof(vp)) || memcmp(&device.scissor,&clip,sizeof(clip)) || device.enabled!=TRUE){
        puts("logical bounds lost across tiled/smaller target transition"); std::exit(1);
    }
}
int main(){
    for(unsigned tiles=1;tiles<=2;++tiles) for(unsigned stored=0;stored<2;++stored)
    for(unsigned hud=0;hud<2;++hud){
        Framebuffer& b=buffers[0]; b.color=presentation_color;b.depth=(void*)2;
        b.width=1920;b.height=1080;b.samples=1;b.tiles.assign(tiles,D3DRECT{0,0,1920,1080});
        b.stored=stored!=0;b.depth_stored=stored!=0;live_framebuffer=false;
        device.tiled=false;device.surfaceHeight=tiles==1?1080:640;raster_bounds.Reset();
        D3DVIEWPORT9 vp=hud?D3DVIEWPORT9{1344,113,204,202,0,1}:D3DVIEWPORT9{0,0,1920,1080,0,1};
        RECT clip=hud?RECT{1344,113,1548,315}:RECT{0,99,1920,981};
        Viewport(vp.X,1080-vp.Y-vp.Height,vp.Width,vp.Height);
        Scissor(clip.left,1080-clip.bottom,clip.right-clip.left,clip.bottom-clip.top);
        for(unsigned restart=0;restart<4;++restart){
            CheckBounds(vp,clip);
            FinishFramebuffer(true,true);
            // EndTiling exposes a small physical target; direct presentation/copy
            // state must never replace the original Fast3D request.
            D3DVIEWPORT9 temporary={0,0,320,240,0,1};device.SetViewport(&temporary);
            BeginFramebuffer();CheckBounds(vp,clip);
            unsigned captured=device.captures;BeginFramebuffer();assert(device.captures==captured);
        }
        FinishFramebuffer(true,true);
        // An internal smaller target round trip without new Fast3D setters.
        b.width=320;b.height=240;b.tiles.assign(1,D3DRECT{0,0,320,240});device.surfaceHeight=240;
        BeginFramebuffer();FinishFramebuffer(true,true);
        b.width=1920;b.height=1080;b.tiles.assign(tiles,D3DRECT{0,0,1920,1080});device.surfaceHeight=tiles==1?1080:640;
        BeginFramebuffer();CheckBounds(vp,clip);FinishFramebuffer(true,true);
    }
    raster_bounds.Reset();device.captures=0;
    raster_bounds.Apply(&device,1080);assert(device.captures==0);
    puts("logical bounds survive physical viewport clamping and temporary targets");
}
