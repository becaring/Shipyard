"""Offline Xenos shader pack (sources/xenos/shader_pack.h): generate, compile, write.

A consumer harvests program keys and builds sources/graphics/shader_pack_sources.cpp
with its own adapted feature decoder and Prism builder. generate() shards the
keys across that host generator; compile_sources() runs the XDK's fxc.exe on each
distinct lowered source; write_pack() writes format v3 (64-bit shaderId1 and a
per-program source digest), or v2 for 32-bit keys. Intermediate files are named
by content, so reruns are incremental.
"""

import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import zlib
from concurrent.futures import ThreadPoolExecutor

SDK = Path(os.environ.get('XEDK', 'C:/Program Files (x86)/Microsoft Xbox 360 SDK'))
PROFILES = (('vs_3_0', 'VSMain'), ('ps_3_0', 'PSMain'))
MAGIC, VERSION, VERSION2 = 0x5348504B, 3, 2
HEADER_BYTES = 64
ENTRY_BYTES = {VERSION: 76, VERSION2: 40}
THREE_POINT, SRGB = 1, 2


def sdk_define(header, name, sdk=SDK):
    match = re.search(r'#define\s+' + name + r'\s+(\d+)', (Path(sdk) / header).read_text(errors='replace'))
    if not match:
        raise ValueError(name + ' not found in ' + header)
    return int(match.group(1))


def sdk_versions(sdk=SDK):
    """(_XDK_VER, D3DX_SDK_VERSION), which the runtime requires the pack to match."""
    return (sdk_define('include/xbox/xdk.h', '_XDK_VER', sdk),
            sdk_define('include/xbox/d3dx9core.h', 'D3DX_SDK_VERSION', sdk))


def _head(id0, id1, flags, floats, version):
    return struct.pack('>QQII' if version == VERSION else '>QIII', id0, id1, flags, floats)


def program_checksum(id0, id1, flags, floats, vertex, pixel, version=VERSION):
    """CRC-32 over the key, float count and both blobs, as shader_cache.cpp verifies it."""
    crc = zlib.crc32(_head(id0, id1, flags, floats, version))
    crc = zlib.crc32(vertex, crc)
    return zlib.crc32(pixel, crc) & 0xFFFFFFFF


def write_pack(programs, path, abi, versions, version=VERSION):
    """programs: {(shaderId0, shaderId1, flags): (floats, vertex code, pixel code, lowered source bytes)}.

    versions is (_XDK_VER, D3DX_SDK_VERSION). The smallest key is the probe.
    Returns (programs, unique blobs, bytes).
    """
    if version not in ENTRY_BYTES:
        raise ValueError('unsupported shader pack version')
    if version == VERSION and abi not in (2, 3):
        raise ValueError('unsupported shader ABI')
    keys = sorted(programs)
    if not keys:
        raise ValueError('no shader programs to pack')
    if version == VERSION2 and any(id1 >> 32 for _, id1, _ in keys):
        raise ValueError('version 2 packs hold 32-bit shaderId1 keys only')
    entry_bytes = ENTRY_BYTES[version]
    offsets, blobs = {}, []
    position = HEADER_BYTES + entry_bytes * len(keys)

    def place(code):
        nonlocal position
        if not code or len(code) % 4:
            raise ValueError('microcode size is not a positive multiple of 4')
        digest = hashlib.sha256(code).digest()
        if digest not in offsets:
            offsets[digest] = position
            blobs.append(code)
            position += len(code)
        return offsets[digest]

    index = bytearray()
    for id0, id1, flags in keys:
        floats, vertex, pixel, source = programs[(id0, id1, flags)]
        if flags & ~(THREE_POINT | SRGB) or not floats:
            raise ValueError('invalid program flags or float count')
        index += _head(id0, id1, flags, floats, version)
        index += struct.pack('>IIIII', place(vertex), len(vertex), place(pixel), len(pixel),
                             program_checksum(id0, id1, flags, floats, vertex, pixel, version))
        if version == VERSION:
            index += hashlib.sha256(source).digest()
    data = bytearray(struct.pack('>8I', MAGIC, version, versions[0], versions[1], len(keys), 0,
                                 abi if version == VERSION else 0, 0))
    data += hashlib.sha256(programs[keys[0]][3]).digest()
    data += index
    for blob in blobs:
        data += blob
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_bytes(bytes(data))
    os.replace(temporary, path)
    return len(keys), len(blobs), len(data)


