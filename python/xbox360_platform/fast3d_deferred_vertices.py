"""Deferred lit/fog vertex attributes (fast3d/deferred_vertex.h).

A directional-lit, non-texgen load publishes positions and clip codes at
once and keeps a lighting/fog snapshot per vertex. Triangles that survive
culling, ModifyVertex and any load reading RSP bytes materialize first: those
are the only readers of loaded u/v/color. Consumers supply their position
replay (aspect and clip policy). Requires round-to-nearest, as VMX does.
"""

from xbox360_platform.fast3d_triangle_rows import RUN_ENABLE

MEMBER = '    Shipyard::Fast3D::DeferredVertices<F3DVtx, MAX_VERTICES, MAX_LIGHTS> mXboxDeferred;'
DECLARATION = '    bool XboxDeferVertices(size_t n_vertices, size_t dest_index, const F3DVtx* vertices);'
VERTEX = 'void Interpreter::GfxSpVertex(size_t n_vertices, size_t dest_index, const F3DVtx* vertices) {'

METHOD = '''// SHIPYARD DEFERRED VERTICES BEGIN
// Live A/B control, sampled once per Run: [0] bit 0 defers lit loads, bit 1
// enables the normal cache; [1] reports the bits this Run uses.
extern "C" volatile unsigned gShipyardDeferredVertexControl[2] = { 3, 0 };

__declspec(noinline) bool Interpreter::XboxDeferVertices(size_t n_vertices, size_t dest_index, const F3DVtx* vertices) {
    static_assert(sizeof(LoadedVertex) == Shipyard::Fast3D::ClipVertexStride &&
                  offsetof(LoadedVertex, clip_rej) == Shipyard::Fast3D::ClipByteOffset &&
                  offsetof(LoadedVertex, x) == 0, "Deferred vertex clip layout");
    // A null source writes nothing; otherwise the eager loop writes every slot.
    if (!vertices || !n_vertices)
        return false;
    if (!Shipyard::Fast3D::VertexSourceDisjoint(vertices, n_vertices * sizeof(F3DVtx), mRsp, sizeof(*mRsp)) ||
        !Shipyard::Fast3D::VertexSourceDisjoint(vertices, n_vertices * sizeof(F3DVtx), &mXboxDeferred,
                                                sizeof(mXboxDeferred))) {
        // The eager loop may read earlier loaded-vertex bytes as its source.
        mXboxDeferred.MaterializeAll(mRsp->loaded_vertices);
        return false;
    }
    mXboxDeferred.Discard(dest_index, n_vertices);
    if (n_vertices > MAX_VERTICES || dest_index > MAX_VERTICES || n_vertices > MAX_VERTICES - dest_index ||
        !mXboxDeferred.enabled ||
        (mRsp->geometry_mode & (G_LIGHTING | G_TEXTURE_GEN | G_LIGHTING_POSITIONAL)) != G_LIGHTING ||
        mRsp->current_num_lights == 0 || mRsp->current_num_lights > MAX_LIGHTS + 1 ||
        !Shipyard::Fast3D::NearestRounding())
        return false;
    // The eager loop refreshes these on its first lit vertex.
    if (mRsp->lights_changed) {
        for (int i = 0; i < mRsp->current_num_lights - 1; i++)
            CalculateNormalDir(&mRsp->current_lights[i].l, mRsp->current_lights_coeffs[i]);
        CalculateNormalDir(&mRsp->lookat[0], mRsp->current_lookat_coeffs[0]);
        CalculateNormalDir(&mRsp->lookat[1], mRsp->current_lookat_coeffs[1]);
        mRsp->lights_changed = false;
    }
    Shipyard::Fast3D::DeferredLighting<MAX_LIGHTS>* const context = mXboxDeferred.Snapshot(
        mRsp->current_lights, mRsp->current_num_lights, mRsp->current_lights_coeffs,
        (mRsp->geometry_mode & G_FOG) != 0, mRsp->texture_scaling_factor.s, mRsp->texture_scaling_factor.t,
        mRsp->fog_mul, mRsp->fog_offset, true);
    if (!context)
        return false;
    LoadedVertex* const d = &mRsp->loaded_vertices[dest_index];
    // Accepted VMX matrices write finite xyzw with w zero or normal; a
    // rejected matrix writes nothing and the eager loop runs.
    if (!Shipyard::Fast3D::TransformPositionsUnaligned(vertices, n_vertices, mRsp->MP_matrix, d, false,
                                                       sizeof(LoadedVertex)))
        return false;
%(positions)s
    mXboxDeferred.Commit(vertices, n_vertices, dest_index, context);
    return true;
}
// SHIPYARD DEFERRED VERTICES END

'''


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Deferred vertex anchor drift: ' + old.strip()[:80])
    return text.replace(old, new)


def _method(text, signature, edit):
    if text.count(signature) != 1:
        raise ValueError('Deferred vertex anchor drift: ' + signature)
    start = text.index(signature)
    end = text.index('\n}', start) + 2
    return text[:start] + edit(text[start:end]) + text[end:]


def header(text, *, member, declaration='    void GfxSpVertex(size_t numVertices, size_t destIndex, const F3DVtx* vertices);'):
    text = _once(text, member, member + '\n' + MEMBER)
    text = _once(text, declaration, DECLARATION + '\n' + declaration)
    return '#include "fast3d/deferred_vertex.h"\n#include "fast3d/clip_quantize.h"\n' + text


def source(text, *, constructor, positions, emit):
    """`positions` adjusts x and clips the n_vertices rows at `d` (it may
    return false before Commit); `emit` is GfxSpTri1's emitter call."""
    text = _once(text, constructor, constructor + '\n    mXboxDeferred.Reset();')
    text = _once(text, VERTEX, METHOD % {'positions': positions.rstrip('\n')} + VERTEX)
    text = _method(text, VERTEX, lambda body: _once(
        body, '    EnsureMatrixProduct();\n',
        '    EnsureMatrixProduct();\n    if (XboxDeferVertices(n_vertices, dest_index, vertices)) return;\n'))
    text = _once(text, '    SUPPORT_CHECK(where == G_MWO_POINT_ST);\n',
                 '    SUPPORT_CHECK(where == G_MWO_POINT_ST);\n    mXboxDeferred.Materialize(vtx_idx, mRsp->loaded_vertices);\n')
    # After culling: rejected triangles never materialize their vertices.
    text = _method(text, 'void Interpreter::GfxSpTri1(', lambda body: _once(
        body, emit, ''.join('    mXboxDeferred.Materialize(%s, mRsp->loaded_vertices);\n' % index
                            for index in ('vtx1_idx', 'vtx2_idx', 'vtx3_idx')) + emit))
    enable = RUN_ENABLE.findall(text)
    if len(enable) != 1:
        raise ValueError('Deferred vertex anchor drift: Run enable')
    return _once(text, enable[0], enable[0] +
                 '    gShipyardDeferredVertexControl[1] = mXboxDeferred.Configure(gShipyardDeferredVertexControl[0]);\n')
