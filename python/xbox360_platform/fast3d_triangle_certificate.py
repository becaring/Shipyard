"""Invalidate Fast3D reconciliation after native backend state mutations."""

import re


NATIVE_STATE_SETTERS = (
    'SetDepthTestAndMask', 'SetColorWriteMask', 'SetZmodeDecal', 'SetViewport', 'SetScissor',
    'SetUseAlpha', 'SetTextureFilter', 'SetSrgbMode', 'SelectTexture', 'SelectTextureFb',
    'SetSamplerParameters', 'UploadTexture', 'DeleteTexture', 'LoadShader', 'UnloadShader',
    'StartDrawToFramebuffer', 'UpdateFramebufferParameters', 'ResolveMSAAColorBuffer',
    'CopyFramebuffer', 'ClearFramebuffer', 'ClearColorRegion', 'StartFrame', 'EndFrame',
    'FinishRender', 'Init',
)


def call_end(text, opening):
    depth = 0
    position = opening
    while position < len(text):
        if text.startswith('//', position):
            position = text.index('\n', position)
            continue
        if text.startswith('/*', position):
            position = text.index('*/', position + 2) + 2
            continue
        char = text[position]
        if char in ('"', "'"):
            quote = char
            position += 1
            while position < len(text):
                if text[position] == '\\':
                    position += 2
                    continue
                if text[position] == quote:
                    break
                position += 1
        elif char == '(':
            depth += 1
        elif char == ')':
            depth -= 1
            if depth == 0:
                return position
        position += 1
    raise ValueError('Unterminated native state call')


def invalidate_native_calls(text, expression, minimum=0, setters=NATIVE_STATE_SETTERS):
    pattern = r'(?m)^([ \t]*)(gfx->)?mRapi->(' + '|'.join(setters) + r')\('
    matches = list(re.finditer(pattern, text))
    if len(matches) < minimum:
        raise ValueError('Re-audit native pipeline invalidation operations')
    for match in reversed(matches):
        indent, owner = match[1], match[2] or ''
        end = call_end(text, match.end() - 1)
        if not re.match(r'\s*;', text[end + 1:]):
            raise ValueError('Native pipeline invalidation expected standalone call')
        text = text[:end + 1] + ')' + text[end + 1:]
        text = text[:match.start()] + indent + '(' + owner + expression + ', ' + text[match.start() + len(indent):]
    return text
