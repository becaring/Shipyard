"""Avoid transient texture metadata ownership and empty mask-table lookups."""
import re


def metadata_reference(text):
    pattern = r'(GfxDpSetTextureImage\([^;{}]*?)\bRawTexMetadata rawTexMetdata'
    result, count = re.subn(pattern, r'\1const RawTexMetadata& rawTexMetdata', text)
    if count != 1:
        raise ValueError('Texture metadata parameter inventory changed')
    return result


MASK_LOOKUP = '''    const std::string& texPath =
        mRdp->texture_to_load.raw_tex_metadata.resource != nullptr
            ? GetBaseTexturePath(mRdp->texture_to_load.raw_tex_metadata.resource->GetInitData()->Path)
            : "";
    auto maskedTextureIter = mMaskedTextures.find(texPath);
    if (maskedTextureIter != mMaskedTextures.end()) {
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].masked = true;
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].blended =
            maskedTextureIter->second.replacementData != nullptr;
    } else {
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].masked = false;
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].blended = false;
    }'''


def empty_mask_lookup(text, *, lookup=MASK_LOOKUP):
    # Test emptiness each time: dynamic mask registration/removal remains visible.
    replacement = '''    if (mMaskedTextures.empty()) {
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].masked = false;
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].blended = false;
    } else {
''' + '\n'.join('    ' + line for line in lookup.splitlines()) + '\n    }'
    for name in ('GfxDpLoadBlock', 'GfxDpLoadTile'):
        signature = 'void Interpreter::' + name + '('
        if text.count(signature) != 1:
            raise ValueError('Texture load method inventory changed: ' + name)
        start = text.index(signature)
        end = text.index('\n}', start) + 2
        method = text[start:end]
        if method.count(lookup) != 1:
            raise ValueError('Texture mask lookup boundary changed: ' + name)
        text = text[:start] + method.replace(lookup, replacement) + text[end:]
    return text


TILE_IMPORT_CERTIFICATE = """// Equal tile fields alone do not prove imports redundant: palette loads, TMEM
// image changes (including unused shader slots) and native invalidation also
// affect their result. The current pipeline key covers all of them.
template<class Owner> static __declspec(noinline) bool XboxCanRetainTextureImports(const Owner& owner) {
    const XboxTrianglePipelineKey& key = owner.mXboxPipelineKey;
    const RDP& r = *owner.mRdp;
    return key.Matches(owner) &&
        key.palettes[0] == XboxTrianglePipelineTraits::PaletteIdentity(owner, 0) &&
        key.palettes[1] == XboxTrianglePipelineTraits::PaletteIdentity(owner, 1) &&
        XboxTriangleBytesEqual(key.images[0], &r.loaded_texture[0], sizeof(key.images[0])) &&
        XboxTriangleBytesEqual(key.images[1], &r.loaded_texture[1], sizeof(key.images[1]));
}

"""


