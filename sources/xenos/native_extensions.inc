// Included inside Fast after the imported native implementation. These are
// general API operations, independent of game scenes or display-list policy.
namespace {
bool ValidFramebufferSlot(int id) {
    return id>=0 && (size_t)id<buffers.size() &&
        std::find(free_framebuffers.begin(),free_framebuffers.end(),id)==free_framebuffers.end();
}
void DestroyFramebufferNative(int id) {
    if(!ValidFramebufferSlot(id))return;
    const bool resume=live_framebuffer;
    FinishFramebuffer();
    for(int slot=0;slot<6;++slot)if(selected_fb[slot]==id) {
        selected_fb[slot]=-1;selected[slot]=UINT32_MAX;SetNativeTexture(slot,NULL);
    }
    smash_device->SetRenderTarget(0,NULL);smash_device->SetDepthStencilSurface(NULL);
    { Smash360PerfScope idleScope(SMASH_PERF_GPU_IDLE); smash_device->BlockUntilIdle(); }
    Smash360_Check(restore_state->Capture(),"release destroyed framebuffer references");
    ReleaseFramebuffer(buffers[id]);buffers[id]=Framebuffer();free_framebuffers.push_back(id);
    if(active_fb==id) {
        active_fb=-1;
        for(size_t candidate=0;candidate<buffers.size();++candidate)
            if(ValidFramebufferSlot((int)candidate) && buffers[candidate].color){active_fb=(int)candidate;break;}
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
    smash_device->GetViewport(&previous);smash_device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);
    D3DVIEWPORT9 full={0,0,b.width,b.height,0,1};smash_device->SetViewport(&full);
    smash_device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    Smash360_Check(smash_device->Clear(1,&rect,D3DCLEAR_TARGET,0xff000000,1,0),"framebuffer color region clear");
    smash_device->SetViewport(&previous);smash_device->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);
    buffers[active_fb].dirty=true;
}
void CopyRegion(int dst,int src,int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1) {
    ValidateFramebuffer(src);ValidateFramebuffer(dst);
    if(sx1<=sx0 || sy1<=sy0 || dx1<=dx0 || dy1<=dy0)return;
    Framebuffer& source=buffers[src];Framebuffer& target=buffers[dst];
    if(sx0<0 || sy0<0 || sx1>(int)source.width || sy1>(int)source.height ||
       dx0<0 || dy0<0 || dx1>(int)target.width || dy1>(int)target.height)
        Smash360_Fatal("framebuffer copy rectangle",E_INVALIDARG);
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
    if(width>4096 || height>4096)Smash360_Fatal("framebuffer readback dimensions",E_INVALIDARG);
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
            const unsigned r=((((p>>16)&255)+4)*31)/255;
            const unsigned g=((((p>>8)&255)+4)*31)/255;
            const unsigned blue=(((p&255)+4)*31)/255;
            rgba16[(size_t)y*width+x]=(uint16_t)((r<<11)|(g<<6)|(blue<<1)|((p>>24)?1:0));
        }
    active_fb=previous;if(resume)BeginFramebuffer();
}
}

