"""Deterministic, per-source incremental ClCompile shards for generated Xbox 360 VC projects."""

from concurrent.futures import ThreadPoolExecutor, as_completed
from copy import deepcopy
from dataclasses import dataclass, replace
from functools import lru_cache
import hashlib
import json
import os
import re
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


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def _canon(element):
    return [element.tag, sorted(element.attrib.items()), (element.text or '').strip(),
            [_canon(child) for child in element]]


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
    manifest_digest = _digest({
        'project_sha256': project_digest,
        'sources': [(row.include, str(row.source), str(row.object), row.sha256) for row in records],
    })
    layout_digest, shards = _shards(project, records, min(jobs, len(records)), output)
    return Plan(project, project_digest, manifest_digest, layout_digest, output, tuple(records), shards)


def _shards(project, records, workers, output):
    """Stripe records over at most workers compile projects; plan() passes every source."""
    root = ET.parse(project).getroot()
    namespace = _xml_parts(root)[0]
    ET.register_namespace('', namespace[1:-1])
    layout = _digest({
        'project_sha256': _sha(project), 'workers': workers,
        'sources': [(row.include, str(row.source), str(row.object)) for row in records],
    })
    count = min(workers, len(records))
    selected = {_key(row.include): row for row in records}
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
        # Worker slots outlive layouts so compile PDBs referenced by reused objects remain.
        folder = output / ('workers-%02d' % workers) / ('shard-%02d' % index)
        intermediate.text = str(folder) + '\\'
        pdb = definitions.find(ns + 'ProgramDataBaseFileName')
        if pdb is None:
            pdb = ET.SubElement(definitions, ns + 'ProgramDataBaseFileName')
        pdb.text = str(folder / 'vc100.pdb')
        guid = clone.find('.//' + ns + 'ProjectGuid')
        if guid is not None:
            guid.text = '{' + str(uuid.uuid5(uuid.NAMESPACE_URL, str(project) + ':' + layout + ':' + str(index))).upper() + '}'
        shard_path = project.with_name(project.stem + '.shipyard-' + layout[:12] + '-%02d.vcxproj' % index)
        shards.append(Shard(shard_path, folder, folder / 'compile.log', chosen,
                            ET.tostring(clone, encoding='utf-8', xml_declaration=True)))
    return layout, tuple(shards)


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
        'sources': [row.include for row in shard.sources],
        'command': command, 'pid': process.pid,
        'exit': exit_code, 'timed_out': timed_out,
        'started_monotonic': started, 'ended_monotonic': ended,
        'seconds': round(ended - started, 3),
    }


def _header_closure(shards, roots):
    """Owned headers each compiled source read, keyed by casefolded source path."""
    allowed = tuple(Path(root).resolve() for root in roots)
    closures = {}
    shas = {}
    for shard in shards:
        expected = {str(row.source).casefold() for row in shard.sources}
        active = set()
        tlog = shard.intermediate / 'CL.read.1.tlog'
        for line in tlog.read_text(encoding='utf-16').splitlines():
            if line.startswith('^'):
                active = {str(Path(name).resolve()).casefold() for name in line[1:].split('|')}
                if active - expected:
                    raise ValueError('Compiler tlog contains unselected sources: ' + str(tlog))
                for key in active:
                    closures.setdefault(key, {})
            elif active:
                path = Path(line)
                if path.suffix.lower() in ('.h', '.hpp', '.inl', '.inc'):
                    resolved = path.resolve()
                    if any(resolved.is_relative_to(root) for root in allowed):
                        if resolved not in shas:
                            shas[resolved] = _sha(resolved)
                        for key in active:
                            closures[key][str(resolved)] = shas[resolved]
        if not expected <= closures.keys():
            raise ValueError('Compiler tlog omits selected sources: ' + str(tlog))
    return closures


def _expander(project, root, ns):
    properties = {key.casefold(): value for key, value in os.environ.items()}
    properties.update(projectdir=str(project.parent) + '/',
                      msbuildthisfiledirectory=str(project.parent) + '/')
    for group in root.findall(ns + 'PropertyGroup'):
        for item in group:
            if item.text:
                properties[item.tag.removeprefix(ns).casefold()] = item.text

    def expand(value):
        for _ in range(32):
            result = re.sub(r'\$\(([^)]+)\)',
                            lambda match: properties.get(match[1].casefold(), match[0]), value)
            if result == value:
                return result
            value = result
        raise ValueError('Cyclic project property in include path: ' + value)
    return expand


