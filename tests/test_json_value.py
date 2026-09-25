import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class JsonValueTests(unittest.TestCase):
    def test_value_single_search_and_lazy_fallback(self):
        header = (ROOT/'sources/json/include/nlohmann/json.hpp').read_text()
        method = header[header.index('    template <class T> T value('):header.index('    iterator find(')]
        self.assertEqual(method.count('FindValue(key)'), 2)
        self.assertNotIn('contains(key)', method)
        source = (ROOT/'sources/json/json.cpp').read_text()
        helper = source[source.index('const json* json::FindValue('):source.index('\nsize_t json::size()', source.index('const json* json::FindValue('))]
        self.assertEqual(helper.count('object_->find(k)'), 1)
        self.assertIn('found ? found->get<std::string>() : std::string(fallback)', method)

        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-json-value-') as folder:
            out = Path(folder)
            generated = out/'json/generated'
            runpy.run_path(str(ROOT/'python/xbox360_platform/json/backend.py'))['generate'](generated)
            ordered = (out/'json/generated/ordered_json.hpp').read_text()
            self.assertEqual(ordered.count('FindValue(key)'), 2)
            batch = out/'compile.cmd'
            batch.write_text(
                '@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /EHsc /MT /I"'+str(ROOT/'sources/json/include')+'" /I"'+str(ROOT/'sources')+'" /I"'+str(out)+
                '" "'+str(ROOT/'sources/json/json.cpp')+'" "'+str(ROOT/'tests/json_value_contract.cpp')+
                '" /Fe"'+str(out/'contract.exe')+'"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            result = subprocess.run([str(out/'contract.exe')], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('JSON value semantics/allocation PASS', result.stdout)
