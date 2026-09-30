import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RestoreBatchTests(unittest.TestCase):
    def test_msaa_samples_share_one_restore_state_block(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        begin = source.split('void BeginFramebuffer() {', 1)[1].split('\nvoid RebindSampledTextures()', 1)[0]
        self.assertEqual(begin.count('BeginRestoreQuad('), 1)
        self.assertEqual(begin.count('EndRestoreQuad('), 1)
        self.assertIn('DrawRestoreQuadSample(', begin)
        self.assertNotIn('    RestoreQuad(', begin)
        self.assertLess(begin.index('BeginRestoreQuad('), begin.index('for (uint32_t sample'))
        self.assertLess(begin.index('for (uint32_t sample'), begin.index('EndRestoreQuad('))

    def test_single_quad_call_still_owns_state_restore(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        wrapper = source.split('void RestoreQuad(IDirect3DTexture9* color', 1)[1]
        wrapper = wrapper.split('\nvoid ClearFramebufferTargets(', 1)[0]
        self.assertIn('BeginRestoreQuad(', wrapper)
        self.assertIn('DrawRestoreQuadSample(', wrapper)
        self.assertIn('EndRestoreQuad(', wrapper)


if __name__ == '__main__':
    unittest.main()
