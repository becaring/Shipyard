import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MaterialValueCacheTests(unittest.TestCase):
    def test_complete_key_controls_value_rebuild(self):
        with tempfile.TemporaryDirectory(prefix='shipyard-material-cache-') as folder:
            out = Path(folder)
            batch = out / 'compile.cmd'
            batch.write_text(
                '@echo off\ncall "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'cl /nologo /EHsc /MT /I"' + str(ROOT / 'sources') + '" "' +
                str(ROOT / 'tests/material_value_cache_contract.cpp') + '" /Fe"' +
                str(out / 'contract.exe') + '"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(out / 'contract.exe')], cwd=out,
                                 capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('complete-key miss', ran.stdout)
