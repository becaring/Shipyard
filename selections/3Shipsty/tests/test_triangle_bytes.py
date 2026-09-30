import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TriangleBytesTests(unittest.TestCase):
    def test_actual_byte_comparator(self):
        source = (ROOT / 'sources/fast3d/triangle_pipeline.h').read_text()
        body = source.split('// SHIPYARD BYTE EQUALITY BEGIN\n', 1)[1].split(
            '// SHIPYARD BYTE EQUALITY END', 1)[0]
        template = (ROOT / 'tests/triangle_bytes_contract.cpp').read_text()
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-triangle-bytes-') as folder:
            output = Path(folder)
            contract = output / 'contract.cpp'
            contract.write_text(template.replace('// ACTUAL BYTE EQUALITY', body))
            batch = output / 'compile.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /O2 /EHsc /MT "' + str(contract) +
                '" /Fe"' + str(output / 'contract.exe') + '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / 'contract.exe')], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())
            xdk = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Xbox 360 SDK'
            compiled = subprocess.run([str(xdk / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc',
                '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(xdk / 'include/xbox'),
                '/Fo' + str(output / 'contract.obj'), str(contract)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            assembly = subprocess.check_output([str(xdk / 'bin/win32/dumpbin.exe'), '/DISASM',
                                               str(output / 'contract.obj')], text=True)
            equal = assembly.split('Equal56:', 1)[1].split('main:', 1)[0]
            self.assertIn('lwz ', equal)
            self.assertIn('lbz ', equal)
            print('PASS XDK /O2 word loads and byte fallback')


if __name__ == '__main__':
    unittest.main()
