from xenos_source import core_source
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FramebufferLazyResolveTests(unittest.TestCase):
    def test_read_only_image_does_not_reopen_its_target(self):
        source = core_source(DEFERRED_FRAMEBUFFERS=1)
        resolve = source.split('void Resolve(int id) {', 1)[1].split('\n}', 1)[0]
        self.assertIn('FinishFramebuffer();', resolve)
        self.assertNotIn('BeginFramebuffer();', resolve)

    def test_end_preserves_resolved_image_without_an_extra_restore(self):
        source = core_source(DEFERRED_FRAMEBUFFERS=1)
        end = source.split('void End() {', 1)[1].split('\n}', 1)[0]
        self.assertIn('if (live_framebuffer || pending_game_depth_clear == active_fb || !buffers[active_fb].stored)', end)
        self.assertIn('if (active_fb != 0)', end)
        self.assertIn('active_fb = 0;', end)
        self.assertIn('BeginFramebuffer();\n        FinishFramebuffer(!discardPresentationDepth', end)
        self.assertIn('buffers[0].depth_stored = false;', end)
        self.assertIn('buffers[0].stored = false;', end)


if __name__ == '__main__':
    unittest.main()
