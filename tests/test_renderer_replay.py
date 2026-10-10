"""Exercise shared renderer reuse and both native depth interfaces."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def compile_run(test, source):
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    installation = subprocess.check_output([
        str(vswhere), '-latest', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
    ], text=True).strip()
    with tempfile.TemporaryDirectory(prefix='shipyard-renderer-replay-') as folder:
        output = Path(folder)
        (output / 'contract.cpp').write_text(source)
        batch = output / 'compile.cmd'
        batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
            'if errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /O2 /fp:precise /EHsc /MT /I"' +
            str(ROOT / 'sources') + '" "' + str(output / 'contract.cpp') + '" /Fe"' +
            str(output / 'contract.exe') + '"\nexit /b %errorlevel%\n')
        built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                               cwd=output, capture_output=True, text=True)
        test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(output / 'contract.exe')], cwd=output,
                             capture_output=True, text=True, timeout=60)
        test.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        print(ran.stdout.strip())


class RendererReplayTests(unittest.TestCase):
    def test_owned_replay_and_material_caches(self):
        compile_run(self, (ROOT / 'tests/renderer_replay_contract.cpp').read_text())

    def test_depth_arrays_and_legacy_map_match_reference(self):
        core = (ROOT / 'sources/xenos/core.cpp').read_text()
        native = core[core.index('struct DepthArrayRange {'):core.index('\nvoid* FramebufferImage(')]
        fixture = (ROOT / 'tests/depth_samples_contract.cpp').read_text()
        compile_run(self, fixture.replace('// SHARED NATIVE DEPTH', native))


if __name__ == '__main__':
    unittest.main()
