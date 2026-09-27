import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PresentationSurfaceTests(unittest.TestCase):
    def test_surface_pacing_and_uncapped_opt_in(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-texture-hash-') as folder:
            output = Path(folder)
            batch = output / 'compile.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /O2 /EHsc /MT /I"' + str(ROOT / 'tests/presentation') + '" /I"' + str(ROOT / 'sources') + '" "' +
                str(ROOT / 'tests/presentation/surface_cases.cpp') + '" "' + str(ROOT / 'sources/presentation/xenos_surface.cpp') + '" /Fe"' + str(output / 'contract.exe') +
                '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / 'contract.exe')], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
