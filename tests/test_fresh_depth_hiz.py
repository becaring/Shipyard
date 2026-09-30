from xenos_source import core_source
"""Check the production HiZ decision and the clear/restore state boundary."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FreshDepthHiZTests(unittest.TestCase):
    def test_capability_and_fresh_depth_truth_table(self):
        source = core_source(DEFERRED_FRAMEBUFFERS=1)
        helpers = source[source.index('bool FramebufferHiZCapable('):source.index('void BeginFramebuffer() {')]
        program = r'''
#include <assert.h>
#include <stdint.h>
typedef unsigned UINT;
enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_NONE, D3DMULTISAMPLE_2_SAMPLES,
                            D3DMULTISAMPLE_4_SAMPLES };
#define GPU_HIERARCHICAL_Z_TILES 3600
UINT XGHierarchicalZSize(UINT width, UINT height, D3DMULTISAMPLE_TYPE samples) {
    return width * height * (samples == D3DMULTISAMPLE_4_SAMPLES ? 4 :
                             samples == D3DMULTISAMPLE_2_SAMPLES ? 2 : 1) / 512;
}
__HELPERS__
int main() {
    assert(FramebufferHiZCapable(true, 1920, 672, D3DMULTISAMPLE_NONE));
    assert(!FramebufferHiZCapable(false, 1920, 672, D3DMULTISAMPLE_NONE));
    assert(!FramebufferHiZCapable(true, 1920, 1080, D3DMULTISAMPLE_NONE));
    assert(!FramebufferHiZCapable(true, 1920, 672, D3DMULTISAMPLE_4_SAMPLES));
    assert(FreshFramebufferHiZ(true, true, false));
    assert(!FreshFramebufferHiZ(false, true, false));
    assert(!FreshFramebufferHiZ(true, false, false));
    assert(!FreshFramebufferHiZ(true, true, true));
}
'''.replace('__HELPERS__', helpers)
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-fresh-hiz-') as folder:
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

    def test_begin_sets_hiz_after_clear_and_restoration(self):
        source = core_source(DEFERRED_FRAMEBUFFERS=1)
        begin = source[source.index('void BeginFramebuffer() {'):source.index('void RebindSampledTextures() {')]
        self.assertLess(begin.index('const bool fresh_hiz = FreshFramebufferHiZ('),
                        begin.index('BeginTiling('))
        self.assertLess(begin.index('EndRestoreQuad(restore_constants);'),
                        begin.rindex('SetRenderState(D3DRS_HIZENABLE,'))
        self.assertIn('fresh_hiz ? D3DHIZ_AUTOMATIC : D3DHIZ_DISABLE', begin)
        self.assertEqual(begin.count('fresh_hiz ? D3DHIZ_AUTOMATIC : D3DHIZ_DISABLE'), 2)
        self.assertIn('fresh_hiz ? D3DHIZ_ENABLE : D3DHIZ_DISABLE', begin)


if __name__ == '__main__':
    unittest.main()
