"""Resumable staged regeneration with receipts.

run_stages() runs (script, options) stages in order and rewrites the receipt
after each: exit code, log path and log SHA-256. A resumed run continues after
the last successful stage only while the stage list, every recorded log and the
source revision still match. preflight_locks() checks sources.lock.json-style
hash maps before anything is cleaned or rebuilt.
"""

import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def scoped_git_environment(checkouts, environment=None):
    """Trust only the given source checkouts for child Git calls."""
    environment = dict(os.environ if environment is None else environment)
    count = int(environment.get('GIT_CONFIG_COUNT', '0'))
    for checkout in checkouts:
        if Path(checkout).is_dir():
            environment['GIT_CONFIG_KEY_%d' % count] = 'safe.directory'
            environment['GIT_CONFIG_VALUE_%d' % count] = Path(checkout).resolve().as_posix()
            count += 1
    environment['GIT_CONFIG_COUNT'] = str(count)
    return environment


def source_revision(root, environment=None):
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, env=environment, text=True).strip()


def preflight_locks(root, locks, sections=('sources', 'files')):
    """Count verified {relative path: sha256} entries; raises on the first drift.

    A text file also matches its UTF-8 (BOM-less) and LF-normalized hashes."""
    root = Path(root)
    checked = 0
    for lock in locks:
        document = json.loads(Path(lock).read_text(encoding='utf-8'))
        for section in sections:
            entries = document.get(section, {})
            if not isinstance(entries, dict):
                continue
            for name, expected in entries.items():
                source = root / name
                if not isinstance(expected, str) or len(expected) != 64 or not source.is_file():
                    continue
                raw = source.read_bytes()
                allowed = {hashlib.sha256(raw).hexdigest()}
                try:
                    text = raw.decode('utf-8-sig')
                    allowed |= {hashlib.sha256(text.encode()).hexdigest(),
                                hashlib.sha256(text.replace('\r\n', '\n').encode()).hexdigest()}
                except UnicodeError:
                    pass
                if expected.lower() not in allowed:
                    raise ValueError('Source lock drift before regeneration: ' + str(lock) + ' -> ' + name)
                checked += 1
    return checked


def _log(log_dir, index, script):
    return Path(log_dir) / ('%02d-%s.log' % (index + 1, Path(script).stem))


def completed_prefix(receipt, stages, log_dir, root):
    """Stages already done; drops a trailing failed row so it reruns."""
    rows = receipt['stages']
    if len(rows) > len(stages):
        raise ValueError('Regeneration receipt has too many stages')
    for index, row in enumerate(rows):
        script, options = stages[index]
        log = _log(log_dir, index, script)
        if row['script'] != script or row['options'] != list(options):
            raise ValueError('Regeneration stage order changed at %d' % (index + 1))
        if row['log'] != log.relative_to(root).as_posix():
            raise ValueError('Regeneration log path changed at %d' % (index + 1))
        if not log.is_file() or sha(log) != row['log_sha256']:
            raise ValueError('Regeneration log drift at %d' % (index + 1))
        if row['exit']:
            if index != len(rows) - 1:
                raise ValueError('Failed regeneration stage is not last')
            rows.pop()
            return index
    return len(rows)


def run_stages(stages, root, log_dir, command, resume=False, environment=None, revision=None, extra=None):
    """Run stages; command(script, options) gives each argv. Returns the receipt.

    revision, when given, is recorded and must be unchanged on resume; extra adds
    fields to a new receipt. Raises SystemExit with a failing stage's exit code."""
    root, log_dir = Path(root), Path(log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    receipt_path = log_dir / 'receipt.json'
    if resume:
        receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
        start = completed_prefix(receipt, stages, log_dir, root)
        if receipt.get('source_revision') != revision:
            raise ValueError('Source revision changed since regeneration began')
    else:
        receipt = dict(extra or {}, source_revision=revision, stages=[])
        start = 0
    for index in range(start, len(stages)):
        script, options = stages[index]
        print('Regenerating %02d/%02d: %s %s' % (index + 1, len(stages), script, ' '.join(options)), flush=True)
        result = subprocess.run(command(script, options), cwd=root, env=environment, capture_output=True,
                                text=True, errors='replace')
        log = _log(log_dir, index, script)
        log.write_text(result.stdout + result.stderr, encoding='utf-8')
        receipt['stages'].append({'script': script, 'options': list(options), 'exit': result.returncode,
                                  'log': log.relative_to(root).as_posix(), 'log_sha256': sha(log)})
        receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        if result.returncode:
            print((result.stdout + result.stderr)[-6000:], flush=True)
            raise SystemExit(result.returncode)
    print('All %d generation stages passed' % len(stages), flush=True)
    return receipt
