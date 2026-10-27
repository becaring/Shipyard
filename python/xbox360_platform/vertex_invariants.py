"""Command-local vertex invariants with the consumer's original fallback."""


def aspect_denominator(source, *, start, end, loop, eligible, framebuffer,
                       denominator, original_statement, numerator, reciprocal=False):
    """`reciprocal` divides through fast3d/aspect_reciprocal.h; `eligible`
    must then imply round-to-nearest."""
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
                             '    float shipyardAspectDenominator;\n' +
                        ('    Shipyard::Fast3D::AspectReciprocal shipyardAspect(1.0f);\n' if reciprocal else '') + loop)
    prepare = ('\n                shipyardAspect = Shipyard::Fast3D::AspectReciprocal(shipyardAspectDenominator);'
               if reciprocal else '')
    divide = ('shipyardAspect.Divide(%s, shipyardAspectDenominator)' if reciprocal else
              '%s / shipyardAspectDenominator') % numerator
    replacement = '''        if (%s && !%s) {
            if (!shipyardAspectReady) {
                shipyardAspectDenominator = %s;%s
                shipyardAspectReady = true;
            }
            x = %s;
        } else {
%s
        }''' % (eligible, framebuffer, denominator, prepare, divide, '    ' + original_statement)
    body = body.replace(original_statement, replacement)
    include = '#include "fast3d/aspect_reciprocal.h"\n' if reciprocal else ''
    return include + source[:first] + body + source[last:]


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


def matrix_quantize(source, *, start='void Interpreter::GfxSpMatrix(', end='void Interpreter::GfxSpPopMatrix(',
                    marker='        for (int i = 0; i < 4; i++) {'):
    """Quantize a replacement matrix with fast3d/clip_quantize.h under
    round-to-nearest; other modes and rejected values keep the scalar loop."""
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError('Matrix quantize method anchors changed')
    first = source.index(start)
    last = source.index(end, first)
    body = source[first:last]
    if body.count('    float matrix[4][4];') != 1 or body.count('int as_int = (int)(v * 65536.0f);') != 1:
        raise ValueError('Matrix quantize inventory changed')
    body = body.replace('    float matrix[4][4];', '    __declspec(align(16)) float matrix[4][4];')
    loop = body.index(marker)
    scalar = body[loop:body.index('    } else {', loop)]
    if 'as_int' not in scalar:
        raise ValueError('Matrix quantize replacement loop changed')
    body = body.replace(scalar, '        if (!Shipyard::Fast3D::NearestRounding() || '
                        '!Shipyard::Fast3D::QuantizeMatrix(it->second.mf, matrix)) {\n' + scalar + '        }\n', 1)
    return '#include "fast3d/clip_quantize.h"\n' + source[:first] + body + source[last:]


def light_direction(source, *, signature='void Interpreter::CalculateNormalDir(const F3DLight_t* light, float coeffs[3]) {'):
    """Light-direction bytes divide by 127 through the exact nearest product."""
    old = '    float light_dir[3] = { light->dir[0] / 127.0f, light->dir[1] / 127.0f, light->dir[2] / 127.0f };'
    if source.count(signature) != 1 or source.count(old) != 1 or source.index(old) < source.index(signature):
        raise ValueError('Light direction anchors changed')
    new = ('    const bool shipyardNearestNormal = Shipyard::Fast3D::NearestRounding();\n    float light_dir[3] = { ' +
           ', '.join('Shipyard::Fast3D::DivideNormalByte127(Shipyard::Fast3D::NormalByteFloat(light->dir[%d]), '
                     'shipyardNearestNormal)' % axis for axis in range(3)) + ' };')
    return '#include "fast3d/vertex_transform.h"\n#include "fast3d/numeric.h"\n' + source.replace(old, new)
