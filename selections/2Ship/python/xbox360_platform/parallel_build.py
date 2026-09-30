"""Deterministic ClCompile shards for generated Xbox 360 VC projects."""

from concurrent.futures import ThreadPoolExecutor, as_completed
from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import time
import uuid
import xml.etree.ElementTree as ET


def _sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _key(value):
    return value.replace('\\', '/').casefold()


@dataclass(frozen=True)
class Source:
    include: str
    source: Path
    object: Path
    sha256: str


@dataclass(frozen=True)
class Shard:
    project: Path
    intermediate: Path
    log: Path
    sources: tuple
    contents: bytes


@dataclass(frozen=True)
class Plan:
    project: Path
    project_sha256: str
    manifest_sha256: str
    layout_sha256: str
    output: Path
    sources: tuple
    shards: tuple


@dataclass(frozen=True)
class LinkView:
    project: Path
    contents: bytes


def _xml_parts(root):
    if not root.tag.startswith('{'):
        raise ValueError('VC project has no XML namespace')
    namespace = root.tag.split('}', 1)[0] + '}'
    items = [item for group in root.findall(namespace + 'ItemGroup')
             for item in group.findall(namespace + 'ClCompile')]
    intermediate = root.findall('.//' + namespace + 'IntDir')
    definitions = root.findall('.//' + namespace + 'ItemDefinitionGroup/' + namespace + 'ClCompile')
    if len(intermediate) != 1 or len(definitions) != 1:
        raise ValueError('Expected one IntDir and one ClCompile definition group')
    return namespace, items, intermediate[0], definitions[0]


def plan(project, manifest, output, jobs=8):
    project = Path(project).resolve()
    output = Path(output).resolve()
    if not 1 <= jobs <= 32:
        raise ValueError('jobs must be between 1 and 32')
    records = []
    for row in manifest:
        source = Path(row['source']).resolve()
        obj = Path(row['object']).resolve()
        digest = row['sha256'].lower()
        if len(digest) != 64 or _sha(source) != digest:
            raise ValueError('Source hash mismatch: ' + str(source))
        if obj.suffix.lower() != '.obj':
            raise ValueError('Object path is not an .obj: ' + str(obj))
        records.append(Source(row['include'], source, obj, digest))
    if not records:
        raise ValueError('Empty ClCompile manifest')
    names = [_key(row.include) for row in records]
    objects = [str(row.object).casefold() for row in records]
    if len(set(names)) != len(names) or len(set(objects)) != len(objects):
        raise ValueError('Duplicate source item or object path')
    root = ET.parse(project).getroot()
    namespace, items, _, _ = _xml_parts(root)
    ET.register_namespace('', namespace[1:-1])
    selected = {_key(row.include): row for row in records}
    actual = [_key(item.attrib['Include']) for item in items]
    if len(actual) != len(set(actual)) or set(actual) != set(selected):
        raise ValueError('Project ClCompile items differ from manifest')
    for item in items:
        row = selected[_key(item.attrib['Include'])]
        obj = item.find(namespace + 'ObjectFileName')
        if obj is None or not obj.text or not obj.text.casefold().startswith('$(intdir)'):
            raise ValueError('Expected $(IntDir) ObjectFileName: ' + row.include)
        if obj.text[len('$(IntDir)'):].replace('\\', '/').casefold() != row.object.name.casefold():
            raise ValueError('ObjectFileName differs from canonical object: ' + row.include)
        if item.find(namespace + 'ProgramDataBaseFileName') is not None:
            raise ValueError('Per-item PDB override is not supported: ' + row.include)
    project_digest = _sha(project)
    payload = json.dumps({
        'project_sha256': project_digest,
        'sources': [(row.include, str(row.source), str(row.object), row.sha256) for row in records],
    }, sort_keys=True, separators=(',', ':')).encode()
    manifest_digest = hashlib.sha256(payload).hexdigest()
    layout = json.dumps({
        'project_sha256': project_digest,
        'sources': [(row.include, str(row.source), str(row.object)) for row in records],
    }, sort_keys=True, separators=(',', ':')).encode()
    layout_digest = hashlib.sha256(layout).hexdigest()
    count = min(jobs, len(records))
    shards = []
    for index in range(count):
        chosen = tuple(records[index::count])
        chosen_names = {_key(row.include) for row in chosen}
        clone = deepcopy(root)
        ns, _, intermediate, definitions = _xml_parts(clone)
        for group in clone.findall(ns + 'ItemGroup'):
            for item in list(group.findall(ns + 'ClCompile')):
                name = _key(item.attrib['Include'])
                if name not in chosen_names:
                    group.remove(item)
                else:
                    item.find(ns + 'ObjectFileName').text = str(selected[name].object)
        folder = output / ('layout-' + layout_digest[:16]) / ('shard-%02d' % index)
        intermediate.text = str(folder) + '\\'
        pdb = definitions.find(ns + 'ProgramDataBaseFileName')
        if pdb is None:
            pdb = ET.SubElement(definitions, ns + 'ProgramDataBaseFileName')
        pdb.text = str(folder / 'vc100.pdb')
        guid = clone.find('.//' + ns + 'ProjectGuid')
        if guid is not None:
            guid.text = '{' + str(uuid.uuid5(uuid.NAMESPACE_URL, str(project) + ':' + layout_digest + ':' + str(index))).upper() + '}'
        shard_path = project.with_name(project.stem + '.shipyard-' + layout_digest[:12] + '-%02d.vcxproj' % index)
        shards.append(Shard(shard_path, folder, folder / 'compile.log', chosen,
                            ET.tostring(clone, encoding='utf-8', xml_declaration=True)))
    return Plan(project, project_digest, manifest_digest, layout_digest, output, tuple(records), tuple(shards))


