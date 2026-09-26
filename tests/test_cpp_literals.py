import hashlib
import unittest

from xbox360_platform.cpp_literals import lower_ascii_array


class CharacterArrayLoweringTests(unittest.TestCase):
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
