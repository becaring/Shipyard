import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ShaderCacheFailureTests(unittest.TestCase):
    def test_optional_store_and_lookup_allocation_failures(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-shader-cache-') as folder:
            out = Path(folder)
            batch = out / 'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /EHsc /MT /I"' + str(ROOT / 'tests/xenos-sdk') +
                '" /I"' + str(ROOT / 'sources') + '" "' +
                str(ROOT / 'tests/shader_cache_failure_contract.cpp') + '" "' +
                str(ROOT / 'sources/xenos/shader_cache.cpp') + '" /Fe"' +
                str(out / 'contract.exe') + '"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out / 'contract.exe')], capture_output=True,
                                    text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('allocation failure PASS', result.stdout)
            payload = subprocess.run([str(out / 'contract.exe'), 'payload'],
                                     capture_output=True, text=True, timeout=10)
            self.assertEqual(payload.returncode, 0, payload.stdout + payload.stderr)
            self.assertIn('payload allocation failure PASS', payload.stdout)


if __name__ == '__main__':
    unittest.main()
