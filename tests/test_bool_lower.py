import unittest

from xbox360_platform.bool_lower import lower


def selected(path):
    return path == b"game.c"


class BoolLowerTests(unittest.TestCase):
    def test_header_declarations_preserve_byte_layout(self):
        source = (b'#line 1 "game.h"\n'
                  b'typedef struct Save { _Bool enabled; } Save;\n'
                  b'#line 1 "game.c"\n'
                  b'int run(void) { return 7; }\n')
        emitted, stats = lower(source, active_source=selected)
        self.assertIn(b'unsigned char enabled', emitted)
        self.assertEqual(stats["type_tokens"], 1)

    def test_selected_boolean_store_requires_conversion(self):
        source = b'#line 1 "game.c"\nint run(void) { _Bool value = 256; return value; }\n'
        with self.assertRaisesRegex(ValueError, "conversion lowering"):
            lower(source, active_source=selected)

    def test_selected_boolean_field_access_is_rejected(self):
        source = (b'#line 1 "game.h"\n'
                  b'typedef struct Save { _Bool enabled; } Save;\n'
                  b'#line 1 "game.c"\n'
                  b'int run(Save* save) { return save->enabled; }\n')
        with self.assertRaisesRegex(ValueError, "boolean fields"):
            lower(source, active_source=selected)

    def test_selected_boolean_parameter_call_is_rejected(self):
        source = (b'#line 1 "game.h"\n'
                  b'void set_enabled(_Bool enabled);\n'
                  b'#line 1 "game.c"\n'
                  b'int run(void) { set_enabled(256); return 0; }\n')
        with self.assertRaisesRegex(ValueError, "boolean-parameter"):
            lower(source, active_source=selected)


if __name__ == "__main__":
    unittest.main()