void InitializeNativeDisplay(){InitializeDisplay();}
HRESULT PresentNativeStartupUI(){
    // Only the OS overlay is drawn here. Fast3D and the real game initialize
    // after writable configuration storage is available, using this device.
    if(!smash_device || !buffers.empty())return E_UNEXPECTED;
    HRESULT result=smash_device->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff000000,1,0);
    if(FAILED(result))return result;
    return smash_device->Present(NULL,NULL,NULL,NULL);
}
void ShutdownNativeDisplay(){Smash360_ShutdownGraphics();}
GfxRenderingAPIXenos::~GfxRenderingAPIXenos(){Smash360_ShutdownGraphics();}
bool ReadPresentedFrameRGBA(std::vector<uint8_t>& rgba,uint32_t& width,uint32_t& height) {
    // BattleShip calls its screenshot hook after presentation. Buffer zero is
    // the real resolved presentation image, including the current native GUI.
    if (!smash_device || !ValidFramebufferSlot(0) || live_framebuffer) return false;
    const Framebuffer& frame=buffers[0];
    if (!frame.stored || !frame.image || !frame.width || !frame.height) return false;
    width=frame.width; height=frame.height;
    std::vector<DWORD> pixels((size_t)width*height);
    rgba.resize((size_t)width*height*4);
    // Same tiled-texture readback as SoH360 ReadFramebufferPixels. This optional
    // capture API returns failure on lock errors instead of making them fatal.
    D3DLOCKED_RECT locked;
    if (FAILED(frame.image->LockRect(0,&locked,NULL,D3DLOCK_READONLY))) return false;
    XGTEXTURE_DESC desc; XGGetTextureDesc(frame.image,0,&desc);
    XGUntileTextureLevel(width,height,0,XGGetGpuFormat(desc.Format),0,&pixels[0],width*4,NULL,locked.pBits,NULL);
    HRESULT result=frame.image->UnlockRect(0);
    if (FAILED(result)) return false;
    for (size_t i=0;i<pixels.size();++i) {
        const DWORD color=pixels[i];
        rgba[i*4]=(uint8_t)(color>>16); rgba[i*4+1]=(uint8_t)(color>>8);
        rgba[i*4+2]=(uint8_t)color; rgba[i*4+3]=(uint8_t)(color>>24);
    }
    return true;
}
const char* GfxRenderingAPIXenos::GetName(){return "Xenos";}
int GfxRenderingAPIXenos::GetMaxTextureSize(){return 4096;}
GfxClipParameters GfxRenderingAPIXenos::GetClipParameters(){return ClipParameters();}
void GfxRenderingAPIXenos::UnloadShader(ShaderProgram* p){::Fast::UnloadShader(p);}
void GfxRenderingAPIXenos::LoadShader(ShaderProgram* p){::Fast::LoadShader(p);}
ShaderProgramKey GfxRenderingAPIXenos::MakeShaderProgramKey(uint64_t a,uint64_t b) const {
    ShaderProgramKey key;key.shaderId0=a;key.shaderId1=b;key.filteringMode=filtering;key.srgbMode=mSrgbMode;return key;
}
ShaderProgram* GfxRenderingAPIXenos::CreateAndLoadNewShader(const ShaderProgramKey& key){return CreateShader(key);}
ShaderProgram* GfxRenderingAPIXenos::LookupShader(const ShaderProgramKey& key){return Lookup(key);}
void GfxRenderingAPIXenos::ShaderGetInfo(ShaderProgram* p,uint8_t* n,bool used[2]){ShaderInfo(p,n,used);}
uint32_t GfxRenderingAPIXenos::NewTexture(){return ::Fast::NewTexture();}
void GfxRenderingAPIXenos::SelectTexture(int slot,uint32_t id){::Fast::SelectTexture(slot,id);}
void GfxRenderingAPIXenos::UploadTexture(const uint8_t* p,uint32_t w,uint32_t h){Upload(p,w,h);}
void GfxRenderingAPIXenos::SetSamplerParameters(int slot,bool linear,uint32_t s,uint32_t t){Sampler(slot,linear,s,t);}
void GfxRenderingAPIXenos::SetDepthTestAndMask(bool test,bool write){Depth(test,write);}
void GfxRenderingAPIXenos::SetZmodeDecal(bool decal){Decal(decal);}
void GfxRenderingAPIXenos::SetViewport(int x,int y,int w,int h){Viewport(x,y,w,h);}
void GfxRenderingAPIXenos::SetScissor(int x,int y,int w,int h){Scissor(x,y,w,h);}
void GfxRenderingAPIXenos::SetUseAlpha(bool alpha){Alpha(alpha);}
void GfxRenderingAPIXenos::DrawTriangles(float v[],size_t count,size_t tris){Draw(v,count,tris);}
void GfxRenderingAPIXenos::Init(){::Fast::Init();Smash360_InitializeGuiForXenos();}
void GfxRenderingAPIXenos::OnResize(){Resize();}
void BeginNativeGuiDraw(){BeginFramebuffer();}
void InvalidateNativeGuiState(){InvalidateNativeState();}
void CaptureNativePerf(Smash360NativePerf* n) {
    memset(n,0,sizeof(*n));
    n->samplerAttempted=native_state_counts.sampler_attempted;
    n->samplerWritten=native_state_counts.sampler_written;
    n->samplerSuppressed=native_state_counts.sampler_suppressed;
    n->constantsAttempted=native_state_counts.constants_attempted;
    n->constantsWritten=native_state_counts.constants_written;
    n->constantsSuppressed=native_state_counts.constants_suppressed;
    n->bindingAttempted=binding_attempted;n->bindingWritten=binding_written;n->bindingSuppressed=binding_suppressed;
    n->uploadRequests=upload_counts.requests;n->uploadReused=upload_counts.reused;n->uploadSavedBytes=upload_counts.bytes_saved;
    n->clears=framebuffer_clear_count;n->restoresAvoided=framebuffer_restore_avoided;n->depthRestoresAvoided=framebuffer_depth_restore_avoided;
    n->pendingTextures=(unsigned)pending_texture_releases.size();n->pendingBytes=(unsigned)pending_texture_bytes;
    // Report the published game target, even though GUI presentation currently
    // owns framebuffer zero. Resolution/MSAA experiments need the real target.
    for(size_t i=0;i<buffers.size();++i) if(published_framebuffer_image && buffers[i].image==published_framebuffer_image) {
        n->width=buffers[i].width;n->height=buffers[i].height;n->samples=buffers[i].samples;n->tiles=(unsigned)buffers[i].tiles.size();break;
    }
    memset(&native_state_counts,0,sizeof(native_state_counts));
    memset(&upload_counts,0,sizeof(upload_counts));
    binding_attempted=binding_written=binding_suppressed=binding_verified=0;
    framebuffer_clear_count=framebuffer_restore_avoided=framebuffer_depth_restore_avoided=0;
}
void ReportNativeBindingConstants() {
    Smash360_Log("RENDER binding_constants attempted=%u written=%u suppressed=%u verified=%u",
        binding_attempted,binding_written,binding_suppressed,binding_verified);
    binding_attempted=binding_written=binding_suppressed=binding_verified=0;
    Smash360_Log("RENDER clears=%u restore_draws_avoided=%u depth_sample_restores_avoided=%u",
        framebuffer_clear_count,framebuffer_restore_avoided,framebuffer_depth_restore_avoided);
    framebuffer_clear_count=framebuffer_restore_avoided=framebuffer_depth_restore_avoided=0;
}
void GfxRenderingAPIXenos::StartFrame(){Start();}
void GfxRenderingAPIXenos::EndFrame(){End();}
void GfxRenderingAPIXenos::FinishRender(){Finish();}
int GfxRenderingAPIXenos::CreateFramebuffer(){return NewFramebuffer();}
void GfxRenderingAPIXenos::DestroyFramebuffer(int id){DestroyFramebufferNative(id);}
void GfxRenderingAPIXenos::UpdateFramebufferParameters(int id,uint32_t w,uint32_t h,uint32_t msaa,bool flip,bool target,bool depth,bool read){
    UpdateFramebuffer(id,w,h,msaa,flip,target,depth,read);
}
void GfxRenderingAPIXenos::StartDrawToFramebuffer(int id,float noise){BindFramebuffer(id,noise);}
void GfxRenderingAPIXenos::CopyFramebuffer(int dst,int src,int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1){CopyRegion(dst,src,sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1);}
void GfxRenderingAPIXenos::ClearFramebuffer(bool color,bool depth){Clear(color,depth);}
void GfxRenderingAPIXenos::SetColorWriteMask(bool enabled){color_writes=enabled;native_state.alpha_valid=false;ApplyAlpha();}
void GfxRenderingAPIXenos::ClearColorRegion(float x0,float y0,float x1,float y1){ClearRegion(x0,y0,x1,y1);}
void GfxRenderingAPIXenos::ReadFramebufferToCPU(int id,uint32_t w,uint32_t h,uint16_t* p){ReadColor(id,w,h,p);}
void GfxRenderingAPIXenos::ResolveMSAAColorBuffer(int dst,int src){ResolveMSAA(dst,src);}
std::unordered_map<std::pair<float,float>,uint16_t,hash_pair_ff> GfxRenderingAPIXenos::GetPixelDepth(int id,const std::set<std::pair<float,float> >& xy){return PixelDepth(id,xy);}
void* GfxRenderingAPIXenos::GetFramebufferTextureId(int id){return FramebufferImage(id);}
void GfxRenderingAPIXenos::SelectTextureFb(int id){SelectFramebuffer(id);}
bool GfxRenderingAPIXenos::FbNeedsSampleVFlip(int id){ValidateFramebuffer(id);return false;}
void GfxRenderingAPIXenos::DeleteTexture(uint32_t id){::Fast::DeleteTexture(id);}
void GfxRenderingAPIXenos::SetTextureFilter(FilteringMode mode){Filter(mode);}
FilteringMode GfxRenderingAPIXenos::GetTextureFilter(){return GetFilter();}
void GfxRenderingAPIXenos::SetSrgbMode(){mSrgbMode=true;}
ImTextureID GfxRenderingAPIXenos::GetTextureById(int id){
    if(id<0 || (size_t)id>=textures.size())Smash360_Fatal("GUI texture index",E_INVALIDARG);
    return textures[id].native;
}
