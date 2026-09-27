import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.test_draw_preparation import function

ROOT = Path(__file__).resolve().parents[1]


class TilingSpaceProbeTests(unittest.TestCase):
    def test_window_accounting_and_ambiguous_capacity(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        program = r'''
#include <assert.h>
#include "tiling_space_probe.h"
using namespace Fast::ShipyardXenos;
int main() {
    TilingSpaceProbe p;
    p.Begin(100, 900);
    p.Setup(200, 800);
    p.BeforeResolve(700, 300, true);
    p.BeforeEnd(850, 150);
    assert(p.summary.brackets == 1 && p.summary.guardFinishes == 1);
    assert(p.summary.normalFinishes == 0 && p.summary.querySamples == 4);
    assert(p.summary.maxUsed[0] == 100 && p.summary.maxUsed[1] == 200);
    assert(p.summary.maxUsed[2] == 700 && p.summary.maxUsed[3] == 850);
    assert(p.summary.minRemaining[0] == 900 && p.summary.minRemaining[3] == 150);
    assert(p.summary.maxSetupBytes == 100 && p.summary.maxFinishBytes == 150);
    assert(p.summary.capacityChanges == 0);
    p.Begin(50, 950);
    p.Setup(90, 910);
    p.BeforeResolve(500, 500, false);
    p.BeforeEnd(600, 500); // Different capacity: do not infer command bytes.
    assert(p.summary.brackets == 2 && p.summary.normalFinishes == 1);
    assert(p.summary.minRemaining[0] == 900 && p.summary.maxUsed[3] == 850);
    assert(p.summary.capacityChanges == 1 && p.summary.maxFinishBytes == 150);
    p.ResetSummary();
    assert(p.summary.brackets == 0 && p.summary.querySamples == 0);
    p.Begin(5, 995);
    p.Setup(10, 990);
    p.BeforeResolve(15, 985, false);
    p.BeforeEnd(20, 980);
    assert(p.summary.brackets == 1 && p.summary.normalFinishes == 1);
    assert(p.summary.minRemaining[0] == 995 && p.summary.maxUsed[3] == 20);
}
'''
        with tempfile.TemporaryDirectory(prefix='shipyard-tiling-space-') as folder:
            out = Path(folder)
            (out / 'probe.cpp').write_text(program)
            batch = out / 'compile.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                             'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"' +
                             str(ROOT / 'sources/xenos') + '" probe.cpp /Feprobe.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out,
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(out / 'probe.exe')], cwd=out, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_samples_surround_setup_and_finish_without_changing_guard(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        begin = function(source, 'void BeginFramebuffer() {')
        finish = function(source, 'void FinishFramebuffer(bool preserveDepth, bool preserveColor) {')
        draw = function(source, 'void Draw(float* vertices, size_t count, size_t triangles) {')
        self.assertLess(begin.index('BeginTiling('), begin.index('TilingSpaceBegin('))
        self.assertLess(begin.index('TilingSpaceBegin('), begin.index('RestoreQuad('))
        self.assertLess(begin.index('SetTileConstants();'), begin.index('TilingSpaceSetup('))
        self.assertLess(finish.index('TilingSpaceBeforeResolve('), finish.index('Resolve('))
        self.assertLess(finish.index('SetPredication(0);'), finish.index('TilingSpaceBeforeEnd('))
        self.assertLess(finish.index('TilingSpaceBeforeEnd('), finish.index('EndTiling('))
        self.assertIn('tiling_draw_bytes + count * sizeof(float) + 8192 > 2 * 1024 * 1024', draw)


if __name__ == '__main__':
    unittest.main()
