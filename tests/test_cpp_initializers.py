"""Execute declaration lowering across static and automatic initialization scopes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from tree_sitter import Language, Parser
import tree_sitter_cpp
from xbox360_platform.cpp_initializers import lower, lower_returns
from xbox360_platform.cpp_call_initializers import lower as lower_calls, lower_designated
from xbox360_platform.prism import walk


class InitializerTests(unittest.TestCase):
    def setUp(self):
        self.parser = Parser(Language(tree_sitter_cpp.language()))

    def test_runtime_evaluation_scope_and_nested_duplicates(self):
        source = '''#include <map>
#include <vector>
#include <utility>
#include <cassert>
int calls=0;
int next(){return ++calls;}
struct Pod { int first; int second; };
void take(Pod x, Pod y) { assert(x.first==1 && x.second==0 && y.first==2 && y.second==3); }
void takePod(Pod x) { assert(x.first==7 && x.second==0); }
struct Defaults { int first, second; Defaults():first(12),second(23){} };
void takeDefaults(Defaults x) { assert(x.first==12 && x.second==45); }
void takeNamed(int tag, Defaults x) { assert(tag==3 && x.first==12 && x.second==45); }
std::vector<int> values(int input) { return {input,input+1}; }
const std::vector<int> globalValues = {next(), next()};
int inspect(int input) {
    static std::vector<int> empty = {};
    assert(empty.empty());
    static std::map<int,std::vector<int>> saved = {{1,{next(),input}},{1,{next(),99}}};
    std::vector<int> localValues = {input,next()};
    assert(saved.size()==1 && saved[1][0]==3 && saved[1][1]==7);
    assert(localValues[0]==input);
    return localValues[1];
}
int main(){takePod(Pod{.first=7}); takeDefaults({.second=45});
takeNamed(3, {.second=45});
assert(values(8)[1]==9);
take({1}, {2,3}); assert(calls==2 && globalValues[0]==1 && globalValues[1]==2);
assert(inspect(7)==5); assert(inspect(8)==6); assert(calls==6);}
'''
        generated = lower(source, self.parser, walk, {
            'globalValues': ('std::vector<int>', 1),
            'saved': ('std::map<int,std::vector<int>>', 1),
            'empty': ('std::vector<int>', 1),
            'localValues': ('std::vector<int>', 1)})
        self.assertIn('empty = std::vector<int>()', generated)
        generated = lower_calls(generated, self.parser, walk, function='take', types=('Pod','Pod'), expected=1)
        generated = lower_designated(generated, self.parser, walk, function='takePod',
            type_name='Pod', fields=('first',), expected=1)
        generated = lower_designated(generated, self.parser, walk, function='takeDefaults',
            type_name='Defaults', fields=('second',), expected=1)
        generated = lower_returns(generated, self.parser, walk, {'values': ('std::vector<int>', 1)})
        generated = lower_designated(generated, self.parser, walk, function='takeNamed',
            type_name='Defaults', fields=[('second',)], expected=1, argument=1, arity=2)
        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder); (out/'case.cpp').write_text(generated)
            batch = out/'compile.cmd'
            batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT case.cpp /Fe:case.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            self.assertEqual(subprocess.run([str(out/'case.exe')]).returncode, 0)

    def test_unknown_forms_fail(self):
        for source in ('std::vector<int> values;', 'std::vector<float> values={1};',
                       'void f(){std::vector<int> values(3);}', 'struct A{std::vector<int> values={1};};'):
            with self.assertRaises(ValueError):
                lower(source, self.parser, walk, {'values': ('std::vector<int>', 1)})
        with self.assertRaises(ValueError):
            lower_designated('void f(){take({.second=1});}', self.parser, walk,
                function='take', type_name='Pod', fields=('first',), expected=1)
        with self.assertRaises(ValueError):
            lower_returns('std::vector<float> f(){return {1};}', self.parser, walk,
                {'f': ('std::vector<int>', 1)})
