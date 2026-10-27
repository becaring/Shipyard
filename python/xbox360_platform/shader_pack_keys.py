"""Fast3D program keys of harvested draw states, from the consumer's own interpreter.

harness_source() lifts Interpreter::GenerateCC and GfxSpTri1's combiner-option
block out of the consumer's (projected) interpreter, with the shader enums of
its interpreter.h, into a host program; derive() feeds it
every distinct (combine, other mode, grayscale). Clamp masks follow GfxSpTri1:
texture and tile sizes are unknown offline, so either outcome is kept.
"""

import re
import subprocess

from . import host_toolchain
from .shader_pack_harvest import rect_combine

# Other-mode bits that feed the options in every pinned LUS (alpha compare,
# primitive depth source, coverage, blender, cycle type), as a safe superset.
RELEVANT_OTHER_MODE = 0x7 | 0x1000 | (0xFFFF << 16) | (0x3 << (32 + 20))
G_TX_MIRROR, G_TX_CLAMP = 1, 2

HARNESS = r'''#include <stack>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "@GBI@"
struct ShaderProgram;
@DECLARATIONS@
// GenerateCC reads the key's combine mode and options and fills these members.
struct ColorCombinerKey { uint64_t combine_mode, options, shader_id; };
struct ColorCombiner {
    uint64_t shader_id0, shader_id1;
    bool usedTextures[2];
    ShaderProgram* prg[16];
    uint8_t shader_input_mapping[2][7];
};
namespace {
struct LoadedTexture { bool masked, blended; };
struct Rdp { uint32_t other_mode_l, other_mode_h; bool grayscale; LoadedTexture loaded_texture[2]; uint64_t combine_mode; };
void GenerateCC(ColorCombiner* comb, const ColorCombinerKey& key) @GENERATE@
uint64_t Options(Rdp* mRdp, std::stack<size_t>& mShaderStack) {
@OPTIONS@
    return cc_options;
}
}
// stdin: "<combine> <other mode> <grayscale>" (hex) per line;
// stdout: the TEXEL0_CLAMP_S option, then "<id0> <id1> <used0> <used1>" per line.
int main() {
    printf("%llx\n", (unsigned long long)SHADER_OPT(TEXEL0_CLAMP_S));
    unsigned long long combine, mode;
    unsigned gray;
    while (scanf("%llx %llx %x", &combine, &mode, &gray) == 3) {
        Rdp rdp = {};
        rdp.other_mode_l = (uint32_t)mode;
        rdp.other_mode_h = (uint32_t)(mode >> 32);
        rdp.grayscale = gray != 0;
        rdp.combine_mode = combine;
        std::stack<size_t> shaders;
        ColorCombinerKey key = {};
        key.combine_mode = combine;
        key.options = Options(&rdp, shaders);
        ColorCombiner comb = {};
        GenerateCC(&comb, key);
        printf("%llx %llx %d %d\n", (unsigned long long)comb.shader_id0, (unsigned long long)comb.shader_id1,
               comb.usedTextures[0] ? 1 : 0, comb.usedTextures[1] ? 1 : 0);
    }
    return 0;
}
'''


