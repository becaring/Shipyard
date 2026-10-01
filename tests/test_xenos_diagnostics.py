import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class XenosDiagnosticsTests(unittest.TestCase):
    def test_hot_path_accounting_has_a_compiled_out_variant(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        self.assertIn('#if SHIPYARD_XENOS_DIAGNOSTICS', source)
        self.assertIn('#define SHIPYARD_DIAGNOSTIC(statement)', source)
        for statement in (
            '++native_state_counts.constants_attempted',
            '++binding_attempted',
            'native_state_counts.sampler_attempted += 5',
            '++upload_counts.requests',
        ):
            self.assertIn('SHIPYARD_DETAIL(' + statement, source)
        self.assertIn('SHIPYARD_DIAGNOSTIC(statement);', source)
        self.assertIn('SHIPYARD_DIAGNOSTIC(alpha_histogram.Record(', source)
        details = (ROOT / 'sources/xenos/details_counts.h').read_text()
        self.assertIn('DetailsCounts() : enabled(false)', details)
        self.assertIn('if (enabled) values[kind] += count;', details)


if __name__ == '__main__':
    unittest.main()