def _compile_identity(build, command):
    """Digest of every project input shared by all compiles, plus one digest per ClCompile item."""
    root = ET.parse(build.project).getroot()
    ns, items, _, _ = _xml_parts(root)
    expand = _expander(build.project, root, ns)
    item_digests = {_key(item.attrib['Include']): _digest(_canon(item)) for item in items}
    for group in root.findall(ns + 'ItemGroup'):
        for item in group.findall(ns + 'ClCompile'):
            group.remove(item)
    imports = {}
    for node in root.iter(ns + 'Import'):
        path = Path(expand(node.attrib.get('Project', '')).replace('\\', '/'))
        path = path if path.is_absolute() else build.project.parent / path
        if '$(' not in str(path) and path.is_file():
            imports[str(path)] = _sha(path)
    environment = {key.casefold(): value for key, value in os.environ.items()}
    referenced = {name.casefold() for name in re.findall(r'\$\(([^)]+)\)', build.project.read_text(
        encoding='utf-8', errors='replace'))}
    return _digest({
        'project': _canon(root), 'imports': imports, 'command': list(command),
        'environment': {name: environment[name] for name in sorted(referenced & environment.keys())},
    }), item_digests


def _selected_headers(build, roots):
    """Hash selected include trees even when MSBuild omits generated dependencies.

    Returns the whole inventory and each source's static include closure.
    """
    allowed = tuple(Path(root).resolve() for root in roots)
    root = ET.parse(build.project).getroot()
    ns, items, _, definitions = _xml_parts(root)
    expand = _expander(build.project, root, ns)

    @lru_cache(maxsize=None)
    def owned_path(value):
        value = expand(value.strip().strip('"'))
        if not value or '$(' in value or '%(' in value:
            return None
        path = Path(value.replace('\\', '/'))
        if not path.is_absolute():
            path = build.project.parent / path
        path = path.resolve()
        return path if any(path.is_relative_to(base) for base in allowed) else None

    def options(element):
        include, forced = [], []
        for name, target in [('AdditionalIncludeDirectories', include), ('ForcedIncludeFiles', forced)]:
            for node in element.findall(ns + name):
                target.extend((node.text or '').split(';'))
        for node in element.findall(ns + 'AdditionalOptions'):
            tokens = iter(re.findall(r'(?:[^\s"]+|"[^"]*")+', node.text or ''))
            for token in tokens:
                token = token.replace('"', '')
                match = re.match(r'^[-/](FI|I)(.*)$', token)
                if match:
                    value = match[2] or next(tokens, '')
                    (forced if match[1] == 'FI' else include).append(value)
        return ([path for value in include if (path := owned_path(value))],
                [path for value in forced if (path := owned_path(value))])

    base_dirs, base_forced = options(definitions)
    sources = {_key(row.include): row.source for row in build.sources}
    selected = []
    directories = set()
    headers = set()
    for item in items:
        local_dirs, local_forced = options(item)
        dirs = tuple(dict.fromkeys(local_dirs + base_dirs))
        directories.update(dirs)
        forced = [path for path in local_forced + base_forced if path.is_file()]
        headers.update(forced)
        selected.append((sources[_key(item.attrib['Include'])], dirs, forced))

    # Broad repository roots allow qualified includes; follow those from sources
    # instead of pulling unrelated scratch builds into this compile's inventory.
    trees = sorted(directories - set(allowed), key=lambda path: len(path.parts))
    scanned = []
    for directory in trees:
        if not directory.is_dir() or any(directory.is_relative_to(parent) for parent in scanned):
            continue
        scanned.append(directory)
        for folder, children, names in os.walk(directory):
            children[:] = [name for name in children if name != '.git']
            for name in names:
                if Path(name).suffix.lower() in ('.h', '.hpp', '.hxx', '.inl', '.inc'):
                    headers.add((Path(folder) / name).resolve())

    # [ \t] rather than \s: whitespace spanning lines backtracks badly on large generated headers.
    includes = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^">]+)[">]', re.MULTILINE)
    @lru_cache(maxsize=None)
    def include_target(directory, name):
        target = directory / name.replace('\\', '/')
        if target in headers:
            return target
        if not target.is_file():
            return None
        target = target.resolve()
        return target if any(target.is_relative_to(base) for base in allowed) else None

    @lru_cache(maxsize=None)
    def parse(path):
        return includes.findall(path.read_text(encoding='utf-8', errors='replace')) if path.is_file() else ()

    @lru_cache(maxsize=None)
    def edges(path, dirs):
        found = []
        for name in parse(path):
            for directory in (path.parent, *dirs):
                target = include_target(directory, name)
                if target is not None:
                    found.append(target)
                    break
        return tuple(found)

    # ponytail: one walk per source over cached edges; memoize closures per (header, dirs) if this shows up.
    closures = {}
    for source, dirs, forced in selected:
        seen = set(forced)
        pending = [source, *forced]
        while pending:
            for target in edges(pending.pop(), dirs):
                if target not in seen:
                    seen.add(target)
                    pending.append(target)
        headers.update(seen)
        closures[str(source).casefold()] = {str(path) for path in seen}
    return {str(path): _sha(path) for path in sorted(headers)}, closures


