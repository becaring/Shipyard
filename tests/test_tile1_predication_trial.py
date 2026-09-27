import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest

from tests.test_draw_preparation import function
from tests.test_tile_coverage import clipped_to_view, f32

ROOT = Path(__file__).resolve().parents[1]


def touches_first_tile(vertices, viewport_y, viewport_height, split_y):
    for offset in range(0, len(vertices), 3):
        for vertex in clipped_to_view(vertices[offset:offset + 3]):
            if viewport_y + (1.0 - vertex[1] / vertex[3]) * viewport_height / 2 <= split_y:
                return True
    return False


class Tile1PredicationTrialTests(unittest.TestCase):
    def _compile(self, source):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        folder = tempfile.TemporaryDirectory(prefix='shipyard-tile1-predication-')
        out = Path(folder.name)
        (out / 'trial.cpp').write_text(source)
        batch = out / 'compile.cmd'
        batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                         'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"' +
                         str(ROOT / 'sources/xenos') + '" trial.cpp /Fetrial.exe\nexit /b %errorlevel%\n')
        built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out,
                               capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        return folder, out / 'trial.exe'

    def test_exact_target_rejections_and_draw_state_order(self):
        program = r'''
#include <assert.h>
#include <vector>
typedef long HRESULT;
#define D3DPT_TRIANGLELIST 4
#define D3DPRED_TILE(i) (1u << (i))
#include "tile1_predication_trial.h"
using namespace Fast::ShipyardXenos;
struct Device {
    std::vector<unsigned> calls;
    HRESULT result;
    void SetPredication(unsigned mask) { calls.push_back(10 + mask); }
    HRESULT DrawPrimitiveUP(unsigned, unsigned, const void*, unsigned) {
        calls.push_back(20); return result;
    }
};
int main() {
    Tile1PredicationTrialCounters counts = {};
    const float below[] = {-0.2f,-0.5f,0.5f,1, 0.2f,-0.5f,0.5f,1, 0,-0.8f,0.5f,1};
    const float straddle[] = {-0.2f,0.5f,0.5f,1, 0.2f,-0.5f,0.5f,1, 0,-0.8f,0.5f,1};
    assert(!ClassifyTile1Predication(false,below,3,4,1080,0,1080,640,&counts));
    assert(!ClassifyTile1Predication(true,below,3,4,1080,1081,100,640,&counts));
    assert(!ClassifyTile1Predication(true,straddle,3,4,1080,0,1080,640,&counts));
    assert(ClassifyTile1Predication(true,below,3,4,1080,0,1080,640,&counts));
    assert(counts.candidates==4 && counts.targetRejected==1);
    assert(counts.viewportRejected==1 && counts.geometryRejected==1 && counts.eligible==1);
    Device d; d.result=-7;
    assert(SubmitTile1PredicatedDraw(&d,true,1,below,16,&counts)==-7);
    assert(d.calls.size()==3 && d.calls[0]==12 && d.calls[1]==20 && d.calls[2]==10);
    assert(counts.applied==1);
    d.calls.clear();d.result=0;
    assert(SubmitTile1PredicatedDraw(&d,false,1,below,16,&counts)==0);
    assert(d.calls.size()==1 && d.calls[0]==20 && counts.applied==1);
}
'''
        folder, executable = self._compile(program)
        try:
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        finally:
            folder.cleanup()

    def test_certified_batches_never_touch_tile0_after_clipping(self):
        program = r'''
#include <iostream>
typedef long HRESULT;
#define D3DPT_TRIANGLELIST 4
#define D3DPRED_TILE(i) (1u << (i))
#include "tile1_predication_trial.h"
using namespace Fast::ShipyardXenos;
int main() {
    unsigned n;std::cin>>n;
    for(unsigned i=0;i<n;++i) {
        unsigned viewportY,viewportHeight,splitY;
        std::cin>>viewportY>>viewportHeight>>splitY;
        float vertices[12];for(unsigned j=0;j<12;++j)std::cin>>vertices[j];
        Tile1PredicationTrialCounters counts={};
        std::cout<<ClassifyTile1Predication(true,vertices,3,4,1080,
            viewportY,viewportHeight,splitY,&counts)<<'\n';
    }
}
'''
        folder, executable = self._compile(program)
        try:
            rng = random.Random(3601)
            cases = []
            lines = ['3000']
            for i in range(3000):
                viewport_y = rng.choice((0, 16, 48))
                viewport_h = rng.choice((720, 900, 1080 - viewport_y))
                split_y = viewport_y + rng.randrange(1, viewport_h)
                vertices = []
                for _ in range(3):
                    w = f32(rng.uniform(0.1, 4))
                    y_px = split_y + (rng.uniform(3, 200) if i % 3 == 0 else rng.uniform(-30, 30))
                    vertices.append((f32(rng.uniform(-1.5, 1.5) * w),
                                     f32((1 - 2 * (y_px - viewport_y) / viewport_h) * w),
                                     f32(0.5 * w), w))
                cases.append((viewport_y, viewport_h, split_y, vertices))
                lines.append(f'{viewport_y} {viewport_h} {split_y} ' +
                             ' '.join(str(v) for vertex in vertices for v in vertex))
            run = subprocess.run([str(executable)], input='\n'.join(lines) + '\n',
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            results = [int(v) for v in run.stdout.split()]
            self.assertEqual(len(results), len(cases))
            self.assertGreater(sum(results), 100)
            for result, (y, h, split, vertices) in zip(results, cases):
                if result:
                    self.assertFalse(touches_first_tile(vertices, y, h, split))
        finally:
            folder.cleanup()

    def test_predication_wraps_draw_only_and_is_default_off(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        draw = function(source, 'void Draw(float* vertices, size_t count, size_t triangles) {')
        self.assertLess(draw.index('PrepareDrawState();'), draw.index('SubmitTile1PredicatedDraw('))
        self.assertLess(draw.index('ApplyAlpha();'), draw.index('SubmitTile1PredicatedDraw('))
        self.assertIn('if (timing_enabled || tile1_predication_trial_enabled)', draw)
        self.assertIn('ClassifyTile1Predication(', draw)
        self.assertIn('Shipyard_Check(drawResult, "Shipyard DrawPrimitiveUP")', draw)
        self.assertNotIn('SetPredication(', draw)


if __name__ == '__main__':
    unittest.main()
