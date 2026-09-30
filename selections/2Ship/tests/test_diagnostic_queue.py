import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DiagnosticQueueTests(unittest.TestCase):
    def test_overlapping_shutdown_waits_for_final_callback(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-shutdown-') as folder:
            out = Path(folder)
            batch = out / 'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /EHsc /MT /I"' + str(ROOT/'tests/io-sdk') +
                '" /I"' + str(ROOT/'sources') + '" "' +
                str(ROOT/'tests/diagnostic_queue_shutdown_contract.cpp') +
                '" /Fe"' + str(out/'contract.exe') + '"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out/'contract.exe')], capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('overlapping shutdown completion PASS', result.stdout)

    def test_owned_record_worker_contract(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-diagnostic-') as folder:
            out = Path(folder)
            batch = out / 'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /EHsc /MT /I"' + str(ROOT/'tests/io-sdk') +
                '" /I"' + str(ROOT/'sources') + '" "' +
                str(ROOT/'tests/diagnostic_queue_contract.cpp') + '" "' +
                str(ROOT/'sources/diagnostics/queue.cpp') + '" /Fe"' +
                str(out/'contract.exe') + '"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out/'contract.exe')], capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('post-shutdown PASS', result.stdout)

if __name__ == '__main__': unittest.main()
