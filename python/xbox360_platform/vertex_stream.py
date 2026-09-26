"""Exact Fast3D triangle-stream write lowering, with per-runtime contracts.

The shared operation is deliberately mechanical. Each application supplies its
own loop anchors, color expressions, namespace, and audited write counts.
Nothing here chooses material, texture, shader, or game behavior.
"""

import re


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
