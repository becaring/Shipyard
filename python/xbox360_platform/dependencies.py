"""Pinned Git dependencies: clean checkouts, git-archive snapshots and receipts.

checkout() clones or moves a checkout to an exact commit and refuses modified
or re-pointed ones. snapshot() copies the committed tree (never the working
tree) into an owned folder and writes a receipt of every file's SHA-256;
verified() checks that receipt against the hash committed in a lock, then every
file against the receipt, before a generator reads the folder.
"""

import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import zipfile


def git(repository, *args, binary=False):
    command = ['git', '-c', 'safe.directory=' + Path(repository).resolve().as_posix(), '-C', str(repository), *args]
    output = subprocess.check_output(command, env=dict(os.environ, GIT_OPTIONAL_LOCKS='0'))
    return output if binary else output.decode().strip()


def checkout(path, url, revision, patched=False, clone_options=()):
    """path at revision from url; patched allows local edits that keep the revision."""
    path = Path(path)
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', '--no-checkout', *clone_options, url, str(path)], check=True)
        git(path, 'checkout', '--detach', revision)
    if git(path, 'remote', 'get-url', 'origin') != url:
        raise ValueError('Dependency URL differs: ' + str(path))
    changed = git(path, 'status', '--porcelain')
    if changed and not patched:
        raise ValueError('Dependency checkout is modified: ' + str(path))
    if git(path, 'rev-parse', 'HEAD') != revision:
        if changed:
            raise ValueError('Modified dependency cannot change revision: ' + str(path))
        git(path, 'fetch', 'origin', revision)
        git(path, 'checkout', '--detach', revision)
        if git(path, 'rev-parse', 'HEAD') != revision:
            raise ValueError('Dependency revision drift: ' + str(path))
    return path


def verify_files(folder, files):
    for name, expected in files.items():
        if hashlib.sha256((Path(folder) / name).read_bytes()).hexdigest() != expected:
            raise ValueError('Locked dependency byte differs: ' + str(Path(folder) / name))


def snapshot(repository, revision, target, receipt_path, acquisition='git archive of a clean checkout'):
    """Committed tree of revision into target; returns the receipt's SHA-256."""
    if git(repository, 'rev-parse', revision + '^{commit}') != revision:
        raise ValueError('Dependency revision is not an exact commit: ' + revision)
    if git(repository, 'status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Dependency source is modified: ' + str(repository))
    target = Path(target).resolve()
    files, ready = {}, []
    with zipfile.ZipFile(io.BytesIO(git(repository, 'archive', '--format=zip', revision, binary=True))) as archive:
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            path = (target / entry.filename).resolve()
            if not path.is_relative_to(target):
                raise ValueError('Archive escaped owned dependency: ' + entry.filename)
            data = archive.read(entry)
            ready.append((path, data))
            files[entry.filename] = hashlib.sha256(data).hexdigest()
    for path, data in ready:
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
    receipt = dict(commit=revision, url=git(repository, 'remote', 'get-url', 'origin'), acquisition=acquisition,
                   files=files)
    Path(receipt_path).write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return hashlib.sha256(Path(receipt_path).read_bytes()).hexdigest()


def verified(target, receipt_path, receipt_sha256):
    """target, after its receipt and every recorded file match."""
    if hashlib.sha256(Path(receipt_path).read_bytes()).hexdigest() != receipt_sha256:
        raise ValueError('Dependency receipt drift: ' + str(receipt_path))
    verify_files(target, json.loads(Path(receipt_path).read_text(encoding='utf-8'))['files'])
    return Path(target)
