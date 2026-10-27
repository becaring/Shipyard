"""Pipeline mutation stamps: a certified triangle key stays current across neutral commands.

Every pipeline-key input (RDP modes, combiner, tiles, loads, texture dirty
flags, grayscale/shader state, geometry Z mode, viewport/scissor flags) must be
written only by handlers outside NEUTRAL_HANDLERS or inside reconciliation,
which recaptures. Native mutations must already clear key.valid
(fast3d_triangle_certificate.invalidate_native_calls). Consumers re-audit the
list when a listed handler, or anything it calls, starts writing key inputs.

The interpreter must declare `unsigned mXboxPipelineCertified` (zeroed with the
key) and certify through XboxPipelineCurrent; capture sites stamp the key.
"""

import re

NEUTRAL_HANDLERS = (
    'gfx_stubbed_command_handler', 'gfx_noop_handler_f3dex2', 'gfx_spnoop_command_handler_f3dex2',
    'gfx_cull_dl_handler_f3dex2', 'gfx_set_prim_depth_handler_rdp', 'gfx_set_interpolation_index_target',
    'gfx_set_env_color_handler_rdp', 'gfx_set_prim_color_handler_rdp', 'gfx_set_fog_color_handler_rdp',
    'gfx_set_fill_color_handler_rdp', 'gfx_set_blend_color_handler_rdp', 'gfx_moveword_handler_f3dex2',
    'gfx_marker_handler_otr', 'gfx_mtx_handler_f3dex2', 'gfx_mtx_otr_handler_custom',
    'gfx_mtx_otr_filepath_handler_custom', 'gfx_pop_mtx_handler_f3dex2', 'gfx_vtx_handler_f3dex2',
    'gfx_vtx_hash_handler_custom', 'gfx_vtx_otr_filepath_handler_custom', 'gfx_dl_handler_common',
    'gfx_end_dl_handler_common', 'gfx_dl_otr_hash_handler_custom', 'gfx_dl_otr_filepath_handler_custom',
    'gfx_dl_index_handler', 'gfx_branch_z_otr_handler_f3dex2', 'gfx_tri1_handler_f3dex2',
    'gfx_tri1_otr_handler_f3dex2', 'gfx_tri2_handler_f3dex', 'gfx_quad_handler_f3dex2')

# Upstream LUS dispatches OTR, then RDP, then the loaded ucode; neutrality
# follows the handler that actually runs for each F3DEX2 opcode.
SEQUENTIAL_LOOKUP = ('otrHandlers.contains(code) ? otrHandlers.at(code).second :\n'
                     '                rdpHandlers.contains(code) ? rdpHandlers.at(code).second :\n'
                     '                f3dex2Handlers.contains(code) ? f3dex2Handlers.at(code).second : 0')
SEQUENTIAL_CALLS = ('otrHandlers.at(opcode).second', 'rdpHandlers.at(opcode).second',
                    'ucode_handlers[ucode_handler_index]->at(opcode).second')

CERTIFICATE = '''
// A key recaptured or fully revalidated at the current stamp stays current
// until a non-neutral command runs, so runs of VTX/TRI/MTX/DL commands skip
// per-triangle revalidation of every RDP input.
static unsigned sXboxPipelineMutation = 1;
static __forceinline bool XboxPipelineCurrent(Interpreter& gfx) {
    if (gfx.mXboxPipelineKey.valid && gfx.mXboxPipelineCertified == sXboxPipelineMutation)
        return true;
    if (!gfx.mXboxPipelineKey.Certify(gfx))
        return false;
    gfx.mXboxPipelineCertified = sXboxPipelineMutation;
    return true;
}
'''

TABLE = '''static const GfxOpcodeHandlerFunc sXboxPipelineNeutralHandlers[] = {
    %s
};
struct XboxPipelineNeutralTable {
    unsigned char neutral[256];
    XboxPipelineNeutralTable() {
        for (unsigned op = 0; op < 256; ++op) {
            const int8_t code = (int8_t)op;
            const GfxOpcodeHandlerFunc handler =
                %s;
            neutral[op] = 0;
            for (size_t i = 0; handler && i < sizeof(sXboxPipelineNeutralHandlers) /
                 sizeof(sXboxPipelineNeutralHandlers[0]); ++i)
                if (handler == sXboxPipelineNeutralHandlers[i]) neutral[op] = 1;
        }
    }
};
static const XboxPipelineNeutralTable sXboxPipelineNeutral;
// Handlers such as rectangles emit triangles and then restore state, so a
// mutating handler advances the stamp both before and after it runs.
static __forceinline bool XboxDispatchStamped(GfxOpcodeHandlerFunc handler, F3DGfx** cmd,
                                              int8_t opcode) {
    const bool neutral = ucode_handler_index == ucode_f3dex2 &&
        sXboxPipelineNeutral.neutral[(uint8_t)opcode];
    if (!neutral) ++sXboxPipelineMutation;
    const bool moved = handler(cmd);
    if (!neutral) ++sXboxPipelineMutation;
    return moved;
}

'''

OWNER = 'static __declspec(thread) XboxDispatchOwner* sXboxDispatchOwner = 0;\n'
NAMES = 'const char* GfxGetOpcodeName(int8_t opcode) {'


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Pipeline stamp anchor drift: ' + old.strip()[:80])
    return text.replace(old, new)


def install(text, handlers=NEUTRAL_HANDLERS, lookup=SEQUENTIAL_LOOKUP, calls=SEQUENTIAL_CALLS,
            owner=OWNER, names=NAMES):
    """Add the stamp, neutral table and stamped dispatch; wrap `if (CALL(&cmd)) {` sites."""
    for name in handlers:
        if not re.search(r'\bbool ' + re.escape(name) + r'\(F3DGfx\*\* cmd\w*\)\s*\{', text):
            raise ValueError('Neutral handler inventory changed: ' + name)
    text = _once(text, owner, owner + CERTIFICATE)
    text = _once(text, names, TABLE % (',\n    '.join(handlers), lookup) + names)
    for call in calls:
        text = _once(text, 'if (' + call + '(&cmd)) {', 'if (XboxDispatchStamped(' + call + ', &cmd, opcode)) {')
    return text
