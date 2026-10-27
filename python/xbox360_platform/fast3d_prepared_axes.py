"""UV axes and clamp limits prepared at reconciliation (fast3d/coordinate.h).

The pipeline key compares the effective tile and texture extents of every
combiner-used slot, so their CoordinateAxis and clamp limit stay valid until
the next reconciliation. Shader-only slots keep per-triangle preparation.
The packer only applies them; CoordinateAxis keeps Coordinate's exact bits.
"""

MEMBERS = '''    Shipyard::Fast3D::CoordinateAxis mXboxAxisS[2];
    Shipyard::Fast3D::CoordinateAxis mXboxAxisT[2];
    float mXboxClampS[2];
    float mXboxClampT[2];'''

CAPTURE = '''    // Packing derivations from the tile/extent state reconciled above; the pipeline
    // compares those tiles and extents for every combiner-used slot.
    for (unsigned i=0;i<2;++i) {
        if (!comb->usedTextures[i]) continue;
        const uint32_t uv_tile = effective_tile[i];
        mXboxAxisS[i].Prepare(mRdp->texture_tile[uv_tile].shifts, mRdp->texture_tile[uv_tile].uls, tex_width[i]);
        mXboxAxisT[i].Prepare(mRdp->texture_tile[uv_tile].shiftt, mRdp->texture_tile[uv_tile].ult, tex_height[i]);
        if (tm & (1 << 2 * i)) mXboxClampS[i] = %s;
        if (tm & (1 << 2 * i + 1)) mXboxClampT[i] = %s;
    }
'''


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Prepared axis anchor drift: ' + old.strip()[:80])
    return text.replace(old, new)


def header(text, anchor):
    return '#include "fast3d/coordinate.h"\n' + _once(text, anchor, anchor + '\n' + MEMBERS)


def capture(text, anchor, clamp_s='(tex_width2[i] - 0.5f) / tex_width[i]',
            clamp_t='(tex_height2[i] - 0.5f) / tex_height[i]'):
    """Insert before `anchor` in reconciliation, where comb, effective_tile,
    tm and the extents named by the clamp expressions are final."""
    return _once(text, anchor, CAPTURE % (clamp_s, clamp_t) + anchor)
