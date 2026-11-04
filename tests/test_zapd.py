"""ZAPD extractor projection: lowering passes and the pinned-source XDK build."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor

from tree_sitter import Language, Parser
import tree_sitter_cpp

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform import zapd
from xbox360_platform.cpp_containers import lower_table_initializers
from xbox360_platform.cpp_syntax import scoped_enums
from xbox360_platform.prism import walk

XDK = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
SOURCES = os.environ.get('SHIPYARD_ZAPD_SOURCES')
TINYXML2 = os.environ.get('SHIPYARD_TINYXML2')


class ZapdLoweringTests(unittest.TestCase):
    def test_scoped_enum_keeps_names_scoped_and_base(self):
        result = scoped_enums('enum class Mode : uint8_t { A, B = 3 };\nMode m = Mode::B;')
        self.assertIn('struct Mode_scope { enum type : uint8_t { A, B = 3 }; };', result)
        self.assertIn('typedef Mode_scope::type Mode;', result)
        with self.assertRaises(ValueError):
            scoped_enums('enum class Broken;')

    def test_tables_fill_from_static_entries(self):
        parser = Parser(Language(tree_sitter_cpp.language()))
        result = lower_table_initializers(
            'static std::map<int, std::string> names = {{1, "one"}, {2, "two"}};\n'
            'std::vector<int> empty = {};\n', parser, walk)
        self.assertIn('struct entry { int first; std::string second; };', result)
        self.assertIn('static const entry entries[] = {{1, "one"}, {2, "two"}};', result)
        self.assertIn('static std::map<int, std::string> names = names_table::build();', result)
        self.assertIn('std::vector<int> empty;', result)


@unittest.skipUnless(SOURCES and TINYXML2 and (XDK / 'bin/win32/cl.exe').is_file(),
                     'Set SHIPYARD_ZAPD_SOURCES to a SoH checkout, SHIPYARD_TINYXML2 to tinyxml2 11, and install the XDK')
class ZapdXdkBuildTests(unittest.TestCase):
    def test_pinned_sources_project_and_compile_for_ppc(self):
        import tree_sitter_c
        from xbox360_platform import gfxd

        checkout = Path(SOURCES)
        parser = Parser(Language(tree_sitter_cpp.language()))
        with tempfile.TemporaryDirectory(prefix='shipyard-zapd-') as folder:
            out = Path(folder)
            args = (checkout / 'ZAPDTR', checkout / 'OTRExporter', checkout / 'libultraship',
                    checkout / 'soh/soh/resource/type/SohResourceType.h', checkout / 'soh/soh/resource/type/Animation.h')
            first = zapd.project(*args, out / 'src', parser)
            self.assertEqual(zapd.project(*args, out / 'again', parser)['outputs'], first['outputs'])
            sources = zapd.gfxd_sources(checkout / 'ZAPDTR/lib/libgfxd')
            (out / 'gfxd').mkdir()
            for name, text in sources.items():
                (out / 'gfxd' / name).write_bytes(text.encode())
            includes = [out / 'src/ZAPD', out / 'src/OTRExporter', out / 'src', Path(TINYXML2), out / 'gfxd',
                        checkout / 'libultraship/include', ROOT / 'sources', ROOT / 'sources/crt',
                        ROOT / 'sources/value_types', XDK / 'include/xbox']
            cl = str(XDK / 'bin/win32/cl.exe')
            common = ['/nologo', '/c', '/O2', '/D_XBOX', '/D_XBOX_VER=200', '/DNDEBUG']

            def compile_unit(unit):
                target = out / (unit.replace('/', '_') + '.obj')
                command = [cl, *common, '/EHsc', '/GR', '/DGAME_OOT', '/FIzapd/compat.h', '/Fo' + str(target)]
                command += ['/I' + str(path) for path in includes] + [str(out / 'src' / unit)]
                return unit, subprocess.run(command, cwd=out, capture_output=True, text=True), target

            def compile_gfxd(unit):
                intermediate = out / 'gfxd' / (unit + '.i')
                pre = subprocess.run([cl, '/nologo', '/TC', '/P', '/Fi' + str(intermediate), '/D_XBOX', '/D_XBOX_VER=200',
                                      '/DNDEBUG', '/I' + str(ROOT / 'sources/crt'), '/I' + str(ROOT / 'sources'),
                                      '/I' + str(XDK / 'include/xbox'), str(out / 'gfxd' / unit)],
                                     cwd=out, capture_output=True, text=True)
                if pre.returncode:
                    return unit, pre, None
                c_parser = Parser(Language(tree_sitter_c.language()))
                data, _ = gfxd.lower_preprocessed(intermediate.read_bytes(), unit, sources['gfxd.h'].encode(), c_parser, walk)
                lowered = out / 'gfxd' / ('lowered_' + unit)
                lowered.write_bytes(data)
                target = out / ('gfxd_' + unit + '.obj')
                return unit, subprocess.run([cl, *common, '/TC', '/Fo' + str(target), str(lowered)],
                                            cwd=out, capture_output=True, text=True), target

            with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
                results = list(pool.map(compile_unit, first['units'])) + list(pool.map(compile_gfxd, zapd.GFXD_UNITS))
            for unit, run, target in results:
                with self.subTest(unit=unit):
                    self.assertEqual(run.returncode, 0, run.stdout[-2000:] + run.stderr[-2000:])
                    self.assertEqual(target.read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
