"""Bitwise coordinate proof for the common Modern/Smash UV arithmetic."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CoordinateTests(unittest.TestCase):
    def test_exact_numeric_domain_and_ppc_compile(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        xdk = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
        with tempfile.TemporaryDirectory(prefix='shipyard-coordinate-') as folder:
            output = Path(folder)
            host = output / 'host.cmd'
            host.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                            'if errorlevel 1 exit /b 1\n'
                            'cl /nologo /O2 /fp:precise /EHsc /MT /I"' + str(ROOT / 'sources') + '" "' +
                            str(ROOT / 'tests/coordinate_contract.cpp') + '" /Fe"' + str(output / 'contract.exe') +
                            '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(host)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / 'contract.exe')], capture_output=True,
                                    text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS 70656 exact coordinate cases', result.stdout)
            ppc = subprocess.run([str(xdk / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/fp:precise',
                                  '/EHsc', '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(xdk / 'include/xbox'),
                                  '/I' + str(ROOT / 'sources'), '/Fo' + str(output / 'contract-xdk.obj'),
                                  str(ROOT / 'tests/coordinate_contract.cpp')],
                                 cwd=output, capture_output=True, text=True)
            self.assertEqual(ppc.returncode, 0, ppc.stdout + ppc.stderr)
            self.assertEqual((output / 'contract-xdk.obj').read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
