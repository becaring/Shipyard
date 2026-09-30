import unittest

from tree_sitter import Language, Parser
import tree_sitter_cpp

from xbox360_platform.cpp_cursor import lower_indirect_loop
from xbox360_platform.prism import walk


SOURCE = '''bool Read(Command** cmd0) {
    float coords[8];
    for (int i = 0; i < 8; i += 2) {
        ++(*cmd0);
        memcpy(&coords[i], &(*cmd0)->w0, sizeof(float));
        memcpy(&coords[i + 1], &(*cmd0)->w1, sizeof(float));
    }
    return false;
}'''


class CursorTests(unittest.TestCase):
    def lower(self, source):
        return lower_indirect_loop(source, Parser(Language(tree_sitter_cpp.language())), walk,
                                   function='Read', parameter='cmd0', cursor_type='Command*',
                                   expected_reads=3)

    def test_writeback_after_local_traversal(self):
        result = self.lower(SOURCE)
        self.assertIn('Command* shipyardCursor = *cmd0;', result)
        self.assertIn('++(shipyardCursor);', result)
        self.assertIn('&(shipyardCursor)->w1', result)
        self.assertLess(result.index('*cmd0 = shipyardCursor;'), result.index('return false;'))
        self.assertEqual(result.count('*cmd0'), 2)

    def test_reject_extra_cursor_use(self):
        with self.assertRaises(ValueError):
            self.lower(SOURCE.replace('++(*cmd0);', '++(*cmd0); consume(*cmd0);'))

    def test_reject_early_exit(self):
        with self.assertRaises(ValueError):
            self.lower(SOURCE.replace('++(*cmd0);', '++(*cmd0); if (i) return true;'))

    def test_reject_duplicate_loop(self):
        with self.assertRaises(ValueError):
            self.lower(SOURCE.replace('return false;', 'for (;;) { ++(*cmd0); } return false;'))

    def test_reject_observer(self):
        with self.assertRaises(ValueError):
            self.lower(SOURCE.replace('++(*cmd0);', '++(*cmd0); observer();'))


if __name__ == '__main__':
    unittest.main()
