#pragma once
// Included inside namespace Fast after the authoritative RDP/RenderingState
// types. No resource ownership or game state is duplicated here.
struct XboxTrianglePipelineResult {
    ColorCombiner* comb;
    uint64_t ccId;
    uint32_t tm, width[2], height[2], boundWidth[2], boundHeight[2], effectiveTile[2];
    float widthFloat[2], heightFloat[2], boundWidthFloat[2], boundHeightFloat[2];
    bool alpha, fog, grayscale, blendColor, redirect;
    uint8_t numInputs;
    bool usedTextures[2];
    GfxClipParameters clip;
};

struct XboxTrianglePipelineKey {
    bool valid, grayscale, redirect, shaderPresent, masked[2], blended[2], used[2];
    uint64_t combine;
    uint32_t modeL, modeH, vertexZ;
    int64_t shaderTop;
    uint8_t firstTile;
    const void* backend;
    GfxClipParameters clip;
    unsigned char rendering[sizeof(RenderingState)];
    unsigned char tiles[2][sizeof(((RDP*)0)->texture_tile[0])];
    uint32_t loaded[2][4];
    uint8_t sampler[2][3];
    XboxTrianglePipelineKey() : valid(false) {}

    template<class Owner> __forceinline bool Matches(const Owner& owner) const {
        const RDP& r=*owner.mRdp;
        const GfxClipParameters currentClip=XboxTrianglePipelineTraits::Clip(owner);
        if(!valid || r.viewport_or_scissor_changed ||
           backend!=owner.mRapi || combine!=r.combine_mode || modeL!=r.other_mode_l || modeH!=r.other_mode_h ||
           grayscale!=r.grayscale || firstTile!=r.first_tile_index ||
           vertexZ!=(owner.mRsp->geometry_mode&G_ZBUFFER) || redirect!=XboxTrianglePipelineTraits::Redirect(owner) ||
           clip.z_is_from_0_to_1!=currentClip.z_is_from_0_to_1 ||
           clip.invertY!=currentClip.invertY ||
           shaderPresent!=XboxTrianglePipelineTraits::ShaderPresent(owner) ||
           (shaderPresent && shaderTop!=XboxTrianglePipelineTraits::ShaderIdentity(owner)) ||
           std::memcmp(rendering,&owner.mRenderingState,sizeof(rendering)))return false;
        for(unsigned i=0;i<2;++i) {
            if(masked[i]!=r.loaded_texture[i].masked || blended[i]!=r.loaded_texture[i].blended)return false;
            if(!used[i])continue;
            if(r.textures_changed[i])return false;
            const unsigned tile=XboxTrianglePipelineTraits::EffectiveTile(owner,i);
            if(tile>=8 || r.texture_tile[tile].tmem_index>=2 ||
               std::memcmp(tiles[i],&r.texture_tile[tile],sizeof(tiles[i])))return false;
            const unsigned tmem=r.texture_tile[tile].tmem_index;
            if(loaded[i][0]!=r.loaded_texture[tmem].size_bytes ||
               loaded[i][1]!=r.loaded_texture[tmem].line_size_bytes ||
               loaded[i][2]!=r.loaded_texture[tmem].full_image_line_size_bytes ||
               loaded[i][3]!=r.loaded_texture[tmem].orig_size_bytes)return false;
            const TextureCacheNode* node=owner.mRenderingState.mTextures[i];
            if(node && (sampler[i][0]!=node->second.linear_filter ||
                        sampler[i][1]!=node->second.cms || sampler[i][2]!=node->second.cmt))return false;
        }
        return true;
    }

    template<class Owner> void Capture(const Owner& owner,const XboxTrianglePipelineResult& result) {
        const RDP& r=*owner.mRdp;
        valid=false;
        // Never skip a pending import for a used slot. Unused dirty flags stay
        // untouched, and become mandatory misses if combiner usage changes.
        if(r.viewport_or_scissor_changed)return;
        backend=owner.mRapi;combine=r.combine_mode;modeL=r.other_mode_l;modeH=r.other_mode_h;
        grayscale=r.grayscale;firstTile=r.first_tile_index;vertexZ=owner.mRsp->geometry_mode&G_ZBUFFER;
        redirect=XboxTrianglePipelineTraits::Redirect(owner);clip=XboxTrianglePipelineTraits::Clip(owner);
        shaderPresent=XboxTrianglePipelineTraits::ShaderPresent(owner);
        shaderTop=shaderPresent?XboxTrianglePipelineTraits::ShaderIdentity(owner):0;
        std::memcpy(rendering,&owner.mRenderingState,sizeof(rendering));
        for(unsigned i=0;i<2;++i) {
            masked[i]=r.loaded_texture[i].masked;blended[i]=r.loaded_texture[i].blended;
            used[i]=result.comb->usedTextures[i];
            if(!used[i])continue;
            if(r.textures_changed[i])return;
            const unsigned tile=XboxTrianglePipelineTraits::EffectiveTile(owner,i);
            if(tile>=8 || r.texture_tile[tile].tmem_index>=2)return;
            // Nonfinite bounds retain their original arithmetic/exception path.
            uint32_t bounds[4];
            std::memcpy(bounds,&r.texture_tile[tile].uls,sizeof(bounds));
            for(unsigned j=0;j<4;++j)if((bounds[j]&0x7f800000u)==0x7f800000u)return;
            std::memcpy(tiles[i],&r.texture_tile[tile],sizeof(tiles[i]));
            const unsigned tmem=r.texture_tile[tile].tmem_index;
            loaded[i][0]=r.loaded_texture[tmem].size_bytes;loaded[i][1]=r.loaded_texture[tmem].line_size_bytes;
            loaded[i][2]=r.loaded_texture[tmem].full_image_line_size_bytes;
            loaded[i][3]=r.loaded_texture[tmem].orig_size_bytes;
            const TextureCacheNode* node=owner.mRenderingState.mTextures[i];
            sampler[i][0]=node?node->second.linear_filter:0;
            sampler[i][1]=node?node->second.cms:0;sampler[i][2]=node?node->second.cmt:0;
        }
        valid=true;
    }
};
