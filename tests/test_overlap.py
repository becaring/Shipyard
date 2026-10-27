"""Game/render overlap primitives: host contracts and a PowerPC instantiation."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
XDK = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
CONTRACTS = {
    'frame_channel': 'frame channel: pool reuse',
    'frame_worker': 'Whole-game handoff',
    'deferred_storage': 'Deferred storage debug ownership',
    'writable_spans': 'writable spans: alternation',
    'replay_batch': 'Interpolation cadence',
    'overlap_depth': 'overlap depth sampling',
    'presentation_cadence': 'Presentation cadence passed',
}


class OverlapTests(unittest.TestCase):
    def test_host_contracts(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-overlap-') as folder:
            output = Path(folder)
            for name, expected in CONTRACTS.items():
                with self.subTest(name):
                    batch = output / (name + '.cmd')
                    batch.write_text(
                        '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                        'if errorlevel 1 exit /b 1\n'
                        'cl /nologo /std:c++17 /EHsc /MT /W4 /WX /I"' + str(ROOT / 'sources') + '" "' +
                        str(ROOT / 'tests' / (name + '_contract.cpp')) + '" "' +
                        str(ROOT / 'sources/async/native.cpp') + '" /Fo"' + str(output) + '\\\\" /Fe"' +
                        str(output / (name + '.exe')) + '"\nexit /b %errorlevel%\n')
                    built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                           cwd=output, capture_output=True, text=True)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    result = subprocess.run([str(output / (name + '.exe'))], capture_output=True,
                                            text=True, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(expected, result.stdout)

    def test_ppc_instantiation(self):
        with tempfile.TemporaryDirectory(prefix='shipyard-overlap-xdk-') as folder:
            obj = Path(folder) / 'overlap.obj'
            built = subprocess.run([str(XDK / 'bin/win32/cl.exe'), '/nologo', '/c', '/EHsc', '/W4', '/WX',
                                    '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(XDK / 'include/xbox'),
                                    '/I' + str(ROOT / 'sources'), '/Fo' + str(obj),
                                    str(ROOT / 'tests/overlap_xdk.cpp')],
                                   cwd=folder, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            self.assertEqual(obj.read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
