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


def exact_tile_updates(text, *, enabled_expr, capture_expr, observe_expr):
    """Keep a tile clean when a setter writes exactly its current field values.

    The caller owns the trial switch and optional profile observer. Both
    setters retain every assignment; only a newly asserted dirty flag can be
    skipped. A prior dirty flag is never cleared by this projection.
    """
    if not all((enabled_expr, capture_expr, observe_expr)):
        raise ValueError('Tile trial expressions must be explicit')

    methods = (
        ('GfxDpSetTile', '''
            previous.palette == static_cast<uint8_t>(palette) &&
            previous.fmt == static_cast<uint8_t>(fmt) &&
            previous.siz == static_cast<uint8_t>(siz) &&
            previous.cms == static_cast<uint8_t>(cms) &&
            previous.cmt == static_cast<uint8_t>(cmt) &&
            previous.shifts == static_cast<uint8_t>(shifts) &&
            previous.shiftt == static_cast<uint8_t>(shiftt) &&
            previous.line_size_bytes == line * 8 &&
            previous.tmem == static_cast<uint16_t>(tmem) &&
            previous.tmem_index == static_cast<uint8_t>(tmem != 0)''',
         '    mRdp->texture_tile[tile].palette = palette;', 0),
        ('GfxDpSetTileSize', '''
            previous.uls == static_cast<float>(uls) &&
            previous.ult == static_cast<float>(ult) &&
            previous.lrs == static_cast<float>(lrs) &&
            previous.lrt == static_cast<float>(lrt)''',
         '    mRdp->texture_tile[tile].uls = uls;', 1),
    )
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
        guard = '''    const bool xboxTileTrial = %(enabled)s;
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
''' % dict(enabled=enabled_expr, capture=capture_expr, fields=fields,
           observe=observe_expr, kind=kind)
        body = body.replace(anchor, guard + anchor)
        body = body.replace(dirty, '''    if (!xboxTileTrial || !xboxSameTileState) {
        mRdp->textures_changed[0] = true;
        mRdp->textures_changed[1] = true;
    }''')
        text = text[:begin] + body + text[end:]
    return text
