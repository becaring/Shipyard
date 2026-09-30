from xenos_source import core_source

"""Run the real framebuffer transitions with SDK-style raster-state resets."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_draw_preparation import function

ROOT = Path(__file__).resolve().parents[1]


class RasterBoundsTests(unittest.TestCase):
    def test_target_bind_and_tiling_restarts_keep_draw_bounds(self):
        source = core_source()
        methods = function(source, "void BeginFramebuffer() {") + "\n"
        methods += function(source, "void FinishFramebuffer(bool preserveDepth, bool preserveColor) {")
        methods += "\n" + function(source, "void Viewport(int x, int y, int w, int h) {")
        methods += "\n" + function(source, "void Scissor(int x, int y, int w, int h) {")
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        installation = subprocess.check_output(
            [
                str(vswhere),
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property",
                "installationPath",
            ],
            text=True,
        ).strip()
        with tempfile.TemporaryDirectory(prefix="shipyard-raster-bounds-") as folder:
            out = Path(folder)
            (out / "transitions.inc").write_text(methods)
            batch = out / "compile.cmd"
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"'
                + str(out)
                + '" /I"'
                + str(ROOT / "sources")
                + '" "'
                + str(ROOT / "tests/raster_bounds_contract.cpp")
                + '" /Fecontract.exe\nexit /b %errorlevel%\n'
            )
            built = subprocess.run(
                [os.environ["ComSpec"], "/d", "/c", str(batch)], cwd=out, capture_output=True, text=True
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(out / "contract.exe")], capture_output=True, text=True, timeout=15)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