def write_shards(build):
    if _sha(build.project) != build.project_sha256:
        raise ValueError('Project changed after planning')
    for shard in build.shards:
        if shard.project.parent != build.project.parent:
            raise ValueError('Shard must retain the original ProjectDir')
        if shard.project.exists():
            if shard.project.read_bytes() != shard.contents:
                raise ValueError('Existing shard project differs: ' + str(shard.project))
        shard.intermediate.mkdir(parents=True, exist_ok=True)
        for row in shard.sources:
            row.object.parent.mkdir(parents=True, exist_ok=True)
        if not shard.project.exists():
            shard.project.write_bytes(shard.contents)


def remove_shards(build):
    for shard in build.shards:
        if shard.project.exists():
            if shard.project.read_bytes() != shard.contents:
                raise ValueError('Refusing to remove changed shard: ' + str(shard.project))
            shard.project.unlink()


def _invoke(shard, command_prefix, arguments, timeout):
    command = [*command_prefix, str(shard.project), '/t:ClCompile',
               '/p:BuildProjectReferences=false', *arguments]
    started = time.monotonic()
    with shard.log.open('w', encoding='utf-8') as log:
        process = subprocess.Popen(command, cwd=shard.project.parent,
                                   stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=os.name != 'nt')
        timed_out = False
        try:
            exit_code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            if os.name == 'nt':
                try:
                    subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   timeout=10, check=False)
                except (OSError, subprocess.TimeoutExpired):
                    pass
            else:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if process.poll() is None:
                process.kill()
            process.wait()
            exit_code = -1
    ended = time.monotonic()
    return {
        'project': str(shard.project), 'intermediate': str(shard.intermediate),
        'log': str(shard.log), 'project_sha256': _sha(shard.project),
        'command': command, 'pid': process.pid,
        'exit': exit_code, 'timed_out': timed_out,
        'started_monotonic': started, 'ended_monotonic': ended,
        'seconds': round(ended - started, 3),
    }


def _header_closure(build, roots):
    allowed = tuple(Path(root).resolve() for root in roots)
    headers = {}
    for shard in build.shards:
        expected = {str(row.source).casefold() for row in shard.sources}
        observed = set()
        active = False
        tlog = shard.intermediate / 'CL.read.1.tlog'
        for line in tlog.read_text(encoding='utf-16').splitlines():
            if line.startswith('^'):
                keys = {str(Path(name).resolve()).casefold() for name in line[1:].split('|')}
                if keys - expected:
                    raise ValueError('Compiler tlog contains unselected sources: ' + str(tlog))
                active = bool(keys & expected)
                observed.update(keys & expected)
            elif active:
                path = Path(line)
                if path.suffix.lower() in ('.h', '.hpp', '.inl', '.inc'):
                    resolved = path.resolve()
                    if any(resolved.is_relative_to(root) for root in allowed):
                        headers[str(resolved)] = _sha(resolved)
        if observed != expected:
            raise ValueError('Compiler tlog omits selected sources: ' + str(tlog))
    return dict(sorted(headers.items()))


