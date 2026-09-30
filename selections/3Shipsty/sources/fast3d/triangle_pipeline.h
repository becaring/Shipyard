#pragma once
// Included inside namespace Fast after the authoritative RDP/RenderingState
// types. No resource ownership or game state is duplicated here.
// SHIPYARD BYTE EQUALITY BEGIN
__forceinline bool XboxTriangleBytesEqual(const void* left, const void* right, size_t size) {
    const unsigned char* a = static_cast<const unsigned char*>(left);
    const unsigned char* b = static_cast<const unsigned char*>(right);
    // XDK lowers small memcpy calls to word loads. Keep arbitrary callers safe.
    if ((reinterpret_cast<uintptr_t>(a) | reinterpret_cast<uintptr_t>(b)) & 3)
        return std::memcmp(a, b, size) == 0;
    while (size >= sizeof(uint32_t)) {
        uint32_t x, y;
        std::memcpy(&x, a, sizeof(x));
        std::memcpy(&y, b, sizeof(y));
        if (x != y)
            return false;
        a += sizeof(x);
        b += sizeof(y);
        size -= sizeof(x);
    }
    while (size--)
        if (*a++ != *b++)
            return false;
    return true;
}
// SHIPYARD BYTE EQUALITY END

struct XboxTrianglePipelineResult {
    ColorCombiner* comb;
    uint64_t ccId;
    uint32_t tm;
    uint32_t width[2];
    uint32_t height[2];
    uint32_t boundWidth[2];
    uint32_t boundHeight[2];
    uint32_t effectiveTile[2];
    float widthFloat[2];
    float heightFloat[2];
    float boundWidthFloat[2];
    float boundHeightFloat[2];
    bool alpha;
    bool fog;
    bool grayscale;
    bool blendColor;
    bool redirect;
    uint8_t numInputs;
    bool usedTextures[2];
    GfxClipParameters clip;
};

struct XboxTrianglePipelineKey {
    bool valid;
    bool grayscale;
    bool redirect;
    bool shaderPresent;
    bool masked[2];
    bool blended[2];
    bool used[2];
    uint64_t combine;
    uint32_t modeL;
    uint32_t modeH;
    uint32_t vertexZ;
    int64_t shaderTop;
    uint8_t firstTile;
    const void* backend;
    GfxClipParameters clip;
    // Alignment-only union members are never accessed; captured bytes retain
    // their complete object representations, including padding and float bits.
    union {
        uint32_t renderingAlignment;
        unsigned char rendering[sizeof(RenderingState)];
    };
    union {
        uint32_t tileAlignment;
        unsigned char tiles[2][sizeof(((RDP*)0)->texture_tile[0])];
    };
    uint32_t loaded[2][4];
    uint8_t sampler[2][3];
    // Reload restoration (consumer trait RestoresReloadedTextures): both TMEM
    // entries and the consumer's palette identity (PaletteIdentity) at
    // capture. Imports may fall back to the other TMEM entry, so both are kept.
    // A texture command sequence that reloads exactly this material leaves
    // every import key, and so the bound cache node, unchanged; the SoH 4.0.5
    // Xbox renderer restored its pipeline instead of rebuilding it.
    union {
        uint32_t imageAlignment;
        unsigned char images[2][sizeof(((RDP*)0)->loaded_texture[0])];
    };
    const void* palettes[2];
    XboxTrianglePipelineKey() : valid(false) {
    }

    template <class Owner> __forceinline bool Matches(const Owner& owner) const {
        const RDP& r = *owner.mRdp;
        const GfxClipParameters currentClip = XboxTrianglePipelineTraits::Clip(owner);
        if (!valid || r.viewport_or_scissor_changed || backend != owner.mRapi ||
            combine != r.combine_mode || modeL != r.other_mode_l || modeH != r.other_mode_h ||
            grayscale != r.grayscale || firstTile != r.first_tile_index ||
            vertexZ != (owner.mRsp->geometry_mode & G_ZBUFFER) ||
            redirect != XboxTrianglePipelineTraits::Redirect(owner) ||
            clip.z_is_from_0_to_1 != currentClip.z_is_from_0_to_1 ||
            clip.invertY != currentClip.invertY ||
            shaderPresent != XboxTrianglePipelineTraits::ShaderPresent(owner) ||
            (shaderPresent && shaderTop != XboxTrianglePipelineTraits::ShaderIdentity(owner)) ||
            !XboxTriangleBytesEqual(rendering, &owner.mRenderingState, sizeof(rendering)))
            return false;
        bool reloaded = false;
        for (unsigned i = 0; i < 2; ++i) {
            if (masked[i] != r.loaded_texture[i].masked ||
                blended[i] != r.loaded_texture[i].blended)
                return false;
            if (!used[i])
                continue;
            if (r.textures_changed[i] && !XboxTrianglePipelineTraits::RestoresReloadedTextures(owner))
                return false;
            const unsigned tile = XboxTrianglePipelineTraits::EffectiveTile(owner, i);
            if (tile >= 8 || r.texture_tile[tile].tmem_index >= 2 ||
                !XboxTriangleBytesEqual(tiles[i], &r.texture_tile[tile], sizeof(tiles[i])))
                return false;
            const unsigned tmem = r.texture_tile[tile].tmem_index;
            reloaded = reloaded || r.textures_changed[i];
            if (loaded[i][0] != r.loaded_texture[tmem].size_bytes ||
                loaded[i][1] != r.loaded_texture[tmem].line_size_bytes ||
                loaded[i][2] != r.loaded_texture[tmem].full_image_line_size_bytes ||
                loaded[i][3] != r.loaded_texture[tmem].orig_size_bytes)
                return false;
            const TextureCacheNode* node = owner.mRenderingState.mTextures[i];
            if (node && (sampler[i][0] != node->second.linear_filter ||
                         sampler[i][1] != node->second.cms || sampler[i][2] != node->second.cmt))
                return false;
        }
        // A pending import is skipped only when the reload restored the
        // captured material byte for byte (Certify then clears the flag).
        if (reloaded &&
            (palettes[0] != XboxTrianglePipelineTraits::PaletteIdentity(owner, 0) ||
             palettes[1] != XboxTrianglePipelineTraits::PaletteIdentity(owner, 1) ||
             !XboxTriangleBytesEqual(images[0], &r.loaded_texture[0], sizeof(images[0])) ||
             !XboxTriangleBytesEqual(images[1], &r.loaded_texture[1], sizeof(images[1]))))
            return false;
        return true;
    }