def _reusable(build, identity, items, inventory, closures):
    """Previous objects whose source, item options, header closure and bytes are unchanged."""
    try:
        previous = json.loads((build.output / 'compile-receipt.json').read_text(encoding='utf-8'))
        if previous.get('header_inventory_version') != 2 or previous.get('compile_sha256') != identity:
            return {}
        recorded = previous['headers']
        names = list(recorded)
        entries = {(row['source'], row['object']): row for row in previous['sources']}
        current = dict(inventory)
        reused = {}
        for row in build.sources:
            entry = entries.get((str(row.source), str(row.object)))
            if (entry is None or entry['source_sha256'] != row.sha256
                    or entry['item_sha256'] != items[_key(row.include)]):
                continue
            closure = {names[index]: recorded[names[index]] for index in entry['headers']}
            # A header the source newly reaches, such as one shadowing an older match, is a change.
            if not ({path.casefold() for path in closures[str(row.source).casefold()]}
                    <= {path.casefold() for path in closure}):
                continue
            for path in closure:
                if path not in current:
                    current[path] = _sha(path) if Path(path).is_file() else None
            if (any(current[path] != digest for path, digest in closure.items())
                    or not row.object.is_file() or _sha(row.object) != entry['object_sha256']):
                continue
            reused[str(row.object)] = (entry, closure)
        return reused
    except (OSError, ValueError, KeyError, IndexError, TypeError):
        return {}


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
    identity, items = _compile_identity(build, (*command_prefix, *arguments))
    headers_before, closures = _selected_headers(build, owned_roots)
    reused = _reusable(build, identity, items, headers_before, closures)
    stale = [row for row in build.sources if str(row.object) not in reused]
    invalidated = 0
    for row in stale:
        if row.object.exists():
            row.object.unlink()
            invalidated += 1
    (build.output / 'compile-receipt.json').unlink(missing_ok=True)
    # Only stale sources reach MSBuild, so its tracking logs never decide what is current.
    compiling = replace(build, shards=_shards(build.project, stale, len(build.shards), build.output)[1])
    write_shards(compiling)
    results = [None] * len(compiling.shards)
    try:
        for shard in compiling.shards:
            for tlog in shard.intermediate.glob('*.tlog'):
                tlog.unlink()
        with ThreadPoolExecutor(max_workers=len(compiling.shards) or 1) as pool:
            futures = {pool.submit(_invoke, shard, tuple(command_prefix), tuple(arguments), timeout): index
                       for index, shard in enumerate(compiling.shards)}
            for future in as_completed(futures):
                results[futures[future]] = future.result()
    finally:
        remove_shards(compiling)
    receipt = {
        'project': str(build.project), 'project_sha256': build.project_sha256,
        'manifest_sha256': build.manifest_sha256, 'layout_sha256': build.layout_sha256,
        'compile_sha256': identity, 'builds': results,
        'build_exit': next((row['exit'] for row in results if row['exit']), 0),
        'sources': [], 'headers': {},
        'header_inventory_version': 2, 'selected_headers': headers_before,
        'invalidated_objects': invalidated, 'reused_objects': len(reused),
    }
    # A failed compile still records reused objects and those of shards that succeeded,
    # so the next run resumes; verify_receipt rejects the receipt itself.
    strict = receipt['build_exit'] == 0
    ok = [shard for shard, row in zip(compiling.shards, results) if row['exit'] == 0]
    if strict and _sha(build.project) != build.project_sha256:
        raise ValueError('Project changed during compilation')
    if _selected_headers(build, owned_roots)[0] != headers_before:
        if strict:
            raise ValueError('Selected headers changed during compilation')
        ok, reused = [], {}
    observed = _header_closure(ok, owned_roots)
    compiled = {str(row.object) for shard in ok for row in shard.sources}
    headers = dict(headers_before)
    for row in build.sources:
        key = str(row.source).casefold()
        if str(row.object) in reused:
            entry, closure = reused[str(row.object)]
        elif str(row.object) in compiled:
            entry, closure = None, dict({path: headers_before[path] for path in closures[key]}, **observed[key])
        else:
            continue
        if _sha(row.source) != row.sha256:
            if strict:
                raise ValueError('Source changed during compilation: ' + str(row.source))
            continue
        data = row.object.read_bytes() if row.object.is_file() else b''
        digest = hashlib.sha256(data).hexdigest()
        offset = 6 if data[:4] == b'\0\0\xff\xff' else 0
        if (len(data) < offset + 2 or struct.unpack_from('<H', data, offset)[0] != 0x1f2
                or (entry and entry['object_sha256'] != digest)):
            if strict:
                raise ValueError('Not a current PPC COFF object: ' + str(row.object))
            continue
        headers.update(closure)
        receipt['sources'].append({
            'source': str(row.source), 'source_sha256': row.sha256,
            'object': str(row.object), 'object_sha256': digest,
            'item_sha256': items[_key(row.include)], 'reused': entry is not None,
            'headers': sorted(closure),
        })
    receipt['headers'] = dict(sorted(headers.items()))
    # Per-source closures index into the sorted header table to keep the receipt small.
    index = {path: number for number, path in enumerate(receipt['headers'])}
    for row in receipt['sources']:
        row['headers'] = [index[path] for path in row['headers']]
    if strict and not receipt['headers']:
        raise ValueError('Actual owned compiler header closure is absent')
    build.output.mkdir(parents=True, exist_ok=True)
    (build.output / 'compile-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


def verify_receipt(build, receipt):
    if receipt.get('header_inventory_version') != 2:
        raise ValueError('Selected header inventory is absent; rebuild required')
    if receipt['build_exit'] or receipt['project_sha256'] != _sha(build.project):
        raise ValueError('Successful current compile receipt required')
    if receipt['manifest_sha256'] != build.manifest_sha256:
        raise ValueError('Compile manifest changed')
    if receipt['layout_sha256'] != build.layout_sha256:
        raise ValueError('Compile shard layout changed')
    if any(item['exit'] for item in receipt['builds']):
        raise ValueError('Incomplete shard compile receipt')
    expected = {(str(row.source), str(row.object)) for row in build.sources}
    actual = {(row['source'], row['object']) for row in receipt['sources']}
    if actual != expected or len(receipt['sources']) != len(build.sources):
        raise ValueError('Compile receipt source inventory differs')
    # The compile projects were removed after compiling; regenerate them from this plan instead.
    reused = {row['object'] for row in receipt['sources'] if row['reused']}
    _, shards = _shards(build.project, [row for row in build.sources if str(row.object) not in reused],
                        len(build.shards), build.output)
    if len(shards) != len(receipt['builds']):
        raise ValueError('Incomplete shard compile receipt')
    for shard, item in zip(shards, receipt['builds']):
        if (item['project'] != str(shard.project)
                or item['intermediate'] != str(shard.intermediate)
                or item['project_sha256'] != hashlib.sha256(shard.contents).hexdigest()):
            raise ValueError('Compiled shard project changed: ' + str(shard.project))
    if not receipt.get('headers'):
        raise ValueError('Compiled header closure is absent')
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
