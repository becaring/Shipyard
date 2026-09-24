"""Exercise the shared immutable metadata value cache with the host compiler."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ShaderMetadataCacheTests(unittest.TestCase):
    def test_key_value_and_reinitialization(self):
        source = ROOT / 'tests/shader_metadata_cache_cases.cpp'
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'shader_metadata_cache.exe'
            batch = Path(directory) / 'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /std:c++20 /EHsc /MT /DNOMINMAX /I"' + str(ROOT / 'sources') +
                '" "' + str(source) + '" /Fe"' + str(output) + '"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=directory, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(output)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('PASS shader metadata cache key, value and invalidation', run.stdout)
