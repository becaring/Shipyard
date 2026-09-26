"""Presentation depth is discarded only when explicitly requested for End()."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'sources/xenos/core.cpp'
API = ROOT / 'sources/xenos/depth_discard.h'


class PresentationDepthDiscardTests(unittest.TestCase):
    def test_end_consumes_hint_and_preserves_nonpresentation_depth(self):
        core = CORE.read_text(encoding='utf-8')
        end = core.split('void End() {', 1)[1].split('\nvoid Finish()', 1)[0]
        self.assertIn('DiscardPresentationDepthOnEnd', API.read_text(encoding='utf-8'))
        self.assertIn('discard_presentation_depth_on_end = false;', end)
        self.assertIn('active_fb == 0', end)
        self.assertIn('FinishFramebuffer(!discardPresentationDepth)', end)
        self.assertIn('FinishFramebuffer();', end)
        self.assertLess(end.index('discard_presentation_depth_on_end = false;'),
                        end.index('BeginFramebuffer();'))


if __name__ == '__main__':
    unittest.main()
