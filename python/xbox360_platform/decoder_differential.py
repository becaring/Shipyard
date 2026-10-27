"""Original-vs-projected Fast3D feature decoder differential (host build).

Each decoder source is self-contained: the shader enums, SHADER_OPT, CCFeatures
and gfx_cc_get_features of one view (pinned upstream, or the consumer's
projection). Both compile into one program in separate namespaces; CCFeatures
size, alignment and every member offset must match, and every option combination
below option_bits, with each combiner nibble and pseudo-random upper id bits,
must decode to identical bytes. shaderId1 is 64-bit.
"""

import re
import subprocess
from pathlib import Path

from . import host_toolchain

HARNESS = r'''#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
namespace original {
#include "decoder_original.inc"
}
namespace projected {
#include "decoder_projected.inc"
}
static_assert(sizeof(original::CCFeatures) == sizeof(projected::CCFeatures), "CCFeatures size drift");
static_assert(alignof(original::CCFeatures) == alignof(projected::CCFeatures), "CCFeatures alignment drift");
#define CHECK_MEMBER(member) \
    static_assert(offsetof(original::CCFeatures, member) == offsetof(projected::CCFeatures, member), \
                  "CCFeatures offset drift: " #member)
@MEMBERS@
int main() {
    uint64_t state = UINT64_C(0x762ae471db8c219f);
    unsigned long long cases = 0;
    const uint64_t optionMask = (UINT64_C(1) << @BITS@) - 1;
    for (uint64_t options = 0; options <= optionMask; ++options) {
        for (unsigned nibble = 0; nibble < 16; ++nibble) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            const uint64_t id0 = (options & 1) ? state : UINT64_C(0x1111111111111111) * nibble;
            const uint64_t id1 = options | ((state * UINT64_C(0x9e3779b97f4a7c15)) & ~optionMask);
            original::CCFeatures a;
            projected::CCFeatures b;
            memset(&a, 0xa5, sizeof(a));
            memset(&b, 0xa5, sizeof(b));
            original::gfx_cc_get_features(id0, id1, &a);
            projected::gfx_cc_get_features(id0, id1, &b);
            if (memcmp(&a, &b, sizeof(a)) != 0) {
                fprintf(stderr, "Decoder mismatch: id0=%016llx id1=%016llx\n", (unsigned long long)id0,
                        (unsigned long long)id1);
                return 1;
            }
            ++cases;
        }
    }
    printf("PASS %llu decoder differential cases; CCFeatures size=%u, layout identical\n", cases,
           (unsigned)sizeof(original::CCFeatures));
    return 0;
}
'''


def members(source):
    """CCFeatures member names, in declaration order."""
    match = re.search(r'struct CCFeatures\s*\{(.*?)\};', source, re.S)
    if not match:
        raise ValueError('CCFeatures definition not found')
    body = re.sub(r'/\*.*?\*/|//[^\n]*|^\s*#[^\n]*', ' ', match.group(1), flags=re.S | re.M)
    names = []
    for declaration in body.split(';'):
        declaration = re.sub(r'\[[^\]]*\]', '', declaration.split('=')[0]).strip()
        if not declaration:
            continue
        first, *rest = declaration.split(',')
        names += [re.findall(r'\w+', first)[-1]] + [re.findall(r'\w+', d)[-1] for d in rest]
    return names


def harness(original, option_bits):
    checks = '\n'.join('CHECK_MEMBER(%s);' % name for name in members(original))
    return HARNESS.replace('@MEMBERS@', checks).replace('@BITS@', str(int(option_bits)))


def run(original, projected, folder, option_bits, includes=()):
    """Build and run the differential; returns its output, raises on any drift."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'decoder_original.inc').write_text(original, encoding='utf-8')
    (folder / 'decoder_projected.inc').write_text(projected, encoding='utf-8')
    program = folder / 'decoder_differential.cpp'
    program.write_text(harness(original, option_bits), encoding='utf-8')
    executable = folder / 'decoder_differential.exe'
    host_toolchain.run([['cl', '/nologo', '/std:c++17', '/EHsc', '/MD', '/O2', '/I' + str(folder)] +
                        ['/I' + str(path) for path in includes] + [program, '/Fe:' + str(executable)]],
                       folder, 'decoder-differential')
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError('Decoder differential failed: ' + result.stdout + result.stderr)
    return result.stdout
