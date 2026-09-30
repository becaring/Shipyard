"""Contracts for the two pinned Prism compiler projections."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))

from xbox360_platform.prism import PrismProjector


class EmptyParser:
    def parse(self, source):
        root = type('Root', (), {'type': 'translation_unit', 'named_children': []})()
        return type('Tree', (), {'root_node': root})()


class PrismProjectorTests(unittest.TestCase):
    def test_rejects_unreviewed_version(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported Prism revision'):
            PrismProjector(EmptyParser(), 'unknown')

    def test_common_lowering_keeps_unrelated_std_names(self):
        source = '#include <variant>\nstd::getline(input,line); std::get<int>(value);\n'
        for revision in ('bbcbc7e3f890a5806b579361e7aa0336acd547e7',
                         '1de054450e7b3c5f777d2e3dfcb228ad120c329d'):
            with self.subTest(revision=revision):
                result = PrismProjector(EmptyParser(), revision).lower(source, 'fixture.cpp')
                self.assertIn('#include <prism/compat.h>\n', result)
                self.assertIn('#include <xbox_compat/variant.h>', result)
                self.assertIn('std::getline(input,line)', result)
                self.assertIn('xbox_compat::get<int>(value)', result)

    def test_c_entry_header_stays_c_compatible(self):
        result = PrismProjector(EmptyParser(), 'bbcbc7e3f890a5806b579361e7aa0336acd547e7').lower(
            '#include <stdint.h>\n', 'invoke.h')
        self.assertNotIn('prism/compat.h', result)

    def test_shared_lowering_does_not_apply_consumer_debug_policy(self):
        source = ('#include <spdlog/spdlog.h>\nSPDLOG_DEBUG("value");\n'
                  'return { start, c };\nreturn { start, c };\n')
        result = PrismProjector(EmptyParser(), '1de054450e7b3c5f777d2e3dfcb228ad120c329d').lower(
            source, 'processor.cpp')
        self.assertIn('#include <spdlog/spdlog.h>', result)
        self.assertIn('SPDLOG_DEBUG("value");', result)
        self.assertNotIn('#if defined(SPDLOG_ACTIVE_LEVEL)', result)

    def test_required_source_anchors_reject_drift(self):
        projector = PrismProjector(
            EmptyParser(), 'bbcbc7e3f890a5806b579361e7aa0336acd547e7')
        with self.assertRaisesRegex(ValueError, 'Expected exactly one Prism anchor'):
            projector.lower('size_t pos = 1;\n: input(text)', 'lexer.h')
        with self.assertRaisesRegex(ValueError, 'Unexpected iterator string returns'):
            projector.lower('return { start, c };', 'processor.cpp')
        with self.assertRaisesRegex(ValueError, 'processor macro anchors'):
            projector.lower('#define CONTAINS', 'processor.h')


if __name__ == '__main__':
    unittest.main()
