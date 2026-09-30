"""Exact Fast3D triangle-stream write lowering, with per-runtime contracts.

The shared operation is deliberately mechanical. Each application supplies its
own loop anchors, color expressions, namespace, and audited write counts.
Nothing here chooses material, texture, shader, or game behavior.
"""

import re


def reuse_material_inputs(source, *, start, end, cursor, mapping, shade):
    """Reuse first-vertex packed invariant channels within the same triangle.

    The caller audits that only the shade selector reads the current vertex.
    The original conversion remains intact for the first vertex and all shade
    channels. Pointers reference completed writes in this call's vertex buffer.
    """
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError('Fast3D material loop anchors changed')
    begin = source.index(start)
    finish = source.index(end, begin)
    body = source[begin:finish]
    anchor = '                switch (%s[k][j]) {' % mapping
    if body.count(anchor) != 1:
        raise ValueError('Fast3D material selector changed')
    prelude = ('    const float* shipyardMaterialInputs[2][sizeof(%s[0]) / sizeof(%s[0][0])];\n'
               % (mapping, mapping))
    reuse = '''                // Begin shared invariant material reuse.
                if (i != 0 && %(mapping)s[k][j] != %(shade)s) {
                    const float* input = shipyardMaterialInputs[k][j];
                    *%(cursor)s++ = input[0];
                    if (k == 0) {
                        *%(cursor)s++ = input[1];
                        *%(cursor)s++ = input[2];
                    }
                    continue;
                }
                if (i == 0) shipyardMaterialInputs[k][j] = %(cursor)s;
                // End shared invariant material reuse.
''' % dict(mapping=mapping, shade=shade, cursor=cursor)
    body = body.replace(anchor, reuse + anchor)
    return source[:begin] + prelude + body + source[finish:]


def hoist_colors(source, *, start, end, cursor, namespace, groups):
    """Normalize guarded material colors once per triangle, without reordering math."""
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError('Fast3D color loop anchors changed')
    begin = source.index(start)
    finish = source.index(end, begin)
    body = source[begin:finish]
    prelude = ''
    for name, guard, expressions in groups:
        prelude += '    float %s[%d];\n    if (%s) {\n' % (name, len(expressions), guard)
        for index, expression in enumerate(expressions):
            value = '%s::UnitColor(%s)' % (namespace, expression)
            assignment = '*%s++ = %s;' % (cursor, value)
            if body.count(assignment) != 1:
                raise ValueError('Fast3D material color changed: ' + expression)
            body = body.replace(assignment, '*%s++ = %s[%d];' % (cursor, name, index))
            prelude += '        %s[%d] = %s;\n' % (name, index, value)
        prelude += '    }\n'
    return source[:begin] + prelude + body + source[finish:]


def hoist_clamp_bounds(source, *, start, end, cursor, used_textures, axes):
    """Evaluate each used slot/axis bound once, retaining its exact expression.

    The consumer supplies audited invariant expressions and guards. No value
    survives the packing call; unused textures and axes remain unevaluated.
    """
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError('Fast3D clamp loop anchors changed')
    begin = source.index(start)
    finish = source.index(end, begin)
    body = source[begin:finish]
    prelude = ''.join('    float %s[2];\n' % name for name, guard, expr in axes)
    prelude += '    for (int t = 0; t < 2; ++t) {\n'
    prelude += '        if (!%s[t]) continue;\n' % used_textures
    for name, guard, expr in axes:
        assignment = '*%s++ = %s;' % (cursor, expr)
        if body.count(assignment) != 1:
            raise ValueError('Fast3D clamp expression changed: ' + name)
        body = body.replace(assignment, '*%s++ = %s[t];' % (cursor, name))
        prelude += '        if (%s) %s[t] = %s;\n' % (guard, name, expr)
    prelude += '    }\n'
    return source[:begin] + prelude + body + source[finish:]


def lower(source, *, start, end, color_pattern, namespace, cursor,
          expected_writes, expected_colors):
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError('Fast3D vertex loop anchors changed')
    begin = source.index(start)
    finish = source.index(end, begin)
    original = source[begin:finish]
    expression = re.compile(color_pattern)
    output = []
    writes = colors = 0
    for line in original.splitlines(keepends=True):
        if not line.lstrip().startswith('//'):
            writes += line.count('mBufVbo[mBufVboLen++]')
            line = line.replace('mBufVbo[mBufVboLen++]', '*' + cursor + '++')
            line, count = expression.subn(namespace + r'::UnitColor(\1)', line)
            colors += count
        output.append(line)
    if (writes, colors) != (expected_writes, expected_colors):
        raise ValueError('Fast3D vertex stream inventory changed: writes=%d colors=%d' %
                         (writes, colors))
    replacement = ('    float* ' + cursor + ' = mBufVbo + mBufVboLen;\n'
                   + ''.join(output)
                   + '    mBufVboLen = (size_t)(' + cursor + ' - mBufVbo);\n\n')
    return '#include "fast3d/unit_color.h"\n' + source[:begin] + replacement + source[finish:]
