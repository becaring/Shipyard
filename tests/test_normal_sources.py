"""Count real helper operations without adding runtime instrumentation."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NormalSourcesTests(unittest.TestCase):
    def test_small_batches_and_existing_numeric_contract(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-normal-sources-') as folder:
            out = Path(folder)
            header = (ROOT / 'sources/fast3d/normal_sources.h').read_text()
            assignment = 'slots[slot] = 0xffffffffu;'
            read = 'const uint8_t* normal = bytes + i * 16 + 12;'
            self.assertEqual(header.count(assignment), 1)
            self.assertEqual(header.count(read), 1)
            instrumented = header.replace(assignment, 'slots[slot] = (++normalSlotWrites, 0xffffffffu);')
            instrumented = instrumented.replace(read, '++normalRecordReads;\n        ' + read)
            (out / 'fast3d').mkdir()
            (out / 'fast3d/normal_sources.h').write_text(
                'extern unsigned normalSlotWrites, normalRecordReads;\n' + instrumented)
            fixture = out / 'contract.cpp'
            fixture.write_text('unsigned normalSlotWrites = 0, normalRecordReads = 0;\n'
                               '#define SHIPYARD_TEST_NORMAL_COUNTS\n' +
                               (ROOT / 'tests/normal_sources_contract.cpp').read_text())
            batch = out / 'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /O2 /EHsc /MT /I"' + str(out) +
                '" "' + str(fixture) + '" /Fe"' + str(out / 'contract.exe') +
                '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out / 'contract.exe')], capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: normal source', result.stdout)

            xdk = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
            ppc = subprocess.run([
                str(xdk / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc',
                '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(xdk / 'include/xbox'),
                '/I' + str(ROOT / 'sources'), str(ROOT / 'tests/normal_sources_contract.cpp'),
                '/Fo' + str(out / 'normal-sources.obj'),
            ], cwd=out, capture_output=True, text=True)
            self.assertEqual(ppc.returncode, 0, ppc.stdout + ppc.stderr)
            self.assertEqual((out / 'normal-sources.obj').read_bytes()[:2], b'\xf2\x01')
            print('Empty/singleton: 0 slot initializations, 0 normal reads; numeric contract and PPC compile PASS')


if __name__ == '__main__':
    unittest.main()
