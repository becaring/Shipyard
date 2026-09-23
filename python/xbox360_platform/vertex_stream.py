"""Exact Fast3D triangle-stream write lowering, with per-runtime contracts.

The shared operation is deliberately mechanical. Each application supplies its
own loop anchors, color expressions, namespace, and audited write counts.
Nothing here chooses material, texture, shader, or game behavior.
"""

import re


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
