import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.parallel_build import (plan, plan_link_view, remove_shards, run,
                                             verify_receipt, write_shards)


NS = 'http://schemas.microsoft.com/developer/msbuild/2003'
N = '{' + NS + '}'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class ParallelBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project_dir = self.root / 'project'
        self.project_dir.mkdir()
        self.source_dir = self.project_dir / 'src'
        self.source_dir.mkdir()
        self.header = self.source_dir / 'shared.h'
        self.header.write_text('#pragma once\n', encoding='utf-8')
        self.project = self.project_dir / 'Game.vcxproj'
        root = ET.Element(N + 'Project')
        globals_group = ET.SubElement(root, N + 'PropertyGroup', Label='Globals')
        ET.SubElement(globals_group, N + 'ProjectGuid').text = '{11111111-1111-1111-1111-111111111111}'
        properties = ET.SubElement(root, N + 'PropertyGroup')
        ET.SubElement(properties, N + 'IntDir').text = '$(ProjectDir)obj\\'
        definitions = ET.SubElement(root, N + 'ItemDefinitionGroup')
        compiler = ET.SubElement(definitions, N + 'ClCompile')
        ET.SubElement(compiler, N + 'Optimization').text = 'MaxSpeed'
        group = ET.SubElement(root, N + 'ItemGroup')
        self.manifest = []
        for index in range(4):
            source = self.source_dir / ('unit%d.cpp' % index)
            source.write_text('#include "shared.h"\nint unit%d() { return %d; }\n' % (index, index), encoding='utf-8')
            include = '$(ProjectDir)src\\' + source.name
            item = ET.SubElement(group, N + 'ClCompile', Include=include)
            ET.SubElement(item, N + 'ObjectFileName').text = '$(IntDir)%04d_unit%d.obj' % (index, index)
            ET.SubElement(item, N + 'AdditionalOptions').text = '/fp:precise'
            self.manifest.append({
                'include': include, 'source': str(source),
                'object': str(self.root / 'objects' / ('%04d_unit%d.obj' % (index, index))),
                'sha256': sha(source),
            })
        self.project.write_bytes(ET.tostring(root, encoding='utf-8', xml_declaration=True))
        self.project_bytes = self.project.read_bytes()
        self.output = self.root / 'build'
        self.fake = self.root / 'fake_msbuild.py'
        self.fake.write_text('''import pathlib, struct, sys, time, xml.etree.ElementTree as ET
project=pathlib.Path(sys.argv[1])
root=ET.parse(project).getroot()
ns='{http://schemas.microsoft.com/developer/msbuild/2003}'
folder=pathlib.Path(root.find('.//'+ns+'IntDir').text)
folder.mkdir(parents=True,exist_ok=True)
lines=[]
time.sleep(0.35)
for item in root.findall('.//'+ns+'ClCompile[@Include]'):
    source=pathlib.Path(item.attrib['Include'].replace('$(ProjectDir)',str(project.parent)+'\\\\'))
    if b'FAIL' in source.read_bytes():
        sys.exit(1)
    obj=pathlib.Path(item.find(ns+'ObjectFileName').text)
    obj.parent.mkdir(parents=True,exist_ok=True)
    if not obj.exists():
        obj.write_bytes(struct.pack('<H',0x1f2)+b'PPC-OBJECT'+pathlib.Path(sys.argv[-1]).read_bytes())
    lines.extend(['^'+str(source),sys.argv[-1]])
(folder/'CL.read.1.tlog').write_text('\\n'.join(lines)+'\\n',encoding='utf-16')
''', encoding='utf-8')

    def build(self):
        return plan(self.project, self.manifest, self.output, jobs=2)

    def rewrite(self, index, text):
        source = Path(self.manifest[index]['source'])
        source.write_text(text, encoding='utf-8')
        self.manifest[index]['sha256'] = sha(source)

    def edit_project(self, change):
        root = ET.parse(self.project).getroot()
        change(root)
        self.project.write_bytes(ET.tostring(root, encoding='utf-8', xml_declaration=True))

    def compile(self, arguments=None, check=True):
        build = self.build()
        receipt = run(build, [sys.executable, str(self.fake)],
                      arguments=arguments or [str(self.header)], owned_roots=[self.root])
        if check:
            self.assertEqual(receipt['build_exit'], 0, '\n'.join(Path(row['log']).read_text()
                                                         for row in receipt['builds']))
            verify_receipt(build, receipt)
        return build, receipt

    @staticmethod
    def compiled(receipt):
        return {include.rsplit('\\', 1)[-1] for row in receipt['builds'] for include in row['sources']}

    def objects(self):
        return [Path(row['object']).read_bytes() for row in self.manifest]

    ALL = {'unit0.cpp', 'unit1.cpp', 'unit2.cpp', 'unit3.cpp'}

    def test_first_build_compiles_everything_then_reuses_all(self):
        stale = Path(self.manifest[0]['object'])
        stale.parent.mkdir(parents=True)
        stale.write_bytes(struct.pack('<H', 0x1f2) + b'stale')
        build, first = self.compile()
        self.assertEqual(self.compiled(first), self.ALL)
        self.assertEqual((first['reused_objects'], first['invalidated_objects']), (0, 1))
        self.assertNotIn(b'stale', stale.read_bytes())
        build, second = self.compile()
        self.assertEqual((second['builds'], second['reused_objects']), ([], 4))
        self.assertTrue(all(row['reused'] for row in second['sources']))
        self.assertEqual(second['headers'], first['headers'])
        plan_link_view(build, second)
        self.assertTrue(all(not shard.project.exists() for shard in build.shards))

    def test_changed_source_recompiles_only_that_unit(self):
        self.compile()
        before = self.objects()
        self.rewrite(0, '#include "shared.h"\nint unit0() { return 5; }\n')
        _, receipt = self.compile()
        self.assertEqual(self.compiled(receipt), {'unit0.cpp'})
        self.assertEqual(self.objects()[1:], before[1:])

    def test_changed_header_recompiles_only_sources_that_include_it(self):
        other = self.source_dir / 'other.h'
        other.write_text('#define OTHER 1\n', encoding='utf-8')
        self.rewrite(1, '#include "shared.h"\n#include "other.h"\nint unit1() { return 1; }\n')
        (self.project_dir / 'a').mkdir()
        (self.project_dir / 'b').mkdir()
        (self.project_dir / 'b' / 'shadow.h').write_text('#define SHADOW 1\n', encoding='utf-8')
        self.rewrite(3, '#include "shadow.h"\nint unit3() { return 3; }\n')
        self.edit_project(lambda root: setattr(root.findall('.//' + N + 'AdditionalOptions')[3], 'text',
                                               '/fp:precise /I"$(ProjectDir)a" /I"$(ProjectDir)b"'))
        self.compile()
        other.write_text('#define OTHER 2\n', encoding='utf-8')
        _, receipt = self.compile()
        self.assertEqual(self.compiled(receipt), {'unit1.cpp'})
        # A new header earlier on the include path shadows the recorded one.
        (self.project_dir / 'a' / 'shadow.h').write_text('#define SHADOW 2\n', encoding='utf-8')
        _, receipt = self.compile()
        self.assertEqual(self.compiled(receipt), {'unit3.cpp'})
        self.header.write_text('#pragma once\n#define CHANGED 1\n', encoding='utf-8')
        _, receipt = self.compile()
        self.assertEqual(self.compiled(receipt), self.ALL)

    def test_changed_item_option_recompiles_only_that_item(self):
        first, _ = self.compile()
        before = self.objects()
        self.edit_project(lambda root: setattr(root.findall('.//' + N + 'AdditionalOptions')[2], 'text',
                                               '/fp:fast'))
        build, receipt = self.compile()
        self.assertNotEqual(build.layout_sha256, first.layout_sha256)
        self.assertEqual(self.compiled(receipt), {'unit2.cpp'})
        self.assertEqual([data for index, data in enumerate(self.objects()) if index != 2],
                         [data for index, data in enumerate(before) if index != 2])

    def test_changed_shared_definition_or_arguments_invalidates_all(self):
        self.compile()
        self.edit_project(lambda root: setattr(root.find('.//' + N + 'Optimization'), 'text', 'Disabled'))
        _, receipt = self.compile()
        self.assertEqual((self.compiled(receipt), receipt['invalidated_objects']), (self.ALL, 4))
        _, receipt = self.compile(arguments=['/p:Extra=1', str(self.header)])
        self.assertEqual(self.compiled(receipt), self.ALL)

    def test_deleted_or_tampered_object_is_recompiled(self):
        self.compile()
        Path(self.manifest[0]['object']).unlink()
        Path(self.manifest[1]['object']).write_bytes(struct.pack('<H', 0x1f2) + b'tampered')
        _, receipt = self.compile()
        self.assertEqual(self.compiled(receipt), {'unit0.cpp', 'unit1.cpp'})
        self.assertNotIn(b'tampered', Path(self.manifest[1]['object']).read_bytes())

    def test_failed_shard_keeps_objects_of_successful_shards(self):
        self.rewrite(0, '// FAIL\nint unit0() { return 0; }\n')
        build, failed = self.compile(check=False)
        self.assertEqual(failed['build_exit'], 1)
        self.assertEqual({Path(row['source']).name for row in failed['sources']}, {'unit1.cpp', 'unit3.cpp'})
        with self.assertRaisesRegex(ValueError, 'Successful current compile receipt'):
            verify_receipt(build, failed)
        self.rewrite(0, '#include "shared.h"\nint unit0() { return 0; }\n')
        _, receipt = self.compile()
        self.assertEqual(self.compiled(receipt), {'unit0.cpp', 'unit2.cpp'})

    def test_partition_preserves_source_and_canonical_objects(self):
        build = self.build()
        self.assertEqual([len(shard.sources) for shard in build.shards], [2, 2])
        write_shards(build)
        try:
            self.assertEqual(self.project.read_bytes(), self.project_bytes)
            paths = set()
            int_dirs = set()
            pdbs = set()
            for shard in build.shards:
                self.assertEqual(shard.project.parent, self.project.parent)
                root = ET.parse(shard.project).getroot()
                int_dirs.add(root.find('.//' + N + 'IntDir').text)
                pdbs.add(root.find('.//' + N + 'ProgramDataBaseFileName').text)
                self.assertEqual(root.find('.//' + N + 'Optimization').text, 'MaxSpeed')
                for item in root.findall('.//' + N + 'ClCompile[@Include]'):
                    paths.add(item.attrib['Include'])
                    self.assertEqual(item.find(N + 'AdditionalOptions').text, '/fp:precise')
                    self.assertTrue(Path(item.find(N + 'ObjectFileName').text).is_absolute())
            self.assertEqual(paths, {row['include'] for row in self.manifest})
            self.assertEqual(len(int_dirs), 2)
            self.assertEqual(len(pdbs), 2)
        finally:
            remove_shards(build)
        self.assertTrue(all(not shard.project.exists() for shard in build.shards))

    def test_parallel_runner_and_receipt(self):
        build = self.build()
        try:
            receipt = run(build, [sys.executable, str(self.fake)],
                          arguments=[str(self.header)], owned_roots=[self.root])
            self.assertEqual(receipt['build_exit'], 0, '\n'.join(Path(item['log']).read_text()
                                                         for item in receipt['builds']))
            self.assertEqual(len(receipt['sources']), 4)
            self.assertEqual(receipt['headers'][str(self.header.resolve())], sha(self.header))
            self.assertEqual(len({item['pid'] for item in receipt['builds']}), 2)
            self.assertLess(max(item['started_monotonic'] for item in receipt['builds']),
                            min(item['ended_monotonic'] for item in receipt['builds']))
            self.assertLess(max(item['seconds'] for item in receipt['builds']), 1.5)
            self.assertEqual(receipt, json.loads((self.output / 'compile-receipt.json').read_text()))
            verify_receipt(build, receipt)
            changed = json.loads(json.dumps(receipt))
            changed['sources'][0]['source_sha256'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'differs from manifest'):
                verify_receipt(build, changed)
            changed = json.loads(json.dumps(receipt))
            changed['headers'] = {}
            with self.assertRaisesRegex(ValueError, 'closure is absent'):
                verify_receipt(build, changed)
            self.header.write_text('#pragma once\n#define CHANGED 1\n', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'header changed'):
                verify_receipt(build, receipt)
            changed = json.loads(json.dumps(receipt))
            changed['builds'][0]['project_sha256'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'shard project changed'):
                verify_receipt(build, changed)
        finally:
            remove_shards(build)

    def test_generated_header_missing_from_tlog_invalidates_cached_objects(self):
        generated = self.project_dir / 'scratch' / 'generated headers'
        generated.mkdir(parents=True)
        header = generated / 'interpreter.h'
        header.write_text('struct Interpreter { int fields[2]; };\n', encoding='utf-8')
        root = ET.parse(self.project).getroot()
        ET.SubElement(root.find(N + 'PropertyGroup'), N + 'GeneratedRoot').text = (
            '$(MSBuildThisFileDirectory)scratch/generated headers')
        for item in root.findall('.//' + N + 'ClCompile[@Include]'):
            item.find(N + 'AdditionalOptions').text += ' /I"$(GeneratedRoot)"'
        self.project.write_bytes(ET.tostring(root, encoding='utf-8', xml_declaration=True))
        for index in range(4):
            self.rewrite(index, '#include "shared.h"\n#include "interpreter.h"\nint unit%d() { return %d; }\n'
                         % (index, index))
        build = self.build()
        # The fake compiler retains objects and omits this generated header from
        # its tlog, reproducing dependency tracking that missed an ABI change.
        fake = self.fake.read_text().replace("sys.argv[-1]])", "str(source.parent/'shared.h')])")
        self.fake.write_text(fake)
        try:
            def compile():
                return run(build, [sys.executable, str(self.fake)],
                           arguments=[str(header)], owned_roots=[self.root])
            first = compile()
            self.assertEqual(first['build_exit'], 0, '\n'.join(Path(row['log']).read_text() for row in first['builds']))
            obj = build.sources[0].object
            original = obj.read_bytes()
            self.assertIn(str(header.resolve()), first['headers'])
            self.assertEqual(compile()['invalidated_objects'], 0)
            header.write_text('struct Interpreter { int fields[8]; };\n', encoding='utf-8')
            changed = compile()
            self.assertEqual(changed['invalidated_objects'], len(build.sources))
            self.assertNotEqual(obj.read_bytes(), original)
            self.assertIn(b'fields[8]', obj.read_bytes())
            verify_receipt(build, changed)
            old = dict(changed)
            old.pop('header_inventory_version')
            (build.output / 'compile-receipt.json').write_text(json.dumps(old))
            self.assertEqual(compile()['invalidated_objects'], len(build.sources))
        finally:
            remove_shards(build)

    def test_link_view_consumes_verified_objects_without_compile_items(self):
        build = self.build()
        try:
            receipt = run(build, [sys.executable, str(self.fake)],
                          arguments=[str(self.header)], owned_roots=[self.root])
            view = plan_link_view(build, receipt)
            self.assertEqual(self.project.read_bytes(), self.project_bytes)
            root = ET.fromstring(view.contents)
            self.assertEqual(root.findall('.//' + N + 'ClCompile[@Include]'), [])
            actual = {item.attrib['Include'] for item in root.findall('.//' + N + 'Object[@Include]')}
            expected = {row['object'] for row in self.manifest}
            self.assertEqual(actual, expected)
            Path(self.manifest[0]['object']).write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'object changed'):
                plan_link_view(build, receipt)
        finally:
            remove_shards(build)

    def test_rejects_duplicate_objects_and_unlisted_project_items(self):
        changed = [dict(row) for row in self.manifest]
        changed[1]['object'] = changed[0]['object']
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            plan(self.project, changed, self.output, jobs=2)
        with self.assertRaisesRegex(ValueError, 'items differ'):
            plan(self.project, self.manifest[:-1], self.output, jobs=2)

    def test_finite_timeout_stops_shards_and_writes_failure_receipt(self):
        build = self.build()
        try:
            with self.assertRaisesRegex(ValueError, 'Finite positive'):
                run(build, [sys.executable, str(self.fake)],
                    arguments=[str(self.header)], owned_roots=[self.root], timeout=None)
            receipt = run(build, [sys.executable, str(self.fake)],
                          arguments=[str(self.header)], owned_roots=[self.root], timeout=0.05)
            self.assertEqual(receipt['build_exit'], -1)
            self.assertTrue(all(item['timed_out'] for item in receipt['builds']))
            self.assertLess(max(item['seconds'] for item in receipt['builds']), 2)
            with self.assertRaisesRegex(ValueError, 'Successful current compile receipt'):
                verify_receipt(build, receipt)
        finally:
            remove_shards(build)

    def test_source_change_keeps_incremental_shard_layout(self):
        before = self.build()
        write_shards(before)
        try:
            timestamps = [shard.project.stat().st_mtime_ns for shard in before.shards]
            self.source_dir.joinpath('unit0.cpp').write_text('int unit0() { return 5; }\n', encoding='utf-8')
            changed = [dict(row) for row in self.manifest]
            changed[0]['sha256'] = sha(changed[0]['source'])
            after = plan(self.project, changed, self.output, jobs=2)
            self.assertEqual(before.layout_sha256, after.layout_sha256)
            self.assertNotEqual(before.manifest_sha256, after.manifest_sha256)
            write_shards(after)
            self.assertEqual(timestamps, [shard.project.stat().st_mtime_ns for shard in after.shards])
        finally:
            remove_shards(before)

    def test_worker_count_changes_incremental_shard_layout(self):
        two = plan(self.project, self.manifest, self.output, jobs=2)
        four = plan(self.project, self.manifest, self.output, jobs=4)
        self.assertNotEqual(two.layout_sha256, four.layout_sha256)
        self.assertTrue({shard.intermediate for shard in two.shards}.isdisjoint(
            {shard.intermediate for shard in four.shards}))
        self.assertTrue({shard.project for shard in two.shards}.isdisjoint(
            {shard.project for shard in four.shards}))

    def test_rejects_project_and_shard_drift(self):
        build = self.build()
        write_shards(build)
        try:
            build.shards[0].project.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'Refusing to remove changed shard'):
                remove_shards(build)
            build.shards[0].project.write_bytes(build.shards[0].contents)
            self.project.write_bytes(self.project_bytes + b' ')
            with self.assertRaisesRegex(ValueError, 'Project changed'):
                write_shards(build)
        finally:
            remove_shards(build)


if __name__ == '__main__':
    unittest.main()
