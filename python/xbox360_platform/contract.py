"""Verify a consumer's pinned platform checkout and production files."""

import hashlib
import json
import re
import subprocess
from pathlib import Path


def _git_output(base: Path, *args: str) -> str:
    try:
        result = subprocess.run(
            ['git', '-c', 'safe.directory=' + str(base), '-c',
             'safe.directory=' + str(base.parent.parent if base.parent.name == 'selections' else base),
             '-C', str(base), *args],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ValueError('Xbox platform base is not a Git checkout: ' + str(base)) from exc
    return result.stdout.strip()


def verify(base: Path, lock: Path) -> dict:
    base = Path(base).resolve()
    lock = Path(lock)
    manifest_path = base / 'baseline.json'
    expected = json.loads(lock.read_text(encoding='utf-8'))
    revision = expected['git_revision']
    if not isinstance(revision, str) or not re.fullmatch(r'[0-9a-fA-F]{7,64}', revision):
        raise ValueError('Xbox platform lock has an invalid Git revision')
    checkout = Path(_git_output(base, 'rev-parse', '--show-toplevel')).resolve()
    selection = expected.get('selection')
    if selection is None:
        selected_root = checkout
    elif selection in ('3Shipsty', '2Ship'):
        selected_root = checkout / 'selections' / selection
    else:
        raise ValueError('Xbox platform lock has an invalid selection')
    if base != selected_root.resolve():
        raise ValueError('Xbox platform base is not the Git checkout root or locked selection: ' + str(base))
    head = _git_output(base, 'rev-parse', '--verify', 'HEAD')
    if not head.lower().startswith(revision.lower()):
        raise ValueError('Xbox platform Git revision drift: ' + str(base))
    actual_digest = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
    if actual_digest != expected['baseline_sha256']:
        raise ValueError('Xbox platform baseline drift: ' + str(manifest_path))
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest['api_version'] != expected['api_version']:
        raise ValueError('Xbox platform API version drift')
    for relative, digest in manifest['files'].items():
        path = (base / relative).resolve()
        if not path.is_relative_to(base.resolve()):
            raise ValueError('Xbox platform baseline path escaped: ' + relative)
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Xbox platform file drift: ' + relative)
    return manifest
