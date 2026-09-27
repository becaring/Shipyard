"""Verify optional tiling code is absent from a consumer's native draw path."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_draw_preparation import function

ROOT = Path(__file__).resolve().parents[1]


def preprocess_draw(source, enabled):
    compiler = Path(os.environ.get('XEDK',
        r'C:\Program Files (x86)\Microsoft Xbox 360 SDK')) / 'bin/win32/cl.exe'
    if not compiler.is_file():
        raise unittest.SkipTest('Xbox preprocessor required')
    draw = function(source, 'void Draw(float* vertices,')
    with tempfile.TemporaryDirectory(prefix='shipyard-tiling-selection-') as folder:
        path = Path(folder) / 'draw.cpp'
        path.write_text(draw)
        result = subprocess.run([str(compiler), '/nologo', '/EP', '/TP',
            '/DSHIPYARD_XENOS_TILING_TRIALS=' + str(enabled), str(path)],
            capture_output=True, text=True, check=True)
    return result.stdout


class TilingTrialSelectionTests(unittest.TestCase):
    def test_normal_build_has_no_trial_calls_or_temporaries(self):
        draw = preprocess_draw((ROOT / 'sources/xenos/core.cpp').read_text(), 0)
        for name in ('GuardTrialBeforeDraw', 'GuardTrialAfterDraw',
                     'ClassifyTile1Predication', 'SubmitTile1PredicatedDraw',
                     'trialContinued', 'tile1Only', 'trialNeedsFinish'):
            self.assertNotIn(name, draw)
        self.assertEqual(draw.count('DrawPrimitiveUP('), 1)
        self.assertLess(draw.index('BeginFramebuffer();'), draw.index('PrepareDrawState();'))
        self.assertIn('depth_clip.Apply(xenos_device);', draw)

    def test_trial_build_retains_runtime_selection(self):
        draw = preprocess_draw((ROOT / 'sources/xenos/core.cpp').read_text(), 1)
        for name in ('GuardTrialBeforeDraw', 'GuardTrialAfterDraw',
                     'ClassifyTile1Predication', 'SubmitTile1PredicatedDraw'):
            self.assertIn(name, draw)


if __name__ == '__main__':
    unittest.main()
