"""Stored zip writer output read back by Python's independent zip reader."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
XDK = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')


class StoredZipTests(unittest.TestCase):
    def test_archives_read_back_and_ppc_compile(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-zip-') as folder:
            output = Path(folder)
            batch = output / 'build.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /O2 /EHsc /W4 /WX /MT /D_CRT_SECURE_NO_WARNINGS /I"' + str(ROOT / 'sources') + '" "' +
                str(ROOT / 'tests/stored_zip_contract.cpp') + '" "' +
                str(ROOT / 'sources/archive/stored_zip_writer.cpp') +
                '" /Fe"' + str(output / 'contract.exe') + '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / 'contract.exe'), str(output)],
                                    capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS stored zip writer', result.stdout)

            with zipfile.ZipFile(output / 'basic.o2r') as archive:
                self.assertIsNone(archive.testzip())
                infos = archive.infolist()
                names = [info.filename for info in infos]
                self.assertEqual(names, sorted(names))
                self.assertEqual(len(names), len(set(names)))
                self.assertEqual(len(names), 2007)
                self.assertTrue(all(info.compress_type == zipfile.ZIP_STORED for info in infos))
                self.assertEqual(archive.read('version'), b'\x00\x12\x34\x56\x78')
                self.assertEqual(archive.read('objects/a'), b'')
                self.assertEqual(archive.read('objects/b'), b'again')
                self.assertEqual(archive.read('objects/c'), b'longer value')
                self.assertEqual(archive.read('textures/\u00e9t\u00e9'), b'x')
                large = archive.read('large')
                self.assertEqual(len(large), 3 << 20)
                self.assertEqual(large[:4], bytes((i * 31 + (i >> 9)) & 0xFF for i in range(4)))
                self.assertEqual(archive.read('many/0007'), b'many/0007')
                self.assertEqual(archive.read('dup/same'), b'abc')
                # Changed repeats of objects/b and objects/c keep their first bytes as
                # unreferenced space; the identical dup/same repeat writes nothing.
                local = sum(30 + len(info.filename.encode()) + info.file_size for info in infos)
                local += 2 * (30 + len('objects/b') + 5)
                central = sum(46 + len(info.filename.encode()) for info in infos)
                self.assertEqual((output / 'basic.o2r').stat().st_size, local + central + 22)

            for source in ('sources/archive/stored_zip_writer.cpp', 'tests/stored_zip_contract.cpp'):
                obj = output / (Path(source).stem + '-xdk.obj')
                ppc = subprocess.run([str(XDK / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc', '/W4',
                                      '/D_XBOX', '/D_XBOX_VER=200', '/D_CRT_SECURE_NO_WARNINGS',
                                      '/I' + str(XDK / 'include/xbox'), '/I' + str(ROOT / 'sources'),
                                      '/Fo' + str(obj), str(ROOT / source)],
                                     cwd=output, capture_output=True, text=True)
                self.assertEqual(ppc.returncode, 0, ppc.stdout + ppc.stderr)
                self.assertEqual(obj.read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
