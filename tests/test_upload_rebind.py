from xenos_source import core_source
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UploadRebindTests(unittest.TestCase):
    def test_unchanged_upload_keeps_native_bindings_without_a_transition(self):
        source = core_source(BINDING_REUSE=1)
        upload = source.split('void Upload(const uint8_t* bytes, uint32_t width, uint32_t height) {', 1)[1]
        unchanged = upload.split('if (size <= UPLOAD_SNAPSHOT_BYTES', 1)[1].split('    snapshot.native = NULL;', 1)[0]
        self.assertIn('const bool framebuffer_transition = selected_fb[upload_tile] >= 0;', unchanged)
        self.assertIn('if (framebuffer_transition)', unchanged)
        self.assertIn('SetNativeTexture(upload_tile, tex.native)', unchanged)
        self.assertIn('if (verify_reuse)', unchanged)
        self.assertEqual(unchanged.count('RebindSampledTextures();'), 1)
        self.assertLess(unchanged.index('if (verify_reuse)'), unchanged.index('RebindSampledTextures();'))


if __name__ == '__main__':
    unittest.main()
