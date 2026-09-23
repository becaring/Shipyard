import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AtomicFileTests(unittest.TestCase):
    def test_logical_cwd_and_validation(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-atomic-') as folder:
            output = Path(folder)
            logical = output/'logical'
            logical.mkdir()
            batch = output/'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /EHsc /MT /DSHIPYARD_TEST_LOGICAL_CWD /I"' +
                str(ROOT/'sources') + '" "' + str(ROOT/'tests/atomic_file_contract.cpp') +
                '" "' + str(ROOT/'sources/storage/atomic_file.cpp') +
                '" "' + str(ROOT/'sources/filesystem/filesystem.cpp') +
                '" "' + str(ROOT/'sources/async/native.cpp') + '" /Fe"' +
                str(output/'contract.exe') + '"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output/'contract.exe'), str(logical)], cwd=output,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('share logical resolution', result.stdout)
