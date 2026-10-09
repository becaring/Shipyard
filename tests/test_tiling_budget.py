import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.test_draw_preparation import function

ROOT = Path(__file__).resolve().parents[1]


class TilingBudgetTests(unittest.TestCase):
    def test_capacity_payload_alignment_and_fallback(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-tiling-budget-') as folder:
            out = Path(folder)
            draw = function((ROOT / 'sources/xenos/core.cpp').read_text(),
                            'void Draw(float* vertices,')
            guards = re.findall(r'tiling_draw_bytes \+ inputBytes \+ 8192 > [^)]*', draw)
            self.assertEqual(len(guards), 2)
            self.assertEqual(guards[0], guards[1])
            (out / 'tiling_precheck.inc').write_text(
                'bool TilingPrecheck(size_t tiling_draw_bytes, size_t count) { const uint64_t inputBytes = uint64_t(count) * sizeof(float); return ' +
                guards[0] + '; }\n')
            batch = out / 'compile.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                             'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"' +
                             str(ROOT / 'sources/xenos') + '" ' +
                             '/I"' + str(out) + '" "' + str(ROOT / 'tests/tiling_budget_contract.cpp') + '" /Feprobe.exe\nexit /b %errorlevel%\n')
            original = batch.read_text()
            for enabled in (0, 1):
                with self.subTest(tiling_budget=enabled):
                    batch.write_text(original.replace(
                        'cl /nologo', f'cl /DSHIPYARD_XENOS_TILING_BUDGET={enabled} /nologo'))
                    built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out,
                                           capture_output=True, text=True)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    run = subprocess.run([str(out / 'probe.exe')], cwd=out, capture_output=True, text=True)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_lifetime_and_viewport_boundary_integration(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        begin = function(source, 'void BeginFramebuffer() {')
        finish = function(source, 'void FinishFramebuffer(bool preserveDepth, bool preserveColor) {')
        draw = function(source, 'void Draw(float* vertices,')
        restore = function(source, 'void DrawRestoreQuadSample(')
        self.assertLess(begin.index('TilingSpaceBegin();'), begin.index('DrawRestoreQuadSample('))
        self.assertLess(draw.index('GetViewport(&guardViewport)'), draw.index('FinishFramebuffer();'))
        self.assertLess(draw.index('BeginFramebuffer();'), draw.index('SetViewport(&guardViewport)'))
        fallback = draw[draw.index('if (trialNeedsFinish) {'):]
        self.assertLess(fallback.index('GetViewport('), fallback.index('FinishFramebuffer();'))
        self.assertLess(fallback.index('FinishFramebuffer();'), fallback.index('BeginFramebuffer();'))
        self.assertLess(fallback.index('BeginFramebuffer();'), fallback.index('SetViewport('))
        self.assertIn('tiling_budget.Close();', finish)
        self.assertIn('tiling_budget.ChargeVertices(sizeof(quad));', restore)
        self.assertLess(draw.index('tiling_budget.ChargeVertices('), draw.index('ContinueTilingAfterDraw()'))
        self.assertIn('b.tiles.size() == 2 && b.samples == 1;', source)


if __name__ == '__main__':
    unittest.main()