def braced(text, start):
    """text[start:] through the brace that closes the first '{' at or after start."""
    depth = 0
    for i in range(text.index('{', start), len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if not depth:
                return text[start:i + 1]
    raise ValueError('unbalanced braces')


def once(text, anchor):
    if text.count(anchor) != 1:
        raise ValueError('Expected exactly one interpreter anchor: ' + anchor[:80])
    return text.index(anchor)


def declarations(header):
    """The shader input enum, ShaderOpts (scoped enum or projected struct form),
    SHADER_OPT and SHADER_ID_SHIFT of interpreter.h, for global scope."""
    options = re.findall(r'(?:enum class|struct) ShaderOpts \{', header)
    if len(options) != 1:
        raise ValueError('Expected exactly one interpreter.h ShaderOpts')
    parts = [braced(header, once(header, 'enum {\n    SHADER_0,')) + ';',
             braced(header, once(header, options[0])) + ';']
    for pattern in (r'#define SHADER_OPT\(opt\)[^\n]*', r'(?:constexpr|const) size_t SHADER_ID_SHIFT = \d+;'):
        found = re.findall(pattern, header)
        if len(found) != 1:
            raise ValueError('Expected exactly one interpreter.h match: ' + pattern)
        parts.append(found[0])
    return '\n'.join(parts)


def harness_source(interpreter, header, gbi='fast/lus_gbi.h',
                   options=('uint64_t cc_options = 0;', 'ColorCombinerKey key'), options_text=None):
    """Host key program from interpreter.cpp and interpreter.h text.

    options: anchors bounding GfxSpTri1's option block, which reads only mRdp's
    other modes, grayscale and loaded-texture flags and mShaderStack. options_text
    supplies that block from another interpreter.cpp when the projection restructured it."""
    signature = 'void Interpreter::GenerateCC(ColorCombiner* comb, const ColorCombinerKey& key) '
    generate = braced(interpreter, once(interpreter, signature))[len(signature):]
    source = interpreter if options_text is None else options_text
    start = once(source, options[0])
    end = source.index(options[1], start)
    block = source[start:end]
    # Line markers would point the host build at projected sources' origins.
    strip = lambda text: re.sub(r'^#line [^\n]*\n', '', text, flags=re.M)
    return (HARNESS.replace('@GBI@', gbi).replace('@DECLARATIONS@', strip(declarations(header)))
            .replace('@GENERATE@', strip(generate)).replace('@OPTIONS@', strip(block)))


def build_harness(source, includes, folder, defines=('F3DEX_GBI_2',)):
    folder.mkdir(parents=True, exist_ok=True)
    program = folder / 'shader_keys.cpp'
    program.write_text(source, encoding='utf-8')
    executable = folder / 'shader_keys.exe'
    host_toolchain.run([['cl', '/nologo', '/std:c++20', '/EHsc', '/O1', '/MD'] + ['/D' + d for d in defines] +
                        ['/I' + str(i) for i in includes] + [program, '/Fe:' + str(executable)]], folder,
                       'shader-keys')
    return executable


def clamp_masks(used, first_tile, tiles):
    """Possible clamp masks (tm). A clamped axis without mirroring may or may
    not clamp depending on sizes; unknown tile modes yield both outcomes. Slot 1
    may sample the first tile again (LUS: no LOD with first tile >= 2)."""
    masks = {0}
    for i in range(2):
        if not used[i]:
            continue
        candidates = {(first_tile + i) & 7} | ({first_tile & 7} if i and first_tile >= 2 else set())
        for axis in range(2):
            bit = 1 << (2 * i + axis)
            choices = set()
            for tile in candidates:
                mode = tiles.get(tile)
                if mode is None:
                    choices |= {0, bit}
                elif not mode[axis] & G_TX_CLAMP:
                    choices.add(0)
                elif mode[axis] & G_TX_MIRROR:
                    choices.add(bit)
                else:
                    choices |= {0, bit}
            masks = {m | choice for m in masks for choice in choices}
    return masks


def derive(executable, states):
    """(shaderId0, shaderId1) pairs for resolved harvest states."""
    states = list(states)
    queries = sorted({(rect_combine(kind, combine, mode), mode, gray)
                      for kind, combine, mode, gray, _, _ in states})
    text = ''.join('%x %x %x\n' % (c, m, 1 if g else 0) for c, m, g in queries)
    result = subprocess.run([str(executable)], input=text, capture_output=True, text=True, check=True)
    lines = result.stdout.split('\n')
    clamp = int(lines[0], 16)
    answers = {}
    for query, line in zip(queries, lines[1:]):
        id0, id1, used0, used1 = line.split()
        answers[query] = (int(id0, 16), int(id1, 16), used0 == '1', used1 == '1')
    if len(answers) != len(queries):
        raise RuntimeError('shader key harness answered %d of %d queries' % (len(answers), len(queries)))
    keys = set()
    for kind, combine, mode, gray, first, tiles in states:
        id0, id1, used0, used1 = answers[(rect_combine(kind, combine, mode), mode, gray)]
        for tm in clamp_masks((used0, used1), first, dict(tiles)):
            keys.add((id0, id1 | tm * clamp))
    return keys
