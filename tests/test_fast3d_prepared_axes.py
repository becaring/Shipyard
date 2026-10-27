"""Prepared UV axes: reconciliation capture and member anchors."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'python'))
from xbox360_platform import fast3d_prepared_axes as axes


class PreparedAxisTests(unittest.TestCase):
    def test_capture_precedes_key_and_uses_consumer_clamp(self):
        text = axes.capture('bool Reconcile() {\n    mXboxPipelineKey.Capture(*this,p);\n}\n',
                            '    mXboxPipelineKey.Capture(*this,p);',
                            clamp_s='(boundWidth[i]-0.5f)/width[i]', clamp_t='(boundHeight[i]-0.5f)/height[i]')
        self.assertLess(text.index('mXboxAxisS[i].Prepare('), text.index('mXboxPipelineKey.Capture'))
        self.assertIn('if (!comb->usedTextures[i]) continue;', text)
        self.assertIn('mXboxClampS[i] = (boundWidth[i]-0.5f)/width[i];', text)
        self.assertIn('mXboxClampT[i] = (boundHeight[i]-0.5f)/height[i];', text)
        header = axes.header('class Interpreter {\n    int mResult;\n};', '    int mResult;')
        self.assertTrue(header.startswith('#include "fast3d/coordinate.h"\n'))
        self.assertIn('    int mResult;\n' + axes.MEMBERS, header)
        with self.assertRaises(ValueError):
            axes.capture('bool Reconcile() {\n}\n', '    mXboxPipelineKey.Capture(*this,p);')


if __name__ == '__main__':
    unittest.main()
