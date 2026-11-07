"""Stored zip reader against archives written by Python's independent zip writer."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
XDK = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
CONTRACT = r'''
#include "archive/stored_zip_reader.h"
#include <stdio.h>
#include <string>
#include <vector>
static int failures = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        printf("FAIL %s\n", what);
        ++failures;
    }
}
int main(int, char** argv) {
    const std::string folder = argv[1];
    Shipyard::Archive::StoredZipReader reader;
    Check(reader.Open((folder + "/assets.zip").c_str()), "open stored archive");
    std::string bytes;
    Check(reader.Read("xml/GC_MQ_D/objects/object_Link.xml", &bytes) && bytes == "<Root/>", "read exact name");
    Check(reader.Read("XML\\gc_mq_d\\Objects\\object_link.xml", &bytes) && bytes == "<Root/>", "read any case and slash");
    Check(reader.Read("empty.txt", &bytes) && bytes.empty(), "read empty entry");
    Check(reader.Read("filelists/big.txt", &bytes) && bytes.size() == 300000 && bytes[299999] == 'z', "read large entry");
    Check(reader.Read("xml/GC_MQ_D", NULL) == false, "folders are not entries");
    Check(reader.Read("missing.xml", &bytes) == false, "missing entry");
    std::vector<std::string> names;
    reader.List("xml/gc_mq_d/", names);
    Check(names.size() == 2 && names[0] == "xml/GC_MQ_D/objects/object_Link.xml" && names[1] == "xml/GC_MQ_D/scenes/a.xml",
          "list a folder at any depth with stored names");
    names.clear();
    reader.List("xml/GC_MQ", names);
    Check(names.empty(), "a name prefix is not a folder");
    names.clear();
    reader.List("", names);
    Check(names.size() == 5, "list everything");
    reader.Close();
    Check(!reader.IsOpen() && !reader.Read("empty.txt", NULL), "closed reader holds nothing");
    Check(!reader.Open((folder + "/deflated.zip").c_str()), "reject compressed archive");
    Check(!reader.Open((folder + "/absent.zip").c_str()), "reject missing archive");
    if (failures == 0)
        printf("PASS stored zip reader\n");
    return failures;
}
'''


class StoredZipReaderTests(unittest.TestCase):
    def test_reads_python_archives_and_ppc_compiles(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-zip-reader-') as folder:
            output = Path(folder)
            with zipfile.ZipFile(output / 'assets.zip', 'w', zipfile.ZIP_STORED) as archive:
                archive.writestr('xml/GC_MQ_D/', b'')
                archive.writestr('xml/GC_MQ_D/objects/object_Link.xml', b'<Root/>')
                archive.writestr('xml/GC_MQ_D/scenes/a.xml', b'<Scene/>')
                archive.writestr('xml/GC_MQ_DX/b.xml', b'<Other/>')
                archive.writestr('empty.txt', b'')
                archive.writestr('filelists/big.txt', b'y' * 299999 + b'z')
                archive.comment = b'a comment moves the end record'
            with zipfile.ZipFile(output / 'deflated.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
                archive.writestr('a.xml', b'<Root/>' * 100)
            contract = output / 'contract.cpp'
            contract.write_text(CONTRACT)
            batch = output / 'build.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /O2 /EHsc /W4 /WX /MT /D_CRT_SECURE_NO_WARNINGS /I"' + str(ROOT / 'sources') + '" "' +
                str(contract) + '" "' + str(ROOT / 'sources/archive/stored_zip_reader.cpp') +
                '" /Fe"' + str(output / 'contract.exe') + '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / 'contract.exe'), str(output)],
                                    capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS stored zip reader', result.stdout)

            obj = output / 'stored_zip_reader-xdk.obj'
            ppc = subprocess.run([str(XDK / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc', '/W4',
                                  '/D_XBOX', '/D_XBOX_VER=200', '/D_CRT_SECURE_NO_WARNINGS',
                                  '/I' + str(XDK / 'include/xbox'), '/I' + str(ROOT / 'sources'),
                                  '/Fo' + str(obj), str(ROOT / 'sources/archive/stored_zip_reader.cpp')],
                                 cwd=output, capture_output=True, text=True)
            self.assertEqual(ppc.returncode, 0, ppc.stdout + ppc.stderr)
            self.assertEqual(obj.read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
