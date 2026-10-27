"""Indexed triangle rows shared inside TRI2 runs (fast3d/row_reuse.h).

A TRI2 run (fast3d_triangle_run) opens a sharing scope; Flush and pipeline
reconciliation close it. Inside it each distinct LoadedVertex packs one row
and corners submit uint16 indices through ShipyardXenos::DrawIndexedTriangles.
A triangle whose corners all have rows skips the packing prelude entirely.
The cull reads per-run x/w, y/w instead of a keyed projection and FPSCR read.

Runs are off under the debugger, traced dispatch and USE_GBI_TRACE, so those
paths keep identity rows; Flush then submits the unchanged flat stream through
the consumer's renderer. Consumers keep their own stream layout: rows share
only what is a function of the vertex plus run-constant RDP state.
"""

import re

ROWS = 'mXboxTriangles'
HANDLER = 'bool gfx_tri2_handler_f3dex(F3DGfx** cmd0) {'
# fast3d_triangle_run's per-Run enable; debugger_snapshot may rewrite its operand.
RUN_ENABLE = re.compile(r'\n    sXboxTriangleRunEnabled = [^\n]*;\n')
CONTROL = ('// Live A/B control, sampled once per Run: [0] requests shared rows and the\n'
           '// run NDC cache, [1] reports whether this Run uses them.\n'
           'extern "C" volatile unsigned gShipyardTriangleRowControl[2] = { 1, 0 };\n'
           'static bool sXboxTriangleRows = false;\n')
CAPACITY = 'MAX_TRI_BUFFER * 32 * 3'


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Triangle row anchor drift: ' + old.strip()[:80])
    return text.replace(old, new)


def _method(text, signature):
    if text.count(signature) != 1:
        raise ValueError('Triangle row anchor drift: ' + signature)
    start = text.index(signature)
    return start, text.index('\n}', start) + 2


def _edit(text, signature, edit):
    start, end = _method(text, signature)
    return text[:start] + edit(text[start:end]) + text[end:]


def header(text, anchor):
    text = _once(text, anchor, anchor + '\n    Shipyard::Fast3D::TriangleRows<256> ' + ROWS + ';')
    return '#include "fast3d/row_reuse.h"\n' + text


def source(text, *, constructor, draw, entry, capture='    mXboxPipelineKey.Capture(*this,p);',
           result='p', share='', buffer='const size_t MAX_TRI_BUFFER = 256;'):
    """Constructor reset, Flush submission, reconciliation layout and run scope.

    `constructor` is a constructor statement to follow, `draw` the Flush draw
    statement, `entry` the reconciliation signature, `capture` its key capture
    (after `result` is published) and `share` extra per-Run enable terms.
    """
    text = _once(text, buffer, buffer + '\nstatic_assert(MAX_TRI_BUFFER == 256, "Shipyard triangle row indices");')
    text = _once(text, constructor, constructor + '\n    ' + ROWS + '.Reset();')

    def flush(body):
        indent = draw[:len(draw) - len(draw.lstrip())]
        body = _once(body, draw, '''%(i)sif (%(rows)s.Indexed(mBufVboNumTris))
%(i)s    %(rows)s.Draw(mBufVbo, mBufVboLen, mBufVboNumTris);
%(i)selse
    %(draw)s''' % {'i': indent, 'rows': ROWS, 'draw': draw})
        if not body.endswith('\n}'):
            raise ValueError('Triangle row Flush tail changed')
        return body[:-1] + '    ' + ROWS + '.Clear();\n}'
    text = _edit(text, 'void Interpreter::Flush() {', flush)
    text = _once(text, entry, entry + '\n    ' + ROWS + '.Invalidate();')
    # Row layout of the reconciled pipeline. LOD fraction reads the
    # triangle's first w, so those rows are never shared (G_TL_LOD or not:
    # other_mode_l is not part of every consumer's key).
    layout = '''    {
        size_t xboxRowFloats = 4;
        for (unsigned t = 0; t < 2; ++t)
            if (%(p)s.usedTextures[t])
                xboxRowFloats += 2 + ((%(p)s.tm >> (2 * t)) & 1) + ((%(p)s.tm >> (2 * t + 1)) & 1);
        xboxRowFloats += (%(p)s.fog ? 4 : 0) + (%(p)s.grayscale ? 4 : 0) + %(p)s.numInputs * (%(p)s.alpha ? 4 : 3);
        bool xboxRowLod = false;
        for (unsigned input = 0; input < %(p)s.numInputs; ++input)
            if (%(p)s.comb->shader_input_mapping[0][input] == G_CCMUX_LOD_FRACTION ||
                (%(p)s.alpha && %(p)s.comb->shader_input_mapping[1][input] == G_CCMUX_LOD_FRACTION))
                xboxRowLod = true;
        %(rows)s.Prepare(xboxRowFloats, xboxRowLod);
    }
''' % {'p': result, 'rows': ROWS}
    text = _once(text, capture, layout + capture)
    enable = RUN_ENABLE.findall(text)
    if len(enable) != 1:
        raise ValueError('Triangle row anchor drift: TRI2 run enable')
    text = _once(text, enable[0], enable[0] +
                 '    sXboxTriangleRows = sXboxTriangleRunEnabled && gShipyardTriangleRowControl[0] != 0%s;\n'
                 '    gShipyardTriangleRowControl[1] = sXboxTriangleRows ? 1 : 0;\n' %
                 (' && ' + share if share else ''))

    def run(body):
        body = _once(body, '    for (;;) {\n', '    gfx->' + ROWS + '.Begin(xboxRun && sXboxTriangleRows);\n    for (;;) {\n')
        return _once(body, '    }\n    return false;\n}', '    }\n    gfx->' + ROWS + '.End();\n    return false;\n}')
    text = _once(text, HANDLER, CONTROL + HANDLER)
    return _edit(text, HANDLER, run)