def exact_tile_updates(text, *, enabled_expr, capture_expr=None, observe_expr=None, retain_expr=None):
    """Keep a tile clean when a setter writes exactly its current field values.

    The caller owns the trial switch, the optional profile observer
    (capture_expr with observe_expr) and an optional extra proof that the
    current imports stay valid (retain_expr). Both setters retain every
    assignment; only a newly asserted dirty flag can be skipped. A prior dirty
    flag is never cleared by this projection.
    """
    if not enabled_expr or bool(capture_expr) != bool(observe_expr):
        raise ValueError('Tile trial expressions must be explicit')

    methods = (
        ('GfxDpSetTile', """
            previous.palette == static_cast<uint8_t>(palette) &&
            previous.fmt == static_cast<uint8_t>(fmt) &&
            previous.siz == static_cast<uint8_t>(siz) &&
            previous.cms == static_cast<uint8_t>(cms) &&
            previous.cmt == static_cast<uint8_t>(cmt) &&
            previous.shifts == static_cast<uint8_t>(shifts) &&
            previous.shiftt == static_cast<uint8_t>(shiftt) &&%(masks)s
            previous.line_size_bytes == line * 8 &&
            previous.tmem == static_cast<uint16_t>(tmem) &&
            previous.tmem_index == static_cast<uint8_t>(tmem != 0)""",
         '    mRdp->texture_tile[tile].palette = palette;', 0),
        ('GfxDpSetTileSize', """
            previous.uls == static_cast<float>(uls) &&
            previous.ult == static_cast<float>(ult) &&
            previous.lrs == static_cast<float>(lrs) &&
            previous.lrt == static_cast<float>(lrt)""",
         '    mRdp->texture_tile[tile].uls = uls;', 1),
    )
    # Forks that also assign the wrap masks compare them in their stored type.
    masks = """
            previous.masks == static_cast<decltype(previous.masks)>(masks) &&
            previous.maskt == static_cast<decltype(previous.maskt)>(maskt) &&"""
    dirty = '    mRdp->textures_changed[0] = true;\n    mRdp->textures_changed[1] = true;'
    for name, fields, anchor, kind in methods:
        signature = 'void Interpreter::' + name + '('
        if text.count(signature) != 1:
            raise ValueError('Tile setter inventory changed: ' + name)
        begin = text.index(signature)
        end = text.index('\n}', begin) + 2
        body = text[begin:end]
        if body.count(anchor) != 1 or body.count(dirty) != 1 or 'xboxSameTileState' in body:
            raise ValueError('Tile setter projection boundary changed: ' + name)
        assigned = [field for field in ('masks', 'maskt') if '].' + field + ' = ' in body]
        if kind == 0 and assigned not in ([], ['masks', 'maskt']):
            raise ValueError('Tile setter mask fields changed')
        fields = fields.replace('%(masks)s', masks if assigned else '')
        if capture_expr:
            guard = """    const bool xboxTileTrial = %(enabled)s;
    const bool xboxTileCapture = %(capture)s;
    bool xboxSameTileState = false;
    if (xboxTileTrial || xboxTileCapture) {
        const bool xboxCouldAvoidDirty =
            !mRdp->textures_changed[0] || !mRdp->textures_changed[1];
        if (xboxCouldAvoidDirty || xboxTileCapture) {
            const auto& previous = mRdp->texture_tile[tile];
            xboxSameTileState =%(fields)s;
            if (xboxTileCapture)
                %(observe)s(%(kind)d, xboxSameTileState,
                            xboxSameTileState && xboxCouldAvoidDirty);
        }
    }
"""
        else:
            guard = """    const bool xboxTileTrial = %(enabled)s;
    bool xboxSameTileState = false;
    if (xboxTileTrial && (!mRdp->textures_changed[0] || !mRdp->textures_changed[1])) {
        const auto& previous = mRdp->texture_tile[tile];
        xboxSameTileState =%(fields)s;
    }
"""
        guard = guard % dict(enabled=enabled_expr, capture=capture_expr, fields=fields,
                             observe=observe_expr, kind=kind)
        retain = ' || !' + retain_expr if retain_expr else ''
        body = body.replace(anchor, guard + anchor)
        body = body.replace(dirty, """    if (!xboxTileTrial || !xboxSameTileState%s) {
        mRdp->textures_changed[0] = true;
        mRdp->textures_changed[1] = true;
    }""" % retain)
        text = text[:begin] + body + text[end:]
    return text


def retained_tile_updates(text):
    """exact_tile_updates, always on, behind TILE_IMPORT_CERTIFICATE (needs mXboxPipelineKey)."""
    signature = 'void Interpreter::GfxDpSetTile('
    if text.count(signature) != 1:
        raise ValueError('Tile setter inventory changed: GfxDpSetTile')
    text = text.replace(signature, TILE_IMPORT_CERTIFICATE + signature)
    return exact_tile_updates(text, enabled_expr='true', retain_expr='XboxCanRetainTextureImports(*this)')


TEXEL_BLOCK = re.compile(
    r'^( +)uint8_t a = col16 & 1;\n'
    r'\1uint8_t r = col16 >> 11;\n'
    r'\1uint8_t g = \(col16 >> 6\) & 0x1f;\n'
    r'\1uint8_t b = \(col16 >> 1\) & 0x1f;\n'
    r'\1mTexUploadBuffer\[4 \* i \+ 0\] = SCALE_5_8\(r\);\n'
    r'\1mTexUploadBuffer\[4 \* i \+ 1\] = SCALE_5_8\(g\);\n'
    r'\1mTexUploadBuffer\[4 \* i \+ 2\] = SCALE_5_8\(b\);\n'
    r'\1mTexUploadBuffer\[4 \* i \+ 3\] = a \? 255 : 0;\n', re.M)


def texel_decode(text, *, expected):
    """Expand each RGBA5551 importer texel with one table lookup per channel and one word store.

    Output bytes are unchanged (fast3d/texel_decode.h); `expected` counts the
    importers (RGBA16, CI4, CI8) still decoding through SCALE_5_8.
    """
    text, count = TEXEL_BLOCK.subn(
        r'\1Shipyard::Fast3D::StoreRgba(&mTexUploadBuffer[4 * i], Shipyard::Fast3D::Rgba5551(col16));\n', text)
    if count != expected:
        raise ValueError('RGBA5551 importer inventory changed')
    return '#include "fast3d/texel_decode.h"\n' + text
