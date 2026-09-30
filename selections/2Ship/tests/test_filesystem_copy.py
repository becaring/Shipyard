"""Exercise real Windows file-copy overwrite and failure behavior."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class FileCopyTests(unittest.TestCase):
    def test_overwrite_preserves_default_failure_behavior(self):
        root = Path(__file__).resolve().parents[1]
        source = r'''
#include "filesystem/filesystem.h"
#include <fstream>
#include <cassert>
namespace fs = xbox_compat::filesystem;
std::string read(const char* name) { std::ifstream in(name); std::string text; in >> text; return text; }
int main() {
    {std::ofstream out("source.txt"); out << "new";}
    {std::ofstream out("target.txt"); out << "old";}
    std::error_code error;
    assert(!fs::copy_file("source.txt","target.txt",error) && error);
    assert(read("target.txt")=="old");
    assert(fs::copy_file("source.txt","target.txt",fs::copy_options::overwrite_existing,error) && !error);
    assert(read("target.txt")=="new");
    assert(!fs::copy_file("missing.txt","target.txt",fs::copy_options::overwrite_existing,error) && error);
    assert(read("target.txt")=="new");
    assert(fs::copy_file("source.txt","fresh.txt") && read("fresh.txt")=="new");
    bool threw=false;
    try { fs::copy_file("source.txt","fresh.txt"); } catch(const fs::filesystem_error&) { threw=true; }
    assert(threw);
    assert(!fs::copy_file("source.txt","target.txt",static_cast<fs::copy_options::type>(99),error) && error);
}
'''
        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder); (out/'case.cpp').write_text(source)
            batch = out/'compile.cmd'
            batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"'+str(root/'sources')+'" case.cpp "'+
                str(root/'sources/filesystem/filesystem.cpp')+'" "'+str(root/'sources/async/native.cpp')+'" /Fe:case.exe\nexit /b %errorlevel%\n')
            result = subprocess.run([os.environ['ComSpec'],'/d','/c',str(batch)],cwd=out,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(out/'case.exe')],cwd=out).returncode,0)
