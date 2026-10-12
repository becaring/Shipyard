"""Host contract and PowerPC compile check for the shared ROM intake service."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
XDK = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')


class RomTests(unittest.TestCase):
    def test_host_contract_and_ppc_compile(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-rom-') as folder:
            output = Path(folder)
            (output / 'roms').mkdir()
            batch = output / 'build.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /O2 /EHsc /W4 /WX /MT /D_CRT_SECURE_NO_WARNINGS /I"' + str(ROOT / 'sources') + '" "' +
                str(ROOT / 'tests/rom_contract.cpp') + '" "' + str(ROOT / 'sources/rom/rom.cpp') +
                '" /Fe"' + str(output / 'contract.exe') + '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / 'contract.exe'), str(output / 'roms')],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS rom intake', result.stdout)
            for source in ('sources/rom/rom.cpp', 'tests/rom_contract.cpp'):
                obj = output / (Path(source).stem + '-xdk.obj')
                ppc = subprocess.run([str(XDK / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc', '/W4',
                                      '/D_XBOX', '/D_XBOX_VER=200', '/D_CRT_SECURE_NO_WARNINGS',
                                      '/I' + str(XDK / 'include/xbox'), '/I' + str(ROOT / 'sources'),
                                      '/Fo' + str(obj), str(ROOT / source)],
                                     cwd=output, capture_output=True, text=True)
                self.assertEqual(ppc.returncode, 0, ppc.stdout + ppc.stderr)
                self.assertEqual(obj.read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
