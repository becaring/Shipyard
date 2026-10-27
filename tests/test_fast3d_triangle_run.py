import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from xbox360_platform import fast3d_triangle_run as run

SOURCE = (run.HANDLER + '\n    Interpreter* gfx = XboxDispatchInterpreter();\n    F3DGfx* cmd = *cmd0;\n\n' +
          run.BODY + '\n\nvoid Interpreter::Run() {\n    auto dbg = mGfxDebugger;\n' + run.RUN_START + '}\n')


class TriangleRunTests(unittest.TestCase):
    def test_run_continues_only_on_adjacent_tri2(self):
        text = run.install(SOURCE, enable='!sTraced', stop='Walked(cmd + 1)', advance=('Count();',))
        self.assertIn('const bool xboxRun = sXboxTriangleRunEnabled && !sTraced;', text)
        self.assertIn('if (!xboxRun || Walked(cmd + 1) || ((cmd + 1)->words.w0 >> 24) != xboxRunOpcode) break;', text)
        self.assertLess(text.index('Count();'), text.index('*cmd0 = ++cmd;'))
        self.assertLess(text.index('static bool sXboxTriangleRunEnabled'), text.index(run.HANDLER))
        self.assertIn(run.RUN_START + '    sXboxTriangleRunEnabled = !dbg->IsDebugging();', text)
        self.assertEqual(text.count('return false;'), 1)

    def test_changed_handler_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'TRI2 run anchor drift'):
            run.install(SOURCE.replace('C1(1, 7)', 'C1(2, 7)'))


if __name__ == "__main__":
    unittest.main()
