"""Prebuilt shader pack: writer, runtime reader and verification, harvest and key harness."""

import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform import host_toolchain, shader_pack, shader_pack_harvest as harvest, shader_pack_keys as keys

VERSIONS = (21256, 43)  # tests/xenos-sdk


def source(id0, id1, flags):
    return b'source %016x %016x %d %d' % (id0, id1, flags & 1, flags >> 1 & 1)


def programs(keys_):
    result = {}
    for id0, id1, flags in keys_:
        vertex = b'VS%02x' % (id0 & 0xff) + b'\0' * 4
        pixel = b'PS%02x' % (flags & 0xff)
        result[(id0, id1, flags)] = ((id0 & 0xff) + 4, vertex, pixel, source(id0, id1, flags))
    return result


class WriterTests(unittest.TestCase):
    def test_round_trip_and_checksum(self):
        wide = [(0x11, 0xfffffffffffe0000, 1), (0x11, 0xfffffffffffe0000, 0), (0x22, 0x100, 3)]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'a.shaderpack'
            count, blobs, size = shader_pack.write_pack(programs(wide), path, 3, VERSIONS)
            pack = shader_pack.read_pack(path)
            self.assertEqual((count, size), (3, path.stat().st_size))
            self.assertEqual((pack['version'], pack['abi'], pack['probe']), (3, 3, 0))
            self.assertEqual([e['key'] for e in pack['entries']], sorted(wide))
            self.assertEqual(pack['probe_digest'], pack['entries'][0]['digest'])
            self.assertEqual(blobs, 5)  # the two 0x11 entries share their vertex code
            data = bytearray(path.read_bytes())
            data[-1] ^= 1
            path.write_bytes(bytes(data))
            with self.assertRaises(ValueError):
                shader_pack.read_pack(path)
        crc = zlib.crc32(struct.pack('>QQII', 1, 2, 3, 4))
        crc = zlib.crc32(b'p', zlib.crc32(b'v', crc))
        self.assertEqual(shader_pack.program_checksum(1, 2, 3, 4, b'v', b'p'), crc)

    def test_version_2_holds_only_32_bit_keys(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'b.shaderpack'
            with self.assertRaises(ValueError):
                shader_pack.write_pack(programs([(1, 1 << 40, 0)]), path, 2, VERSIONS, version=2)
            shader_pack.write_pack(programs([(1, 7, 0)]), path, 2, VERSIONS, version=2)
            pack = shader_pack.read_pack(path)
            self.assertEqual((pack['version'], pack['abi'], pack['entries'][0]['digest']), (2, 0, None))


class RuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(prefix='shipyard-shader-pack-')
        out = Path(cls.folder.name)
        cls.executable = out / 'contract.exe'
        host_toolchain.run([['cl', '/nologo', '/EHsc', '/MT', '/I' + str(ROOT / 'tests/xenos-sdk'),
                             '/I' + str(ROOT / 'sources'), ROOT / 'tests/shader_pack_contract.cpp',
                             ROOT / 'sources/xenos/shader_cache.cpp', '/Fe:' + str(cls.executable)]], out, 'contract')
        cls.keys = [(0x10, 0xfffffffffffe0000, 1), (0x20, 0xfffffffffffe0100, 0), (0x30, 0x5, 3)]

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def run_pack(self, name, abi, mode, differ, queries, version=3, versions=VERSIONS, corrupt=None):
        path = Path(self.folder.name) / name
        shader_pack.write_pack(programs(self.keys), path, 3, versions, version=version)
        if corrupt is not None:
            data = bytearray(path.read_bytes())
            data[corrupt] ^= 0x40
            path.write_bytes(bytes(data))
        arguments = ['%x:%x:%d' % key for key in queries]
        result = subprocess.run([str(self.executable), str(path), str(abi), mode, '%x' % differ] + arguments,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lines = result.stdout.splitlines()
        hits = [line.split()[1] for line in lines if line.startswith('hit ')]
        return lines[0], hits, lines[-1]

    def label(self, key):
        return '%x:%x:%d' % key

    def test_matching_build_uses_every_program(self):
        configured, hits, counts = self.run_pack('match.shaderpack', 3, 'match', 0, self.keys + [(0x40, 0, 0)])
        self.assertEqual(configured, 'configured 1')
        self.assertEqual(hits, [self.label(k) for k in self.keys])
        self.assertEqual(counts, 'counts 6 0')

    def test_changed_program_source_or_floats_is_compiled_instead(self):
        for mode in ('source', 'floats'):
            _, hits, counts = self.run_pack(mode + '.shaderpack', 3, mode, 0x20, self.keys)
            self.assertEqual(hits, [self.label(self.keys[0]), self.label(self.keys[2])])
            self.assertEqual(counts, 'counts 4 1')

    def test_changed_probe_or_abi_disables_the_pack(self):
        _, hits, _ = self.run_pack('probe.shaderpack', 3, 'source', 0x10, self.keys)
        self.assertEqual(hits, [])
        _, hits, _ = self.run_pack('abi.shaderpack', 2, 'match', 0, self.keys)
        self.assertEqual(hits, [])

    def test_corrupt_or_foreign_packs_are_rejected(self):
        configured, hits, _ = self.run_pack('xdk.shaderpack', 3, 'match', 0, self.keys, versions=(1, 43))
        self.assertEqual((configured, hits), ('configured 0', []))
        _, hits, _ = self.run_pack('crc.shaderpack', 3, 'match', 0, self.keys, corrupt=-1)
        self.assertEqual(hits, [self.label(k) for k in self.keys[:2]])
        # The probe's own code is checked before anything is used.
        size = shader_pack.HEADER_BYTES + shader_pack.ENTRY_BYTES[3] * 3
        _, hits, _ = self.run_pack('probe-crc.shaderpack', 3, 'match', 0, self.keys, corrupt=size + 1)
        self.assertEqual(hits, [])

    def test_version_2_relies_on_the_probe(self):
        narrow = [(0x10, 0x7, 1), (0x20, 0x8, 0)]
        self.keys, saved = narrow, self.keys
        try:
            _, hits, _ = self.run_pack('v2.shaderpack', 3, 'source', 0x20, narrow, version=2)
            self.assertEqual(hits, [self.label(k) for k in narrow])
            _, hits, _ = self.run_pack('v2-probe.shaderpack', 3, 'source', 0x10, narrow, version=2)
            self.assertEqual(hits, [])
        finally:
            self.keys = saved


def otr(kind, payload):
    return struct.pack('<II', 0, kind) + b'\0' * 56 + payload


def words(*commands):
    return b''.join(struct.pack('<II', w0, w1) for w0, w1 in commands)


class HarvestTests(unittest.TestCase):
    def test_display_lists_follow_otr_calls_and_raw_lists_are_found(self):
        child = otr(harvest.DISPLAY_LIST, bytes([4]) + b'\0' * 7 + words(
            (0xFC000000 | 0x121824, 0xFF33FFFF), (0x05000000, 0), (0xDF000000, 0)))
        name_hash = harvest.crc64('lists/child')
        parent = otr(harvest.DISPLAY_LIST, bytes([4]) + b'\0' * 7 + words(
            (0xEF000000 | (2 << 20), 0x00552078), (0x31000000, 0), (name_hash >> 32, name_hash & 0xFFFFFFFF),
            (0xE4000000, 0x01000000), (0xDF000000, 0)))
        raw = struct.pack('<II', 0, 0x52454C4F) + b'\0' * 56 + b'\x12' * 8 + struct.pack(
            '>IIIIII', 0xFC000000 | 0x41, 0x2, 0x06000000, 0, 0xDF000000, 0)
        with tempfile.TemporaryDirectory() as folder:
            archive = Path(folder) / 'a.o2r'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('lists/parent', parent)
                z.writestr('lists/child', child)
                z.writestr('data/relo', raw)
            lists, records = harvest.display_list_records([archive], 1, raw_types={0x52454C4F})
        self.assertEqual(lists, 3)
        combines = {(r[0], r[1]) for r in records}
        child_combine = harvest.decode_combine(0xFC121824, 0xFF33FFFF)
        self.assertIn(('tri', child_combine), combines)  # child run directly and from the parent
        texrect = [r for r in records if r[0] == 'texrect']
        self.assertEqual(len(texrect), 1)
        self.assertEqual(texrect[0][2], (2 << (32 + 20)) | 0x00552078)
        self.assertIn(('tri', harvest.decode_combine(0xFC000041, 0x2)), combines)

    def test_expansion_fills_unknown_state_from_seeds(self):
        records = {('tri', None, 0, 0, 0, (), False), ('tri', 5, 0x10, 0x10, 0, (), False)}
        states = harvest.expand(records, {(7, 0x1)}, {0x2}, 1, 0xFF)
        self.assertIn(('tri', 7, 0x1, False, 0, ()), states)
        self.assertIn(('tri', 7, 0x2, False, 0, ()), states)
        self.assertIn(('tri', 5, 0x12, False, 0, ()), states)


FIXTURE_HEADER = '''enum {
    SHADER_0,
    SHADER_INPUT_1,
};
enum class ShaderOpts {
    ALPHA,
    FOG,
    TEXEL0_CLAMP_S = 8,
};
#define SHADER_OPT(opt) ((uint64_t)(1 << static_cast<int>(ShaderOpts::opt)))
constexpr size_t SHADER_ID_SHIFT = 17;
'''
FIXTURE_INTERPRETER = '''void Interpreter::GenerateCC(ColorCombiner* comb, const ColorCombinerKey& key) {
    comb->shader_id0 = key.combine_mode;
    comb->shader_id1 = key.options;
    comb->usedTextures[0] = (key.combine_mode & 1) != 0;
    comb->usedTextures[1] = false;
}
void Interpreter::GfxSpTri1() {
    uint64_t cc_options = 0;
    if ((mRdp->other_mode_l & FIXTURE_FOG_BIT) != 0) {
        cc_options |= SHADER_OPT(FOG);
    }
    if (mShaderStack.empty()) {
        cc_options |= -1 << SHADER_ID_SHIFT;
    }
    ColorCombinerKey key;
}
'''


class KeyHarnessTests(unittest.TestCase):
    def test_lifted_interpreter_derives_keys_with_clamp_masks(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            (out / 'fixture_gbi.h').write_text('#define FIXTURE_FOG_BIT 0x40000000u\n', encoding='utf-8')
            program = keys.harness_source(FIXTURE_INTERPRETER, FIXTURE_HEADER, gbi='fixture_gbi.h')
            executable = keys.build_harness(program, [out], out)
            states = {('tri', 0x3, 0x40000000, False, 0, ((0, (2, 0)),)),
                      ('fillrect', 0x8, 3 << 52, False, 0, ())}
            derived = keys.derive(executable, states)
        high = 0xfffffffffffe0000
        self.assertEqual(derived, {(0x3, high | 2), (0x3, high | 2 | 0x100), (harvest.FILL_COMBINE, high)})

    def test_clamp_masks_follow_tile_modes(self):
        self.assertEqual(keys.clamp_masks((True, False), 0, {0: (3, 0)}), {1})
        self.assertEqual(keys.clamp_masks((True, False), 0, {0: (0, 0)}), {0})
        self.assertEqual(keys.clamp_masks((False, True), 2, {3: (0, 0), 2: (1 | 2, 0)}), {0, 4})


@unittest.skipUnless((shader_pack.SDK / 'bin/win32/fxc.exe').is_file(), 'Xbox 360 SDK fxc.exe is required')
class BuildTests(unittest.TestCase):
    def test_generator_compiles_and_packs_each_lowered_source(self):
        fixture = ROOT / 'tests/shader-pack-generator'
        with tempfile.TemporaryDirectory(prefix='shipyard-pack-build-') as folder:
            out = Path(folder)
            executable = out / 'shader_pack_sources.exe'
            host_toolchain.run([['cl', '/nologo', '/std:c++17', '/EHsc', '/MD', '/O1', '/I' + str(fixture),
                                 '/I' + str(ROOT / 'sources'), ROOT / 'sources/graphics/shader_pack_sources.cpp',
                                 ROOT / 'sources/graphics/shader_lower.cpp', '/Fo:' + str(out) + '/',
                                 '/Fe:' + str(executable)]], out, 'generator')
            wanted = {(0x11, 0xfffffffffffe0000), (0x22, 0x100), (0xbad, 0)}
            record = shader_pack.build(executable, fixture / 'template.hlsl', wanted, 3, out / 'work',
                                       out / 'test.shaderpack', 2, versions=VERSIONS)
            self.assertEqual((record['programs'], record['lowering_failures'], record['sources']), (4, 2, 4))
            pack = shader_pack.read_pack(out / 'test.shaderpack')
            self.assertEqual(pack['abi'], 3)
            for entry in pack['entries']:
                id0, id1, flags = entry['key']
                self.assertEqual(entry['floats'], 4 + (id0 & 3))
                lowered = [p for p in (out / 'work/sources').rglob('*.hlsl')
                           if ('// %016x %016x %d 0' % (id0, id1, flags & 1)).encode() in p.read_bytes()]
                self.assertEqual(len(lowered), 1)
                self.assertEqual(entry['digest'], hashlib.sha256(lowered[0].read_bytes()).digest())
                self.assertTrue(entry['vertex'] and entry['pixel'])
            # The renderer callback, built with the pack capability set, uses the pack
            # only while it regenerates the same sources.
            contract = out / 'program_contract.exe'
            host_toolchain.run([['cl', '/nologo', '/std:c++17', '/EHsc', '/MD', '/DSHIPYARD_XENOS_SHADER_PACK=1',
                                 '/DSHIPYARD_XENOS_SHADER_ABI=3', '/I' + str(fixture), '/I' + str(ROOT / 'tests/xenos-sdk'),
                                 '/I' + str(ROOT / 'sources'), fixture / 'program_contract.cpp',
                                 ROOT / 'sources/xenos/shader_pack_program.cpp', ROOT / 'sources/xenos/shader_cache.cpp',
                                 ROOT / 'sources/graphics/shader_lower.cpp', '/Fo:' + str(out) + '/',
                                 '/Fe:' + str(contract)]], out, 'program')
            queries = ['11:fffffffffffe0000:0:0', '11:fffffffffffe0000:2:0', '22:100:1:0', '22:100:1:1', '33:0:0:0']
            changed = out / 'changed.hlsl'
            changed.write_bytes((fixture / 'template.hlsl').read_bytes() + b'\n')
            for templ, expected in ((fixture / 'template.hlsl', queries[:3]), (changed, [])):
                run = subprocess.run([str(contract), str(templ), str(out / 'test.shaderpack')] + queries,
                                     capture_output=True, text=True, timeout=10)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertEqual([line.split()[1] for line in run.stdout.splitlines() if line.startswith('hit ')],
                                 expected)


if __name__ == '__main__':
    unittest.main()
