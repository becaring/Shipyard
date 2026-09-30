"""The fixed-arity XDK header must come from its maintained generator."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))

from xbox360_platform.value_types import MAX, variant_header


class ValueTypesProjectionTests(unittest.TestCase):
    def test_checked_in_header_matches_generator(self):
        self.assertEqual(MAX, 19)
        header = ROOT / 'sources/value_types/xbox_compat/variant.h'
        self.assertEqual(header.read_text(encoding='utf-8'), variant_header())


if __name__ == '__main__':
    unittest.main()
