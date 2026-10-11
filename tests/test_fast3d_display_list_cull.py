import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.fast3d_display_list_cull import ORIGINAL, MARKER, transform

SOURCE = 'bool gfx_cull_dl_handler_f3d(F3DGfx** cmd) {}\n' + ORIGINAL + '\nbool next() {}\n'


class DisplayListCullTests(unittest.TestCase):
    def test_replaces_stub_once(self):
        result = transform(SOURCE)
        self.assertNotIn('// TODO:', result)
        self.assertEqual(result.count(MARKER), 1)
        self.assertIn('outside &= gfx->mRsp->loaded_vertices[i].clip_rej;', result)
        self.assertIn('return gfx_end_dl_handler_common(cmd0);', result)
        self.assertNotIn('g_exec_stack', result)
        self.assertTrue(result.endswith('\nbool next() {}\n'))
        self.assertEqual(transform(result), result)

    def test_rejects_changed_stub(self):
        with self.assertRaises(ValueError):
            transform(SOURCE.replace('return false;', 'return true;'))
        with self.assertRaises(ValueError):
            transform(SOURCE + ORIGINAL)


if __name__ == '__main__':
    unittest.main()
