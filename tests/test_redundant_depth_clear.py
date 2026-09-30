from xenos_source import core_source
"""Exercise the production Clear decision with single and tiled framebuffer states."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RedundantDepthClearTests(unittest.TestCase):
    def test_depth_clear_at_fresh_begin_is_not_submitted_twice(self):
        source = core_source(DEFERRED_FRAMEBUFFERS=1)
        begin = source.index('void Clear(bool color, bool depth) {')
        clear = source[begin:source.index('\nvoid ResolveMSAA(', begin)]
        program = r'''
#include <assert.h>
#include <vector>
typedef unsigned DWORD;
enum { D3DCLEAR_TARGET = 1, D3DCLEAR_ZBUFFER = 2, D3DCLEAR_STENCIL = 4 };
struct Framebuffer {
    bool stored, depth_stored;
    int depth;
    unsigned samples;
    std::vector<int> tiles;
};
std::vector<Framebuffer> buffers(1);
int active_fb = 0;
bool live_framebuffer;
unsigned framebuffer_clear_count, framebuffer_restore_avoided;
unsigned framebuffer_depth_restore_avoided;
unsigned auto_depth_clears, color_restores, depth_restores;
unsigned explicit_clears;
DWORD explicit_flags;
void BeginFramebuffer() {
    if (live_framebuffer) return;
    Framebuffer& b = buffers[active_fb];
    // SDK BeginTiling(flags=0) clears every tile; the single-tile path clears
    // depth when the retained depth has been discarded.
    if (b.tiles.size() > 1 || !b.stored || (b.depth && !b.depth_stored))
        if (b.depth) ++auto_depth_clears;
    if (b.stored) {
        ++color_restores;
        if (b.depth_stored) ++depth_restores;
    }
    live_framebuffer = true;
}
void ClearFramebufferTargets(DWORD flags) {
    ++explicit_clears;
    explicit_flags = flags;
}
__CLEAR_FUNCTION__
void reset(bool tiled, bool live) {
    Framebuffer& b = buffers[0];
    b.stored = b.depth_stored = true;
    b.depth = 1;
    b.samples = 1;
    b.tiles.assign(tiled ? 2 : 1, 0);
    live_framebuffer = live;
    framebuffer_clear_count = framebuffer_restore_avoided = 0;
    framebuffer_depth_restore_avoided = 0;
    auto_depth_clears = color_restores = depth_restores = 0;
    explicit_clears = explicit_flags = 0;
}
int main() {
    for (int tiled = 0; tiled != 2; ++tiled) {
        reset(tiled != 0, false);
        Clear(false, true);
        assert(framebuffer_clear_count == 1);
        assert(auto_depth_clears == 1 && explicit_clears == 0);
        assert(color_restores == 1 && depth_restores == 0);
        assert(buffers[0].stored && !buffers[0].depth_stored);
        assert(framebuffer_depth_restore_avoided == 1);

        reset(tiled != 0, true);
        Clear(false, true);
        assert(auto_depth_clears == 0 && explicit_clears == 1);
        assert(explicit_flags == (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL));

        reset(tiled != 0, false);
        Clear(true, false);
        assert(color_restores == 1 && depth_restores == 1);
        assert(explicit_clears == 1 && explicit_flags == D3DCLEAR_TARGET);
        assert(buffers[0].depth_stored);

        reset(tiled != 0, false);
        Clear(true, true);
        assert(auto_depth_clears == 1 && color_restores == 0);
        assert(explicit_clears == (tiled ? 1u : 0u));
    }
}
'''.replace('__CLEAR_FUNCTION__', clear)
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-depth-clear-') as folder:
            out = Path(folder)
            (out / 'test.cpp').write_text(program)
            batch = out / 'compile.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT test.cpp /Fecontract.exe\n'
                'exit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out,
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out / 'contract.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