def cull(text, *, first, last='        float cross = dx1 * dy2 - dy1 * dx2;\n',
         function='void Interpreter::GfxSpTri1(', indices=('vtx1_idx', 'vtx2_idx', 'vtx3_idx'),
         vertices=('v1', 'v2', 'v3')):
    """Inside a run, the cull's dx/dy come from the run NDC cache.

    `first` begins the consumer's projection block, which ends before `last`
    and declares dx1, dy1, dx2 and dy2. Outside runs that block is unchanged.
    """
    def edit(body):
        start = body.index(first) if body.count(first) == 1 else -1
        if start < 0 or body.count(last) != 1 or body.index(last) < start:
            raise ValueError('Triangle row cull block changed')
        end = body.index(last)
        block = body[start:end]
        for name in ('dx1', 'dy1', 'dx2', 'dy2'):
            block = _once(block, '        float %s = ' % name, '        %s = ' % name)
        loads = ''.join('            %s.ndc.Load(%s, *%s, x%d, y%d);\n' % (ROWS, index, vertex, n + 1, n + 1)
                        for n, (index, vertex) in enumerate(zip(indices, vertices)))
        replacement = '''        float dx1, dy1, dx2, dy2;
        if (%(rows)s.run) {
            float x1, y1, x2, y2, x3, y3;
%(loads)s            dx1 = x1 - x2;
            dy1 = y1 - y2;
            dx2 = x3 - x2;
            dy2 = y3 - y2;
        } else {
%(block)s        }
''' % {'rows': ROWS, 'loads': loads, 'block': block}
        return body[:start] + replacement + body[end:]
    return _edit(text, function, edit)


PLAN = '''    // SHIPYARD TRIANGLE ROWS: one stride per batch, room for three rows, then
    // rows for the corners not already packed in this run.
    if (mBufVboNumTris && (%(rows)s.stride != %(stride)s ||
        mBufVboLen + 3 * %(stride)s > %(capacity)s)) Flush();
    %(rows)s.stride = %(stride)s;
    LoadedVertex* xboxPack[3];
    const int xboxPackCount = (int)%(rows)s.Plan(%(corners)s, xboxPack, mBufVboNumTris, !%(rect)s);
    if (xboxPackCount) {
'''


def _plan(stride, corners, rect):
    return PLAN % {'rows': ROWS, 'stride': stride, 'capacity': CAPACITY, 'corners': corners, 'rect': rect}


def _region(emitter, begin, end, corners, uses):
    if emitter.count(begin) != 1 or emitter.count(end) != 1 or emitter.index(end) < emitter.index(begin):
        raise ValueError('Triangle row packing region changed')
    first = emitter.index(begin)
    last = emitter.index(end) + len(end)
    region = emitter[first:last]
    if region.count(corners + '[') != uses:
        raise ValueError('Triangle row corner inventory changed: %d' % region.count(corners + '['))
    return first, last, region.replace(corners + '[', 'xboxPack[')


def row_emitter(text, *, signature, begin, end, loop, uses, stride=ROWS + '.next', corners='v_arr',
                rect='is_rect'):
    """Cursor-style packer: `begin` opens the output cursor, `loop` is the
    per-corner loop header and `end` publishes mBufVboLen."""
    def edit(emitter):
        first, last, region = _region(emitter, begin, end, corners, uses)
        region = _once(region, loop, loop.replace('i < 3;', 'i < xboxPackCount;'))
        return emitter[:first] + _plan(stride, corners, rect) + region + '\n    }' + emitter[last:]
    return _edit(text, signature, edit)


def column_emitter(text, *, signature, rows, end, uses, loops, stride='xboxStride', corners='v_arr',
                   rect='is_rect', loop='for(int i=0;i<3;++i)'):
    """Three-row packer: `rows` declares the three row pointers, `end` advances
    mBufVboLen by three rows. Per-corner loops visit only packed corners."""
    def edit(emitter):
        first, last, region = _region(emitter, rows, end, corners, uses)
        if region.count(loop) != loops:
            raise ValueError('Triangle row lane loop inventory changed: %d' % region.count(loop))
        region = region.replace(loop, loop.replace('i<3', 'i<xboxPackCount'))
        # Plan pads missing lanes with the first packed vertex, so their
        # fixed-lane values equal row 0's and may be stored there again.
        region = _once(region, rows, '''    float* xboxRows[3]={mBufVbo+mBufVboLen,
                       mBufVbo+mBufVboLen+(xboxPackCount>1?%(s)s:0),
                       mBufVbo+mBufVboLen+(xboxPackCount>2?2*%(s)s:0)};''' % {'s': stride})
        region = _once(region, end, end.replace('3*', 'xboxPackCount*') + '\n    }')
        return emitter[:first] + _plan(stride, corners, rect) + region + emitter[last:]
    return _edit(text, signature, edit)
