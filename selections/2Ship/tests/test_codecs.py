import unittest
from xbox360_platform.codecs import adapt, units


class CodecTests(unittest.TestCase):
    def test_version_and_platform_guards_are_checked(self):
        source = '#if defined(_MSC_VER)\n   _set_abort_behavior(0, 1);\n#endif\n'
        result = adapt('opus', '1.6.1', 'celt/arch.h', source)
        self.assertIn('&& !defined(_XBOX)', result)
        self.assertIn('_set_abort_behavior(0, 1)', result)
        with self.assertRaises(ValueError):
            adapt('opus', 'unknown', 'celt/arch.h', source)
        with self.assertRaises(ValueError):
            adapt('opus', '1.6.1', 'celt/arch.h', '')

    def test_source_inventory_rejects_duplicates_and_unknown_names(self):
        text = ('set(VORBIS_SOURCES a.c b.c)\nset(VORBISFILE_SOURCES c.c)\n'
                'set(VORBISENC_SOURCES d.c)\n')
        self.assertEqual(units('vorbis', lambda n: text), ['lib/a.c', 'lib/b.c', 'lib/c.c', 'lib/d.c'])
        for invalid in (text.replace('d.c', 'a.c'), text.replace('d.c', '../outside.c'),
                        text.replace('VORBIS_SOURCES', 'RENAMED')):
            with self.assertRaises(ValueError):
                units('vorbis', lambda n: invalid)
