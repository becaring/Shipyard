import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class EnvironmentTests(unittest.TestCase):
    def test_bounded_reader_parser_and_immutable_store(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-environment-') as folder:
            output = Path(folder)
            (output/'valid.env').write_bytes(b'# comment\r\nSSB64_MAX_FRAMES=123\r\nVALUE= a=b #literal \nEMPTY=')
            (output/'large.env').write_bytes(b'A=' + b'x' * (1024 * 1024 - 2))
            (output/'oversize.env').write_bytes(b'A=' + b'x' * (1024 * 1024 - 1))
            batch = output/'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /EHsc /W4 /WX /MT /I"' + str(ROOT/'sources') + '" "' +
                str(ROOT/'tests/environment_contract.cpp') + '" /Fe"' +
                str(output/'contract.exe') + '"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([
                str(output/'contract.exe'), str(output/'valid.env'),
                str(output/'missing.env'), str(output), str(output/'oversize.env'),
                str(output/'large.env')
            ], cwd=output, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('Shipyard environment contract PASS', result.stdout)