def read_pack(path):
    """Header and entries of a pack, checked as the runtime checks them (for reports and tests)."""
    data = Path(path).read_bytes()
    magic, version, xdk, d3dx, count, probe, abi, _ = struct.unpack_from('>8I', data)
    if magic != MAGIC or version not in ENTRY_BYTES or not count or probe >= count:
        raise ValueError('not a shader pack')
    entries = []
    for i in range(count):
        at = HEADER_BYTES + i * ENTRY_BYTES[version]
        if version == VERSION:
            id0, id1, flags, floats = struct.unpack_from('>QQII', data, at)
            rest, digest = at + 24, data[at + 44:at + 76]
        else:
            id0, id1, flags, floats = struct.unpack_from('>QIII', data, at)
            rest, digest = at + 20, None
        vo, vs, po, ps, crc = struct.unpack_from('>IIIII', data, rest)
        vertex, pixel = data[vo:vo + vs], data[po:po + ps]
        if crc != program_checksum(id0, id1, flags, floats, vertex, pixel, version):
            raise ValueError('shader pack entry %d checksum mismatch' % i)
        entries.append(dict(key=(id0, id1, flags), floats=floats, vertex=vertex, pixel=pixel, digest=digest))
    if [e['key'] for e in entries] != sorted(set(e['key'] for e in entries)):
        raise ValueError('shader pack index is not strictly ascending')
    return dict(version=version, xdk=xdk, d3dx=d3dx, abi=abi, probe=probe, probe_digest=data[32:64],
                entries=entries)


def generate(executable, template, keys, variants, abi, jobs, out):
    """Lowered sources for every key and variant, sharded over jobs generator processes.

    keys: (shaderId0, shaderId1) pairs; variants: (three-point, sRGB) 0/1 pairs.
    Returns ({(id0, id1, three_point, srgb): (floats, source path)}, failure lines).
    """
    lines = ['%016x %016x %d %d' % (id0, id1, tp, srgb) for id0, id1 in sorted(keys) for tp, srgb in variants]
    out = Path(out)
    folder = out / 'generate'
    folder.mkdir(parents=True, exist_ok=True)
    processes = []
    for shard in range(max(1, jobs)):
        part = lines[shard::max(1, jobs)]
        if not part:
            continue
        keys_file = folder / ('keys-%02d.txt' % shard)
        keys_file.write_text('\n'.join(part) + '\n', encoding='ascii')
        sources = out / 'sources' / ('%02d' % shard)
        sources.mkdir(parents=True, exist_ok=True)
        command = [str(executable), str(template), str(keys_file), str(sources), str(abi)]
        processes.append((sources, subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                                    text=True)))
    result, failures = {}, []
    for sources, process in processes:
        stdout, stderr = process.communicate()
        if process.returncode:
            raise RuntimeError('shader source generator failed: ' + stderr)
        for line in stdout.splitlines():
            parts = line.split(' ', 6)
            if parts[0] == 'K':
                key = (int(parts[1], 16), int(parts[2], 16), int(parts[3]), int(parts[4]))
                result[key] = (int(parts[5]), sources / (parts[6] + '.hlsl'))
            elif parts[0] == 'F':
                failures.append(line)
    return result, failures


def compile_sources(paths, jobs, out, sdk=SDK):
    """fxc for every distinct source and profile; returns ({(path, profile): code}, error texts)."""
    fxc = Path(sdk) / 'bin/win32/fxc.exe'
    folder = Path(out) / 'code'
    folder.mkdir(parents=True, exist_ok=True)

    def one(item):
        path, (profile, entry) = item
        target = folder / (hashlib.sha256(path.read_bytes()).hexdigest()[:32] + '.' + profile + '.bin')
        if not target.is_file():
            temporary = str(target) + '.tmp'
            result = subprocess.run([str(fxc), '/nologo', '/T', profile, '/E', entry, '/Fo', temporary, str(path)],
                                    capture_output=True, text=True)
            if result.returncode:
                return (path, profile), None, result.stdout + result.stderr
            os.replace(temporary, target)
        return (path, profile), target.read_bytes(), None

    work = [(path, profile) for path in sorted(set(paths)) for profile in PROFILES]
    code, errors = {}, []
    with ThreadPoolExecutor(max(1, jobs)) as pool:
        for key, data, error in pool.map(one, work):
            if data is None:
                errors.append('%s %s\n%s' % (key[0], key[1], error))
            else:
                code[key] = data
    return code, errors


def build(executable, template, keys, abi, out, pack, jobs, variants=((1, 0), (0, 0)), sdk=SDK, versions=None):
    """Generate, compile and write a v3 pack; returns a build record.

    versions overrides the (_XDK_VER, D3DX_SDK_VERSION) read from sdk."""
    out = Path(out)
    sources, failures = generate(executable, template, keys, variants, abi, jobs, out)
    code, errors = compile_sources([path for _, path in sources.values()], jobs, out, sdk)
    programs, incomplete = {}, 0
    for (id0, id1, tp, srgb), (floats, path) in sources.items():
        vertex, pixel = code.get((path, 'vs_3_0')), code.get((path, 'ps_3_0'))
        if vertex is None or pixel is None:
            incomplete += 1
            continue
        programs[(id0, id1, tp * THREE_POINT | srgb * SRGB)] = (floats, vertex, pixel, path.read_bytes())
    (out / 'lowering-failures.txt').write_text('\n'.join(failures) + '\n', encoding='utf-8')
    (out / 'compile-failures.txt').write_text('\n'.join(errors) + '\n', encoding='utf-8')
    count, blobs, size = write_pack(programs, pack, abi, versions or sdk_versions(sdk))
    return dict(keys=len(keys), sources=len(set(path for _, path in sources.values())), programs=count,
                lowering_failures=len(failures), programs_without_code=incomplete, unique_microcode=blobs,
                bytes=size, abi=abi, version=VERSION, pack=str(pack))
