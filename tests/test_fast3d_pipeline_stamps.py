import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from xbox360_platform import fast3d_pipeline_stamps as stamps

SOURCE = (stamps.OWNER +
          ''.join('bool %s(F3DGfx** cmd0) {\n    return false;\n}\n' % name for name in stamps.NEUTRAL_HANDLERS) +
          stamps.NAMES + '\n    return "";\n}\n' +
          ''.join('    if (%s(&cmd)) {\n        return;\n    }\n' % call for call in stamps.SEQUENTIAL_CALLS))


class PipelineStampTests(unittest.TestCase):
    def test_every_dispatch_site_advances_the_stamp(self):
        text = stamps.install(SOURCE)
        self.assertEqual(text.count('static unsigned sXboxPipelineMutation = 1;'), 1)
        for call in stamps.SEQUENTIAL_CALLS:
            self.assertIn('if (XboxDispatchStamped(' + call + ', &cmd, opcode)) {', text)
            self.assertNotIn('if (' + call + '(&cmd)) {', text)
        self.assertLess(text.index('sXboxPipelineNeutral;'), text.index(stamps.NAMES))
        for name in stamps.NEUTRAL_HANDLERS:
            self.assertIn('    ' + name, text[text.index('sXboxPipelineNeutralHandlers[] = {'):])

    def test_missing_neutral_handler_is_a_reaudit(self):
        source = SOURCE.replace('bool gfx_mtx_handler_f3dex2(', 'bool gfx_mtx_handler_renamed(')
        with self.assertRaisesRegex(ValueError, 'gfx_mtx_handler_f3dex2'):
            stamps.install(source)


if __name__ == "__main__":
    unittest.main()
