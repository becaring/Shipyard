"""Execute lowered maps and JSON assignments against the shared JSON runtime."""
from pathlib import Path
import os
import runpy
import subprocess
import tempfile
import unittest
from tree_sitter import Language, Parser
import tree_sitter_cpp
from xbox360_platform.prism import walk
from xbox360_platform.cpp_maps import lower as lower_maps
from xbox360_platform.json_initializers import lower as lower_json

ROOT = Path(__file__).resolve().parents[1]


class InitializerTests(unittest.TestCase):
    def test_entry_order_arrays_and_failed_assignment(self):
        source = '''#include <nlohmann/json.hpp>
#include <cassert>
#include <type_traits>
typedef nlohmann::json json;
const std::map<int,int> entries = {{1,3},{1,7},{2,9}};
int fail() { throw 1; }
int main() {
    static_assert(std::is_const<decltype(entries)>::value, "const retained");
    assert(entries.at(1)==3 && entries.at(2)==9);
    json j; unsigned char bytes[3]={0,127,255};
    j=json{{"bytes",bytes},{"count",3}};
    assert(j.at("bytes").at(2).get<int>()==255 && j.at("count").get<int>()==3);
    try { j=json{{"replacement",1},{"failed",fail()}}; } catch(int) {}
    assert(j.contains("bytes") && !j.contains("replacement"));
}
'''
        parser = Parser(Language(tree_sitter_cpp.language()))
        source = lower_maps(source, parser, walk, ('entries',))
        source = lower_json(source, parser, walk, aliases=('json',), expected=2)
        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            runpy.run_path(str(ROOT/'python/xbox360_platform/json/backend.py'))['generate'](out/'json/generated')
            (out/'case.cpp').write_text(source, encoding='utf-8')
            batch = out/'compile.cmd'
            batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"'+str(ROOT/'sources/json/include')+
                '" /I"'+str(ROOT/'sources')+'" /I"'+str(out)+'" "'+str(ROOT/'sources/json/json.cpp')+
                '" case.cpp /Fe:case.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            self.assertEqual(subprocess.run([str(out/'case.exe')]).returncode, 0)

    def test_json_ambiguous_forms_fail(self):
        parser = Parser(Language(tree_sitter_cpp.language()))
        for value in ('json{}', 'json{{"x",1},{"x",2}}', 'json{{key,1}}'):
            with self.assertRaises(ValueError):
                lower_json('void f(){j='+value+';}', parser, walk, aliases=('json',), expected=1)
