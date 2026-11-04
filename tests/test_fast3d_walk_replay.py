"""Walk-once interpolation batches: the log contract and the projection's anchors."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
sys.path.insert(0, str(ROOT / 'tests'))
from xbox360_platform import fast3d_walk_replay as walk
from test_renderer_replay import compile_run


class WalkReplayTests(unittest.TestCase):
    def test_log_streams_and_texture_hazards(self):
        compile_run(self, (ROOT / 'tests/walk_replay_contract.cpp').read_text())

    def test_header_members_follow_interpolation_factor(self):
        text = walk.header('class Interpreter {\n    float mInterpolationT = 1.0f;\n};\n')
        self.assertTrue(text.startswith('#include "fast3d/walk_replay.h"\n'))
        self.assertIn('void XboxBeginWalkBatch(', text)
        with self.assertRaises(ValueError):
            walk.header('class Interpreter {};\n')


if __name__ == '__main__':
    unittest.main()
