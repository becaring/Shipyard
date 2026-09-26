"""Command-local vertex invariants with the consumer's original fallback."""


def aspect_denominator(source, *, start, end, loop, eligible, framebuffer,
                       denominator, original_statement, numerator):
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError('Vertex aspect method anchors changed')
    if source.count('return ' + numerator + ' / (' + denominator + ');') != 1:
        raise ValueError('Vertex aspect arithmetic changed')
    first = source.index(start)
    last = source.index(end, first)
    body = source[first:last]
    if body.count(loop) != 1 or body.count(original_statement) != 1:
        raise ValueError('Vertex aspect loop inventory changed')
    body = body.replace(loop, '    bool shipyardAspectReady = false;\n'
                             '    float shipyardAspectDenominator;\n' + loop)
    replacement = '''        if (%s && !%s) {
            if (!shipyardAspectReady) {
                shipyardAspectDenominator = %s;
                shipyardAspectReady = true;
            }
            x = %s / shipyardAspectDenominator;
        } else {
%s
        }''' % (eligible, framebuffer, denominator, numerator, '    ' + original_statement)
    body = body.replace(original_statement, replacement)
    return source[:first] + body + source[last:]


def normal_components(source, *, start, end, lighting, eligible, needed, components):
    """Reuse exact byte-to-float conversions only for disjoint vertex packets."""
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError('Vertex normal method anchors changed')
    first = source.index(start)
    last = source.index(end, first)
    body = source[first:last]
    if body.count(lighting) != 1:
        raise ValueError('Vertex lighting anchor changed')
    for axis, expression in enumerate(components):
        if body.count(expression) != 4:
            raise ValueError('Vertex normal use inventory changed')
        body = body.replace(expression, '(shipyardNormalBatch ? shipyardNormal[%d] : %s)' % (axis, expression))
    prelude = '\n            const bool shipyardNormalBatch = %s && (%s);\n' % (eligible, needed)
    prelude += '            float shipyardNormal[3];\n            if (shipyardNormalBatch) {\n'
    for axis, expression in enumerate(components):
        prelude += '                shipyardNormal[%d] = (float)%s;\n' % (axis, expression)
    prelude += '            }\n'
    body = body.replace(lighting, lighting + prelude)
    return source[:first] + body + source[last:]


def light_colors(source, *, start, end, loop, positive, eligible, capacity, components):
    """Convert an immutable light's RGB only on its first positive contribution."""
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError('Vertex light-color method anchors changed')
    first = source.index(start)
    last = source.index(end, first)
    body = source[first:last]
    if body.count(loop) != 1 or body.count(positive) != 1:
        raise ValueError('Vertex light-color loop inventory changed')
    for axis, expression in enumerate(components):
        if body.count(expression) != 1:
            raise ValueError('Vertex light-color use inventory changed')
        body = body.replace(expression, '(shipyardLightColorBatch ? shipyardLightColors[i][%d] : %s)' % (axis, expression))
    prelude = '    const bool shipyardLightColorBatch = %s;\n' % eligible
    prelude += '    bool shipyardLightColorReady[%s] = {};\n' % capacity
    prelude += '    float shipyardLightColors[%s][3];\n' % capacity
    body = body.replace(loop, prelude + loop)
    initialize = '\n                    if (shipyardLightColorBatch && !shipyardLightColorReady[i]) {\n'
    for axis, expression in enumerate(components):
        initialize += '                        shipyardLightColors[i][%d] = (float)%s;\n' % (axis, expression)
    initialize += '                        shipyardLightColorReady[i] = true;\n                    }\n'
    body = body.replace(positive, positive + initialize)
    return source[:first] + body + source[last:]