    // Consumers certify through this, not Matches. A hit may restore a
    // byte-identical reload of a used slot; upstream's import would have found
    // the bound node and cleared that slot's dirty flag, so clear it here too.
    // Unused slots keep theirs, exactly as upstream reconciliation does.
    template <class Owner> __forceinline bool Certify(Owner& owner) const {
        if (!Matches(owner))
            return false;
        for (unsigned i = 0; i < 2; ++i)
            if (used[i])
                owner.mRdp->textures_changed[i] = false;
        return true;
    }

    template <class Owner>
    void Capture(const Owner& owner, const XboxTrianglePipelineResult& result) {
        const RDP& r = *owner.mRdp;
        valid = false;
        // Never skip a pending import for a used slot. Unused dirty flags stay
        // untouched, and become mandatory misses if combiner usage changes.
        if (r.viewport_or_scissor_changed)
            return;
        backend = owner.mRapi;
        combine = r.combine_mode;
        modeL = r.other_mode_l;
        modeH = r.other_mode_h;
        grayscale = r.grayscale;
        firstTile = r.first_tile_index;
        vertexZ = owner.mRsp->geometry_mode & G_ZBUFFER;
        redirect = XboxTrianglePipelineTraits::Redirect(owner);
        clip = XboxTrianglePipelineTraits::Clip(owner);
        shaderPresent = XboxTrianglePipelineTraits::ShaderPresent(owner);
        shaderTop = shaderPresent ? XboxTrianglePipelineTraits::ShaderIdentity(owner) : 0;
        std::memcpy(rendering, &owner.mRenderingState, sizeof(rendering));
        palettes[0] = XboxTrianglePipelineTraits::PaletteIdentity(owner, 0);
        palettes[1] = XboxTrianglePipelineTraits::PaletteIdentity(owner, 1);
        std::memcpy(images[0], &r.loaded_texture[0], sizeof(images[0]));
        std::memcpy(images[1], &r.loaded_texture[1], sizeof(images[1]));
        for (unsigned i = 0; i < 2; ++i) {
            masked[i] = r.loaded_texture[i].masked;
            blended[i] = r.loaded_texture[i].blended;
            used[i] = result.comb->usedTextures[i];
            if (!used[i])
                continue;
            if (r.textures_changed[i])
                return;
            const unsigned tile = XboxTrianglePipelineTraits::EffectiveTile(owner, i);
            if (tile >= 8 || r.texture_tile[tile].tmem_index >= 2)
                return;
            // Nonfinite bounds retain their original arithmetic/exception path.
            uint32_t bounds[4];
            std::memcpy(bounds, &r.texture_tile[tile].uls, sizeof(bounds));
            for (unsigned j = 0; j < 4; ++j)
                if ((bounds[j] & 0x7f800000u) == 0x7f800000u)
                    return;
            std::memcpy(tiles[i], &r.texture_tile[tile], sizeof(tiles[i]));
            const unsigned tmem = r.texture_tile[tile].tmem_index;
            loaded[i][0] = r.loaded_texture[tmem].size_bytes;
            loaded[i][1] = r.loaded_texture[tmem].line_size_bytes;
            loaded[i][2] = r.loaded_texture[tmem].full_image_line_size_bytes;
            loaded[i][3] = r.loaded_texture[tmem].orig_size_bytes;
            const TextureCacheNode* node = owner.mRenderingState.mTextures[i];
            sampler[i][0] = node ? node->second.linear_filter : 0;
            sampler[i][1] = node ? node->second.cms : 0;
            sampler[i][2] = node ? node->second.cmt : 0;
        }
        valid = true;
    }
};