def run(build, command_prefix, arguments=(), owned_roots=(), timeout=7200):
    if not command_prefix:
        raise ValueError('Compiler command prefix is empty')
    if not owned_roots:
        raise ValueError('Owned header roots are required for compiler dependency hashing')
    if timeout is None or timeout <= 0:
        raise ValueError('Finite positive per-shard timeout is required')
    for row in build.sources:
        if _sha(row.source) != row.sha256:
            raise ValueError('Source changed before compilation: ' + str(row.source))
    write_shards(build)
    with ThreadPoolExecutor(max_workers=len(build.shards)) as pool:
        futures = {pool.submit(_invoke, shard, tuple(command_prefix), tuple(arguments), timeout): index
                   for index, shard in enumerate(build.shards)}
        results = [None] * len(build.shards)
        for future in as_completed(futures):
            results[futures[future]] = future.result()
    receipt = {
        'project': str(build.project), 'project_sha256': build.project_sha256,
        'manifest_sha256': build.manifest_sha256, 'layout_sha256': build.layout_sha256,
        'builds': results,
        'build_exit': next((row['exit'] for row in results if row['exit']), 0),
        'sources': [], 'headers': {},
    }
    if receipt['build_exit'] == 0:
        if _sha(build.project) != build.project_sha256:
            raise ValueError('Project changed during compilation')
        for row in build.sources:
            if _sha(row.source) != row.sha256:
                raise ValueError('Source changed during compilation: ' + str(row.source))
            data = row.object.read_bytes()
            offset = 6 if data[:4] == b'\0\0\xff\xff' else 0
            if len(data) < offset + 2 or struct.unpack_from('<H', data, offset)[0] != 0x1f2:
                raise ValueError('Not a PPC COFF object: ' + str(row.object))
            receipt['sources'].append({
                'source': str(row.source), 'source_sha256': row.sha256,
                'object': str(row.object), 'object_sha256': hashlib.sha256(data).hexdigest(),
            })
        receipt['headers'] = _header_closure(build, owned_roots)
        if owned_roots and not receipt['headers']:
            raise ValueError('Actual owned compiler header closure is absent')
    build.output.mkdir(parents=True, exist_ok=True)
    (build.output / 'compile-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


def verify_receipt(build, receipt):
    if receipt['build_exit'] or receipt['project_sha256'] != _sha(build.project):
        raise ValueError('Successful current compile receipt required')
    if receipt['manifest_sha256'] != build.manifest_sha256:
        raise ValueError('Compile manifest changed')
    if receipt['layout_sha256'] != build.layout_sha256:
        raise ValueError('Compile shard layout changed')
    if len(receipt['builds']) != len(build.shards) or any(item['exit'] for item in receipt['builds']):
        raise ValueError('Incomplete shard compile receipt')
    for shard, item in zip(build.shards, receipt['builds']):
        if (item['project'] != str(shard.project)
                or item['intermediate'] != str(shard.intermediate)
                or item['project_sha256'] != hashlib.sha256(shard.contents).hexdigest()
                or _sha(shard.project) != item['project_sha256']):
            raise ValueError('Compiled shard project changed: ' + str(shard.project))
    if not receipt.get('headers'):
        raise ValueError('Compiled header closure is absent')
    expected = {(str(row.source), str(row.object)) for row in build.sources}
    actual = {(row['source'], row['object']) for row in receipt['sources']}
    if actual != expected or len(receipt['sources']) != len(build.sources):
        raise ValueError('Compile receipt source inventory differs')
    pinned = {(str(row.source), str(row.object)): row.sha256 for row in build.sources}
    for row in receipt['sources']:
        if row['source_sha256'] != pinned[(row['source'], row['object'])]:
            raise ValueError('Compile receipt source hash differs from manifest: ' + row['source'])
        if _sha(row['source']) != row['source_sha256'] or _sha(row['object']) != row['object_sha256']:
            raise ValueError('Compiled source or object changed: ' + row['source'])
    for path, digest in receipt['headers'].items():
        if _sha(path) != digest:
            raise ValueError('Compiled header changed: ' + path)


def plan_link_view(build, receipt):
    verify_receipt(build, receipt)
    root = ET.parse(build.project).getroot()
    namespace, items, _, _ = _xml_parts(root)
    if len(items) != len(build.sources):
        raise ValueError('Link project source inventory changed')
    for group in root.findall(namespace + 'ItemGroup'):
        for item in list(group.findall(namespace + 'ClCompile')):
            group.remove(item)
    objects = ET.SubElement(root, namespace + 'ItemGroup')
    for source in build.sources:
        ET.SubElement(objects, namespace + 'Object', Include=str(source.object))
    guid = root.find('.//' + namespace + 'ProjectGuid')
    if guid is not None:
        guid.text = '{' + str(uuid.uuid5(uuid.NAMESPACE_URL,
            str(build.project) + ':' + build.layout_sha256 + ':link')).upper() + '}'
    path = build.project.with_name(build.project.stem + '.shipyard-link-' +
                                   build.layout_sha256[:12] + '.vcxproj')
    ET.register_namespace('', namespace[1:-1])
    return LinkView(path, ET.tostring(root, encoding='utf-8', xml_declaration=True) + b'\n')


def write_link_view(view):
    if view.project.exists() and view.project.read_bytes() != view.contents:
        raise ValueError('Existing link view differs: ' + str(view.project))
    if not view.project.exists():
        view.project.write_bytes(view.contents)


def remove_link_view(view):
    if view.project.exists():
        if view.project.read_bytes() != view.contents:
            raise ValueError('Refusing to remove changed link view: ' + str(view.project))
        view.project.unlink()
