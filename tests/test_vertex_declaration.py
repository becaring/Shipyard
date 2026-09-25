import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VertexDeclarationTests(unittest.TestCase):
    def test_native_formats_preserve_component_swizzles(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        start = source.index('void Element(')
        method = source[start:source.index('\n}', start) + 2]
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-vertex-declaration-') as folder:
            out = Path(folder)
            (out / 'element.inc').write_text(method + '\n')
            batch = out / 'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /EHsc /MT /I"' + str(out) + '" "' +
                str(ROOT / 'tests/vertex_declaration_contract.cpp') + '" /Fe"' +
                str(out / 'contract.exe') + '"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out / 'contract.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('native vertex formats PASS', result.stdout)
