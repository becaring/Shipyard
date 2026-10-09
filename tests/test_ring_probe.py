import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.test_draw_preparation import function

ROOT = Path(__file__).resolve().parents[1]


class RingProbeTests(unittest.TestCase):
    def test_integer_address_delta_and_ambiguous_segments(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        program = r'''
#include <assert.h>
#include "ring_probe.h"
using Fast::ShipyardXenos::RingCursor;
using Fast::ShipyardXenos::SameRingSegmentDelta;
int main() {
    size_t bytes = 999;
    assert(SameRingSegmentDelta(RingCursor(0x1000,0x4000),
                                RingCursor(0x1200,0x4000),0x4000,&bytes));
    assert(bytes == 0x200);
    bytes = 999;
    assert(!SameRingSegmentDelta(RingCursor(0x1000,0x4000),
                                 RingCursor(0x1200,0x5000),0x4000,&bytes));
    assert(bytes == 999);
    assert(!SameRingSegmentDelta(RingCursor(0x3000,0x4000),
                                 RingCursor(0x1000,0x4000),0x4000,&bytes));
    assert(!SameRingSegmentDelta(RingCursor(0x1000,0x4000),
                                 RingCursor(0x3000,0x4000),0x1000,&bytes));
    assert(!SameRingSegmentDelta(RingCursor(0x1000,0x2000),
                                 RingCursor(0x3000,0x2000),0x4000,&bytes));
    assert(!SameRingSegmentDelta(RingCursor(0,0x4000),
                                 RingCursor(0x1200,0x4000),0x4000,&bytes));
    assert(!SameRingSegmentDelta(RingCursor(0x1000,0x4000),
                                 RingCursor(0x1200,0x4000),0x4000,0));
}
'''
        with tempfile.TemporaryDirectory(prefix='shipyard-ring-probe-') as folder:
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

    def test_recording_is_profile_only_and_includes_draw_preparation(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        draw = function(source, 'void Draw(float* vertices,')
        self.assertLess(draw.index('probeRing = timing_enabled'), draw.index('RingCursor ringBefore'))
        self.assertLess(draw.index('RingCursor ringBefore'), draw.index('PrepareDrawState();'))
        self.assertLess(draw.index('ApplyAlpha();'), draw.index('SubmitTile1PredicatedDraw('))
        self.assertLess(draw.index('SubmitTile1PredicatedDraw('), draw.index('RingCursor ringAfter'))
        self.assertLess(draw.index('if (probeRing) {'), draw.index('RingCursor ringAfter'))


if __name__ == '__main__':
    unittest.main()
