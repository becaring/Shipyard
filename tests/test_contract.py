import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.contract import verify


class ContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name) / 'checkout'
        self.base.mkdir()
        (self.base / 'sources').mkdir()
        self.source = self.base / 'sources' / 'probe.cpp'
        self.source.write_bytes(b'int probe = 1;\n')
        (self.base / 'README.md').write_bytes(b'Fixture checkout\n')
        self.manifest = self.base / 'baseline.json'
        self.manifest.write_text(json.dumps({
            'name': 'fixture',
            'api_version': 1,
            'files': {
                'sources/probe.cpp': hashlib.sha256(self.source.read_bytes()).hexdigest(),
            },
        }, sort_keys=True) + '\n', encoding='utf-8')
        self.git('init', '-q')
        self.git('add', '.')
        self.git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                 'commit', '-qm', 'fixture')
        self.head = self.git('rev-parse', 'HEAD')
        self.lock = Path(self.temp.name) / 'platform-base.lock.json'
        self.write_lock(self.head[:7])

    def git(self, *args):
        return subprocess.run(
            ['git', '-C', str(self.base), *args],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()

    def write_lock(self, revision):
        self.lock.write_text(json.dumps({
            'git_revision': revision,
            'api_version': 1,
            'baseline_sha256': hashlib.sha256(self.manifest.read_bytes()).hexdigest(),
        }) + '\n', encoding='utf-8')

    def test_checkout_verifies_with_dirty_nonproduction_file(self):
        self.assertEqual(verify(self.base, self.lock)['api_version'], 1)
        (self.base / 'README.md').write_bytes(b'Local documentation edit\n')
        self.assertEqual(verify(self.base, self.lock)['api_version'], 1)

    def test_changed_production_file_fails_even_when_revision_matches(self):
        self.source.write_bytes(b'int probe = 2;\n')
        with self.assertRaisesRegex(ValueError, 'file drift'):
            verify(self.base, self.lock)

    def test_wrong_revision_prefix_fails(self):
        wrong_first = '0' if self.head[0] != '0' else '1'
        self.write_lock(wrong_first + self.head[1:7])
        with self.assertRaisesRegex(ValueError, 'Git revision drift'):
            verify(self.base, self.lock)

    def test_git_parent_cannot_stand_in_for_checkout_root(self):
        nested = self.base / 'nested'
        nested.mkdir()
        (nested / 'baseline.json').write_bytes(self.manifest.read_bytes())
        with self.assertRaisesRegex(ValueError, 'not the Git checkout root'):
            verify(nested, self.lock)


    def test_named_selection_verifies_and_detects_drift(self):
        nested = self.base / 'selections' / '3Shipsty'
        (nested / 'sources').mkdir(parents=True)
        (nested / 'sources/probe.cpp').write_bytes(self.source.read_bytes())
        (nested / 'baseline.json').write_bytes(self.manifest.read_bytes())
        self.git('add', 'selections')
        self.git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                 'commit', '-qm', 'selection')
        self.write_lock(self.git('rev-parse', 'HEAD'))
        lock = json.loads(self.lock.read_text())
        lock['selection'] = '3Shipsty'
        self.lock.write_text(json.dumps(lock) + '\n', encoding='utf-8')
        self.assertEqual(verify(nested, self.lock)['api_version'], 1)
        (nested / 'sources/probe.cpp').write_bytes(b'int probe = 2;\n')
        with self.assertRaisesRegex(ValueError, 'file drift'):
            verify(nested, self.lock)
        with self.assertRaisesRegex(ValueError, 'not the Git checkout root'):
            verify(self.base, self.lock)



if __name__ == '__main__':
    unittest.main()
