"""Fail closed when a consumer's pinned shared base changes."""

import hashlib
import json
from pathlib import Path


def verify(base: Path, lock: Path) -> dict:
    manifest_path = base / 'baseline.json'
    expected = json.loads(lock.read_text(encoding='utf-8'))
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
