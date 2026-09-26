import unittest
from xbox360_platform.c89_lower import lower


class InlineTests(unittest.TestCase):
    def test_opt_in_preserves_other_tokens(self):
        source = b'static inline int f(){ return 1; } /* inline */ const char* s="inline";'
        self.assertEqual(lower(source)[0], source)
        result, changes = lower(source, xdk_inline=True)
        self.assertEqual(changes, 1)
        self.assertEqual(result, source.replace(b'static inline', b'static __inline'))

    def test_only_selected_source_is_lowered(self):
        source = b'#line 1 "sdk.h"\ninline int a();\n#line 1 "game.c"\ninline int b();'
        result, changes = lower(source, active_source=lambda n: n == b'game.c', xdk_inline=True)
        self.assertEqual(changes, 1)
        self.assertIn(b'inline int a()', result)
        self.assertIn(b'__inline int b()', result)
