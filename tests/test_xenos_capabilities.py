import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from xbox360_platform.xenos_capabilities import compiler_options


class XenosCapabilityTests(unittest.TestCase):
    def test_release_enables_savings_and_compiles_out_diagnostics(self):
        options = compiler_options(3, diagnostics='0')
        for enabled in ('DEFERRED_FRAMEBUFFERS', 'BINDING_REUSE', 'TILING_BUDGET'):
            self.assertIn('/DSHIPYARD_XENOS_' + enabled + '=1', options)
        for disabled in ('DIAGNOSTICS', 'DETAILED_TIMING', 'TILING_TRIALS', 'SHADER_PACK'):
            self.assertIn('/DSHIPYARD_XENOS_' + disabled + '=0', options)
        self.assertIn('/DSHIPYARD_XENOS_SHADER_ABI=3', options)

    def test_instrumented_build_restores_counters(self):
        options = compiler_options(2, shader_pack=True, diagnostics='1')
        self.assertIn('/DSHIPYARD_XENOS_DIAGNOSTICS=1', options)
        self.assertIn('/DSHIPYARD_XENOS_DETAILED_TIMING=1', options)
        self.assertIn('/DSHIPYARD_XENOS_SHADER_PACK=1', options)
        with self.assertRaises(ValueError):
            compiler_options(3, diagnostics='yes')


if __name__ == "__main__":
    unittest.main()
