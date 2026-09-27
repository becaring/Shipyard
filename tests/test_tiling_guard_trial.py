import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.test_draw_preparation import function

ROOT = Path(__file__).resolve().parents[1]


class TilingGuardTrialTests(unittest.TestCase):
    def test_conservative_decision_and_post_draw_accounting(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        program = r'''
#include <assert.h>
#include "tiling_guard_trial.h"
using namespace Fast::ShipyardXenos;
int main() {
    TilingGuardTrialWindow w;
    const unsigned capacity = 8*1024*1024 - 4;
    const unsigned reserve = 4*1024*1024 + 64*1024;
    w.Begin(264, capacity-264);
    assert(w.active);
    assert(w.BeforeDraw(100000, capacity-100000, 65536) == GuardTrialContinue);
    assert(w.AfterDraw(101000, capacity-101000));
    assert(w.stats.continued == 1 && w.stats.postQueries == 1);
    assert(w.stats.maxDrawBytes == 1000 && w.stats.totalDrawBytes == 1000);
    assert(w.BeforeDraw(101000, capacity-101000, 256*1024+1) == GuardTrialOversize);
    assert(w.BeforeDraw(101000, capacity-101000, 256*1024) == GuardTrialContinue);
    assert(w.AfterDraw(102000, capacity-102000));
    assert(w.BeforeDraw(102000, capacity-102000+4, 1) == GuardTrialCapacity);
    assert(w.stats.capacityFallbacks == 1);
    w.Begin(264, capacity-264);
    assert(w.BeforeDraw(200, capacity-200, 1) == GuardTrialCursor);
    assert(w.stats.cursorFallbacks == 1);
    w.Begin(264, capacity-264);
    assert(w.BeforeDraw(capacity-reserve, reserve, 1) == GuardTrialSpace);
    assert(w.stats.spaceFallbacks == 1);
    w.Begin(264, capacity-264);
    assert(w.BeforeDraw(capacity-reserve-1, reserve+1, 1) == GuardTrialContinue);
    assert(!w.AfterDraw(capacity-reserve+2, reserve-2));
    assert(w.stats.postFallbacks == 1 && !w.active);
    w.Begin(264, capacity-264);
    assert(w.BeforeDraw(300, capacity-300, 1) == GuardTrialContinue);
    assert(!w.AfterDraw(200, capacity-200));
    assert(w.stats.postCursorRegressions == 1 && !w.active);
    w.Begin(264, capacity-264);
    assert(w.BeforeDraw(300, capacity-300, 1) == GuardTrialContinue);
    assert(!w.AfterDraw(400, capacity-400+4));
    assert(w.stats.postCapacityChanges == 1 && !w.active);
    w.Begin(264, 12345);
    assert(!w.active && w.stats.beginCapacityRejected == 1);
    w.Begin(1, TilingGuardTrialWindow::ExpectedCapacity -
               TilingGuardTrialWindow::CapacityTolerance - 2);
    assert(!w.active);
    w.Begin(1, TilingGuardTrialWindow::ExpectedCapacity -
               TilingGuardTrialWindow::CapacityTolerance - 1);
    assert(w.active);
    w.Begin(1, TilingGuardTrialWindow::ExpectedCapacity +
               TilingGuardTrialWindow::CapacityTolerance - 1);
    assert(w.active);
    w.Begin(1, TilingGuardTrialWindow::ExpectedCapacity +
               TilingGuardTrialWindow::CapacityTolerance);
    assert(!w.active);
    w.ResetStats();
    assert(!w.stats.continued && !w.stats.beginCapacityRejected);
}
'''
        with tempfile.TemporaryDirectory(prefix='shipyard-tiling-guard-') as folder:
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

    def test_trial_is_opt_in_and_keeps_original_guard_and_endtiling(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        draw = function(source, 'void Draw(float* vertices, size_t count, size_t triangles) {')
        self.assertIn('tiling_draw_bytes + count * sizeof(float) + 8192 > 2 * 1024 * 1024', draw)
        self.assertLess(draw.index('GuardTrialBeforeDraw('), draw.index('PrepareDrawState();'))
        self.assertLess(draw.index('SubmitTile1PredicatedDraw('), draw.index('GuardTrialAfterDraw('))
        self.assertIn('FinishFramebuffer();', draw)
        finish = function(source, 'void FinishFramebuffer(bool preserveDepth, bool preserveColor) {')
        self.assertIn('Shipyard_Check(xenos_device->EndTiling(', finish)
        self.assertLess(finish.index('EndTiling('), finish.index('tiling_guard_trial_window.Close();'))
        self.assertIn('if (!tiling_guard_trial_enabled) return false;', source)
        self.assertIn('b.width == 1920 && b.height == 1080 &&', source)
        self.assertIn('b.tiles.size() == 2 && b.samples == 1', source)


if __name__ == '__main__':
    unittest.main()
