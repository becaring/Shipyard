import unittest
from xbox360_platform.c_bool_scalars import lower


class ScalarBooleanTests(unittest.TestCase):
    def test_wide_values_are_normalized_at_each_conversion(self):
        source = b'bool f(int x) { bool b = x; b = x & 256; return b; }'
        self.assertEqual(lower(source, 2),
                         b'unsigned char f(int x) { unsigned char b = ((x) != 0); b = ((x & 256) != 0); return ((b) != 0); }')

    def test_global_boolean_and_mask_return(self):
        source = b'static bool flag = false; bool f(int x) { flag = x; return x & 256; }'
        result = lower(source, 2)
        self.assertIn(b'flag = ((x) != 0)', result)
        self.assertIn(b'return ((x & 256) != 0)', result)

    def test_unreviewed_contexts_are_rejected(self):
        for source in (b'bool* p;', b'void f(bool x) {}', b'bool x; void f(){ x++; }',
                       b'bool x; void f(){ int x; }', b'bool x; void f(int* x) {}'):
            with self.assertRaises(ValueError):
                lower(source, 1)

    def test_inventory_drift_is_rejected(self):
        with self.assertRaises(ValueError):
            lower(b'int x;', 1)

    def test_reviewed_boolean_abi_address(self):
        self.assertIn(b'write(&flag)', lower(b'bool flag; void f(){ write(&flag); }', 1,
                                            approved_addresses=frozenset({b'flag'})))
