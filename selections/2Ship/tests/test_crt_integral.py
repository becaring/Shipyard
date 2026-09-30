"""Compare shared IEEE rounding to the host CRT in all rounding modes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class IntegralTests(unittest.TestCase):
    def test_rounding_modes_and_boundaries(self):
        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            batch = out/'compile.cmd'
            batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /fp:strict '
                '/DSHIPYARD_CRT_NEARBYINT=tested_nearbyint /DSHIPYARD_CRT_NEARBYINTF=tested_nearbyintf '
                '/DSHIPYARD_CRT_ROUNDF=tested_roundf /DSHIPYARD_CRT_TRUNCF=tested_truncf '
                '"'+str(ROOT/'sources/crt/float.cpp')+'" "'+str(ROOT/'tests/crt_integral_cases.cpp')+
                '" /Fe:case.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            self.assertEqual(subprocess.run([str(out/'case.exe')]).returncode, 0)
