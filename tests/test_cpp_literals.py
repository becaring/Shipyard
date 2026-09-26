import hashlib
import unittest

from xbox360_platform.cpp_literals import lower_ascii_array, lower_binary_integers


class CharacterArrayLoweringTests(unittest.TestCase):
    def test_binary_tokens_preserve_suffixes_and_ignore_strings(self):
        import tree_sitter
        import tree_sitter_cpp
        parser = tree_sitter.Parser(tree_sitter.Language(tree_sitter_cpp.language()))
        source = 'int a = 0b11; auto b = 0B101ULL; const char* s = "0b11"; // 0b10'
        self.assertEqual(lower_binary_integers(source, parser),
                         'int a = 0x3; auto b = 0x5ULL; const char* s = "0b11"; // 0b10')

    def test_binary_user_suffix_is_rejected(self):
        import tree_sitter
        import tree_sitter_cpp
        parser = tree_sitter.Parser(tree_sitter.Language(tree_sitter_cpp.language()))
        with self.assertRaisesRegex(ValueError, 'Unaudited'):
            lower_binary_integers('auto a = 0b11_custom;', parser)

    def test_preserves_concatenated_bytes_and_terminator(self):
        original = 'static const char value[4] = "ab" "c";'
        checksum = hashlib.sha256(b'abc\0').hexdigest()
        result = lower_ascii_array(original, 'static const char value[4] =', 4, checksum)
        self.assertEqual(result, 'static const char value[4] ={\n    97,98,99,0,\n};')

    def test_rejects_nonliteral_initializer(self):
        checksum = hashlib.sha256(b'abc\0').hexdigest()
        with self.assertRaisesRegex(ValueError, 'Unaudited'):
            lower_ascii_array('static const char value[4] = other;',
                              'static const char value[4] =', 4, checksum)


if __name__ == '__main__':
    unittest.main()
