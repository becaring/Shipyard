"""Exercise actual ordinary/ordered JSON conversion with a counting allocator."""

import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class JsonVectorTests(unittest.TestCase):
    def test_conversion_allocation_and_ownership(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-json-vector-') as folder:
            out = Path(folder)
            runpy.run_path(str(ROOT / 'python/xbox360_platform/json/backend.py'))['generate'](
                out / 'json/generated')
            sources = [ROOT / 'sources/json/json.cpp', ROOT / 'tests/json_vector_contract.cpp']
            includes = [ROOT / 'sources/json/include', ROOT / 'sources', out]
            arguments = ' '.join('/I"' + str(p) + '"' for p in includes)
            arguments += ' ' + ' '.join('"' + str(p) + '"' for p in sources)
            batch = out / 'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /O2 /EHsc /MT ' + arguments +
                ' /Fe"' + str(out / 'contract.exe') + '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out / 'contract.exe')], capture_output=True,
                                    text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('allocation counts PASS', result.stdout)
            print(result.stdout, end='')

            xdk = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
            ppc = subprocess.run([
                str(xdk / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc', '/MT',
                '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(xdk / 'include/xbox'),
                *['/I' + str(p) for p in includes], *[str(p) for p in sources],
            ], cwd=out, capture_output=True, text=True)
            self.assertEqual(ppc.returncode, 0, ppc.stdout + ppc.stderr)
            for name in ('json.obj', 'json_vector_contract.obj'):
                self.assertEqual((out / name).read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
