import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.fast3d_triangle_certificate import invalidate_native_calls


class NativeInvalidationTests(unittest.TestCase):
    def test_unbraced_branch_remains_one_statement(self):
        source = '    if (enabled)\n        mRapi->SetUseAlpha(enabled);\n    else\n        mRapi->SetUseAlpha(false);\n'
        result = invalidate_native_calls(source, 'mXboxPipelineKey.valid=false', minimum=2)
        self.assertEqual(result, '    if (enabled)\n        (mXboxPipelineKey.valid=false, mRapi->SetUseAlpha(enabled));\n'
                         '    else\n        (mXboxPipelineKey.valid=false, mRapi->SetUseAlpha(false));\n')

    def test_other_owner_and_calls_are_preserved(self):
        source = '    gfx->mRapi->SetSamplerParameters(0, true, 0, 0);\n    mRapi->ShaderGetInfo(prg, &count, used);\n'
        result = invalidate_native_calls(source, 'mXboxPipelineKey.valid=false', minimum=1)
        self.assertEqual(result, '    (gfx->mXboxPipelineKey.valid=false, gfx->mRapi->SetSamplerParameters(0, true, 0, 0));\n'
                         '    mRapi->ShaderGetInfo(prg, &count, used);\n')


if __name__ == '__main__':
    unittest.main()
