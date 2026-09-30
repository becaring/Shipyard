import tempfile
import unittest
from pathlib import Path

import tree_sitter
import tree_sitter_cpp

from xbox360_platform.logging_syntax import LoggingSyntax


class LoggingSyntaxTests(unittest.TestCase):
    def setUp(self):
        parser = tree_sitter.Parser(tree_sitter.Language(tree_sitter_cpp.language()))
        self.syntax = LoggingSyntax(Path(tempfile.gettempdir()), parser)

    def test_range_uses_requested_iterators(self):
        source = b'void run() { for (auto& entry : entries) { use(entry); } }'
        output = self.syntax.lower_ranges(source, begin='Begin', end='End')
        self.assertIn(b'Begin(_loggingRange', output)
        self.assertIn(b'End(_loggingRange', output)
        self.assertNotIn(b'for (auto& entry : entries)', output)

    def test_mapping_keeps_multiline_macro_intact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            syntax = LoggingSyntax(root, self.syntax.parser)
            upstream = root / 'source.h'
            generated = root / 'generated.h'
            upstream.write_text('#define VALUE(x) \\\n+    ((x) + 1)\nint value;\n')
            syntax.mapped(upstream, '#define VALUE(x) \\\n+    ((x) + 2)\nint value;\n', generated)
            output = generated.read_text()
            self.assertNotIn('\\\n#line', output)
            self.assertIn('((x) + 2)', output)


if __name__ == '__main__':
    unittest.main()
