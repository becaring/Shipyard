"""Restore Fast3D display-list bounding-volume rejection (G_CULLDL).

Upstream F3DEX2 dispatch leaves the handler as a stub that never culls. The
handler rejects a list only when every vertex in the encoded range shares an
outside clip plane; near stays conservative. Vertex loads must write clip_rej
for every loaded vertex, and handlers must already use XboxDispatchInterpreter.
"""

ORIGINAL = '''bool gfx_cull_dl_handler_f3dex2(F3DGfx** cmd) {
    // TODO:
    return false;
}'''

MARKER = '// MODERN DISPLAY LIST CULL BEGIN'

HANDLER = '''// MODERN DISPLAY LIST CULL BEGIN
bool gfx_cull_dl_handler_f3dex2(F3DGfx** cmd0) {
    const F3DGfx* cmd = *cmd0;
    const uint32_t encodedFirst = uint32_t(cmd->words.w0) & 0xffffu;
    const uint32_t encodedLast = uint32_t(cmd->words.w1) & 0xffffu;
    unsigned first, last;
    if (ucode_handler_index == ucode_f3dex2 ||
        ucode_handler_index == ucode_f3dex || ucode_handler_index == ucode_f3dexb) {
        // F3DEX-family GBI encodes both inclusive endpoints in halfwords.
        if ((encodedFirst | encodedLast) & 1u) return false;
        first = encodedFirst >> 1;
        last = encodedLast >> 1;
    } else if (ucode_handler_index == ucode_f3d || ucode_handler_index == ucode_f3db) {
        // Original Fast3D uses 40-byte RSP vertex slots and an exclusive end.
        // The GBI's four-bit (vend + 1) wraps vertex 15's endpoint to zero.
        if (encodedFirst % 40u || encodedLast % 40u ||
            encodedFirst >= 16u * 40u || encodedLast >= 16u * 40u) return false;
        first = encodedFirst / 40u;
        last = encodedLast ? encodedLast / 40u - 1u : 15u;
    } else {
        return false;
    }
    if (first > last || last >= MAX_VERTICES) return false;

    Interpreter* gfx = XboxDispatchInterpreter();
    // Both eager and deferred loads have already transformed positions and
    // written these flags. Rejection must not materialize unused RGB/UV data.
    unsigned outside = 0x2fu; // left, right, bottom, top, far; near stays conservative
    for (unsigned i = first; i <= last; ++i) {
        outside &= gfx->mRsp->loaded_vertices[i].clip_rej;
        if (!outside) return false;
    }

    // Identical to ENDDL: retain the caller's continuation and branch markers.
    gfx->mMarkerOn = false;
    *cmd0 = g_exec_stack.ret();
    return true;
}
// MODERN DISPLAY LIST CULL END'''


def transform(source):
    if MARKER in source:
        return source
    if source.count(ORIGINAL) != 1:
        raise ValueError('Fast3D display-list cull handler changed')
    return source.replace(ORIGINAL, HANDLER)
