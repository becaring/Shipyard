"""Compare both submission paths through the renderer's shared draw scaffold."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.test_draw_preparation import function

ROOT = Path(__file__).resolve().parents[1]


class IndexedDrawTests(unittest.TestCase):
    def test_bounds_payload_order_and_state_boundaries(self):
        source = (ROOT / "sources/xenos/core.cpp").read_text()
        draw = function(source, "void Draw(float* vertices,")
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        installation = subprocess.check_output([
            str(vswhere), "-latest", "-products", "*", "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix="shipyard-indexed-") as folder:
            out = Path(folder)
            (out / "production_draw.inc").write_text(draw)
            for trials, budget, deferred, abi in ((0,0,0,2), (0,1,1,2), (1,0,0,3), (1,1,1,3)):
                with self.subTest(trials=trials, budget=budget, deferred=deferred, abi=abi):
                    batch = out / "compile.cmd"
                    flags = (
                        f" /DSHIPYARD_XENOS_TILING_TRIALS={trials} /DSHIPYARD_XENOS_TILING_BUDGET={budget}"
                        f" /DSHIPYARD_XENOS_DEFERRED_FRAMEBUFFERS={deferred} /DSHIPYARD_XENOS_SHADER_ABI={abi}"
                        " /DSHIPYARD_XENOS_ALPHA_HISTOGRAM=1"
                    )
                    batch.write_text(
                        '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                        'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /O2 /MT' + flags + ' /I"' + str(out) +
                        '" /I"' + str(ROOT / "sources") + '" "' + str(ROOT / "tests/indexed_draw_contract.cpp") +
                        '" /Fe"' + str(out / "contract.exe") + '"\nexit /b %errorlevel%\n'
                    )
                    built = subprocess.run([os.environ["ComSpec"], "/d", "/c", str(batch)], cwd=out,
                                           capture_output=True, text=True)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    result = subprocess.run([str(out / "contract.exe")], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
