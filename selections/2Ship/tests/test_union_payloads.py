"""Execute projected payload assignments and verify omitted fields are zeroed."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from tree_sitter import Language, Parser
import tree_sitter_cpp
from xbox360_platform.prism import walk
from xbox360_platform.cpp_aggregate import lower_union_payloads


class UnionPayloadTests(unittest.TestCase):
    def test_partial_initialization_and_assignment_reference(self):
        source = '''#include <cassert>
union Data { struct { int value; int* address; int omitted; } p; };
int main() {
    Data d; int x=3;
    d.p = { 4, &x };
    assert(d.p.value == 4 && d.p.address == &x && d.p.omitted == 0);
    auto& alias = d.p = { 9 };
    assert(&alias == &d.p && alias.value == 9 && alias.address == 0 && alias.omitted == 0);
}
'''
        parser = Parser(Language(tree_sitter_cpp.language()))
        projected = lower_union_payloads(source, parser, walk, union_name='Data', expected_members=1, expected_assignments=2)
        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'payload.cpp').write_text(projected, encoding='utf-8')
            batch = root/'compile.cmd'
            batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc payload.cpp /Fe:payload.exe\nexit /b %errorlevel%\n')
            result = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(root/'payload.exe')]).returncode, 0)

    def test_unknown_payload_fails(self):
        source = 'union Data { struct { int x; } p; }; void f(){other.q={1};}'
        with self.assertRaises(ValueError):
            lower_union_payloads(source, Parser(Language(tree_sitter_cpp.language())), walk,
                union_name='Data', expected_members=1, expected_assignments=1)
