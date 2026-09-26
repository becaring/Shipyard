"""Execute nested container construction and pair binding semantics."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from tree_sitter import Language, Parser
import tree_sitter_cpp
from xbox360_platform.cpp_bindings import lower_pairs
from xbox360_platform.cpp_containers import materialize, lower_constructor_arguments
from xbox360_platform.prism import walk, lower_ranges


class ContainerTests(unittest.TestCase):
    def setUp(self):
        self.parser = Parser(Language(tree_sitter_cpp.language()))

    def initializer(self, text):
        tree = self.parser.parse(('void f(){target='+text+';}').encode())
        return next(n for n in walk(tree.root_node) if n.type == 'initializer_list')

    def test_runtime_nested_maps_and_binding_ownership(self):
        declarations, result = materialize('std::map<int, std::vector<std::pair<int, int>>>',
            self.initializer('{{1,{{2,3},{4,5}}},{1,{{9,9}}},{2,{}}}'), prefix='value')
        source = '''#include <map>
#include <vector>
#include <utility>
#include <iterator>
#include <cassert>
#include <set>
struct Registration {
    std::set<int> values;
    Registration(int, const std::set<int>& input) : values(input) {}
};
Registration registered(1, {2, 2, 3});
int main(){
assert(registered.values.size()==2 && registered.values.count(3)==1);
''' + declarations + '\nauto& values = '+result+''';
assert(values.size()==2 && values[1].size()==2 && values[1][0].second==3);
for(auto [key, rows] : values) { rows.clear(); }
assert(values[1].size()==2);
for(auto& [key, rows] : values) { for(auto& [first, second] : rows) second += first; }
assert(values[1][0].second==5 && values[1][1].second==9);
auto [position, inserted] = values.insert(std::make_pair(3,std::vector<std::pair<int,int>>()));
assert(inserted && position->first==3);
const auto& [key, rows] = *values.begin();
assert(key==1 && rows.size()==2);
}
'''
        source = lower_pairs(source, self.parser, walk, expected=5)
        source = lower_constructor_arguments(source, self.parser, walk, constructor='Registration',
            argument=1, type_name='std::set<int>', expected=1)
        source = lower_ranges(source.encode(), self.parser, 'std::begin', 'std::end').decode()
        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            (out/'case.cpp').write_text(source, encoding='utf-8')
            batch = out/'compile.cmd'
            batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT case.cpp /Fe:case.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            self.assertEqual(subprocess.run([str(out/'case.exe')]).returncode, 0)

    def test_unsupported_forms_fail(self):
        with self.assertRaises(ValueError):
            materialize('Unknown', self.initializer('{1,2}'), prefix='x')
        with self.assertRaises(ValueError):
            materialize('std::pair<int,int>', self.initializer('{1}'), prefix='x')
        for source in ('void f(){auto [x,y,z]=v;}', 'void f(){auto&& [x,y]=v;}', 'void f(){}'):
            with self.assertRaises(ValueError):
                lower_pairs(source, self.parser, walk, expected=1)
        for source in ('void f(){Registration value(1, {2});}', 'Registration value(1, values);'):
            with self.assertRaises(ValueError):
                lower_constructor_arguments(source, self.parser, walk, constructor='Registration',
                    argument=1, type_name='std::set<int>', expected=1)
