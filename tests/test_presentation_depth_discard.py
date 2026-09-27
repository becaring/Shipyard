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
        self.assertIn('FinishFramebuffer(!discardPresentationDepth, !canDiscardPresentationColor)', end)
        self.assertIn('FinishFramebuffer();', end)
        self.assertLess(end.index('discard_presentation_depth_on_end = false;'),
                        end.index('BeginFramebuffer();'))

    def test_color_discard_requires_direct_unpublished_unsampled_presentation(self):
        core = CORE.read_text(encoding='utf-8')
        end = core.split('void End() {', 1)[1].split('\nvoid Finish()', 1)[0]
        self.assertIn('DiscardPresentationColorOnEnd', API.read_text(encoding='utf-8'))
        for guard in ('discardPresentationDepth', 'buffers[0].color == presentation_color',
                      'buffers[0].tiles.size() == 1', '!presentation_image_ever_published',
                      'selected_fb[binding] == 0'):
            self.assertIn(guard, end)
        self.assertLess(end.index('discard_presentation_color_on_end = false;'),
                        end.index('BeginFramebuffer();'))
        self.assertIn('b.stored = preserveColor;', core)


if __name__ == '__main__':
    unittest.main()
