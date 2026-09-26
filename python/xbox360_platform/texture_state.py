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


def empty_mask_lookup(text):
    # Test emptiness each time: dynamic mask registration/removal remains visible.
    replacement = '''    if (mMaskedTextures.empty()) {
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].masked = false;
        mRdp->loaded_texture[mRdp->texture_tile[tile].tmem_index].blended = false;
    } else {
''' + '\n'.join('    ' + line for line in MASK_LOOKUP.splitlines()) + '\n    }'
    for name in ('GfxDpLoadBlock', 'GfxDpLoadTile'):
        signature = 'void Interpreter::' + name + '('
        if text.count(signature) != 1:
            raise ValueError('Texture load method inventory changed: ' + name)
        start = text.index(signature)
        end = text.index('\n}', start) + 2
        method = text[start:end]
        if method.count(MASK_LOOKUP) != 1:
            raise ValueError('Texture mask lookup boundary changed: ' + name)
        text = text[:start] + method.replace(MASK_LOOKUP, replacement) + text[end:]
    return text
