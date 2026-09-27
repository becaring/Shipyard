import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TextureHashCacheTests(unittest.TestCase):
    def test_owned_texture_revalidation_and_invalidation(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-texture-hash-') as folder:
            output = Path(folder)
            batch = output / 'compile.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /O2 /EHsc /MT /I"' + str(ROOT / 'sources') + '" "' +
                str(ROOT / 'tests/texture_hash_cache_contract.cpp') + '" /Fe"' + str(output / 'contract.exe') +
                '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / 'contract.exe')], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_archive_epoch_route_compiles_for_xenon(self):
        xdk = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
        with tempfile.TemporaryDirectory(prefix='shipyard-texture-hash-xenon-') as folder:
            output = Path(folder) / 'contract.obj'
            result = subprocess.run([
                str(xdk / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc',
                '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(xdk / 'include/xbox'),
                '/I' + str(ROOT / 'sources'), '/Fo' + str(output),
                str(ROOT / 'tests/texture_hash_cache_contract.cpp'),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(output.read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
