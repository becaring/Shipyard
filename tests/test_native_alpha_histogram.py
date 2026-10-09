import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NativeAlphaHistogramTests(unittest.TestCase):
    def test_classification_and_interval_reset(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        program = r'''
#include <assert.h>
#include "alpha_mode_histogram.h"
using namespace Fast::ShipyardXenos;
int main() {
    for (unsigned flags = 0; flags != 16; ++flags) {
        const bool alpha = (flags & 1) != 0;
        const bool edge = (flags & 2) != 0;
        const bool noise = (flags & 4) != 0;
        const bool invisible = (flags & 8) != 0;
        assert(ClassifyNativeAlphaMode(false, alpha, edge, noise, invisible) == 0);
        const unsigned expected = alpha && edge && !noise && !invisible ? 2 : 1;
        assert(ClassifyNativeAlphaMode(true, alpha, edge, noise, invisible) == expected);
    }
    NativeAlphaHistogram histogram = {};
    histogram.Record(0, 2);
    histogram.Record(1, 3);
    histogram.Record(1, 4);
    histogram.Record(2, 5);
    histogram.Record(3, 99);
    unsigned draws[3], triangles[3], vertices[3];
    histogram.CopyAndReset(draws, triangles, vertices);
    assert(draws[0] == 1 && draws[1] == 2 && draws[2] == 1);
    assert(triangles[0] == 2 && triangles[1] == 7 && triangles[2] == 5);
    assert(vertices[0] == 6 && vertices[1] == 21 && vertices[2] == 15);
    histogram.CopyAndReset(draws, triangles, vertices);
    for (unsigned i = 0; i < 3; ++i)
        assert(!draws[i] && !triangles[i] && !vertices[i]);
    histogram.Record(2, 1);
    histogram.CopyAndReset(draws, triangles, vertices);
    assert(draws[2] == 1 && triangles[2] == 1 && vertices[2] == 3);
}
'''
        with tempfile.TemporaryDirectory(prefix='shipyard-alpha-') as folder:
            out = Path(folder)
            (out / 'test.cpp').write_text(program)
            batch = out / 'compile.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"' +
                str(ROOT / 'sources/xenos') + '" test.cpp /Fecontract.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out,
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out / 'contract.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_profile_only_submitted_draw_accounting(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        draw = source[source.index('void Draw(float* vertices,'):]
        draw = draw[:draw.index('\nvoid ', 1)]
        checked = draw.index('Shipyard_Check(drawResult, indices ? "Shipyard DrawIndexedPrimitiveUP" : "Shipyard DrawPrimitiveUP");')
        recorded = draw.index('alpha_histogram.Record(')
        self.assertLess(checked, recorded)
        self.assertIn('if (SHIPYARD_XENOS_ALPHA_HISTOGRAM && timing_enabled)\n        SHIPYARD_DIAGNOSTIC(alpha_histogram.Record(', draw[checked:recorded + 60])
        capture = source[source.index('void CaptureAndResetCounters(NativeCounters* counters) {'):]
        self.assertIn('alpha_histogram.CopyAndReset(counters->alphaDraws, counters->alphaTriangles,',
                      capture)


if __name__ == '__main__':
    unittest.main()
