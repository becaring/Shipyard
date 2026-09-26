"""Compare bounded-depth parsing with the pinned upstream callback semantics."""
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class JsonDepthTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('SHIPYARD_NLOHMANN_INCLUDE'), 'Set the pinned upstream JSON include directory')
    def test_reference_callback_and_full_parser(self):
        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            runpy.run_path(str(ROOT/'python/xbox360_platform/json/backend.py'))['generate'](out/'json/generated')
            outputs = []
            for reference in (True, False):
                name = 'reference' if reference else 'shared'
                flags = ('/DREFERENCE_JSON /I"'+os.environ['SHIPYARD_NLOHMANN_INCLUDE']+'"' if reference else
                         '/I"'+str(ROOT/'sources/json/include')+'" /I"'+str(ROOT/'sources')+'" /I"'+str(out)+
                         '" "'+str(ROOT/'sources/json/json.cpp')+'"')
                batch = out/'compile.cmd'
                batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                    'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT '+flags+' "'+str(ROOT/'tests/json_depth_cases.cpp')+
                    '" /Fe:'+name+'.exe\nexit /b %errorlevel%\n')
                result = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                outputs.append(subprocess.check_output([str(out/(name+'.exe'))], text=True))
            self.assertEqual(outputs[0], outputs[1])
