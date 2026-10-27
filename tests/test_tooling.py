"""Consumer tooling: staged regeneration, dependency snapshots, projector style, decoder differential."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform import decoder_differential, dependencies, projector_style, stages


def python_stage(root):
    return lambda script, options: [sys.executable, str(Path(root) / script), *options]


class StageTests(unittest.TestCase):
    def test_resume_reruns_failed_stage_only(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'ok.py').write_text('print("ok")\n')
            (root / 'gate.py').write_text('import pathlib, sys\nsys.exit(0 if pathlib.Path("open").exists() else 3)\n')
            plan = [('ok.py', []), ('gate.py', ['x']), ('ok.py', ['y'])]
            logs = root / 'logs'
            with self.assertRaises(SystemExit) as failure:
                stages.run_stages(plan, root, logs, python_stage(root), revision='r1')
            self.assertEqual(failure.exception.code, 3)
            self.assertEqual([row['exit'] for row in json.loads((logs / 'receipt.json').read_text())['stages']], [0, 3])
            (root / 'open').touch()
            with self.assertRaises(ValueError):
                stages.run_stages(plan, root, logs, python_stage(root), resume=True, revision='r2')
            receipt = stages.run_stages(plan, root, logs, python_stage(root), resume=True, revision='r1')
            self.assertEqual([row['exit'] for row in receipt['stages']], [0, 0, 0])
            (logs / '01-ok.log').write_text('edited')
            with self.assertRaises(ValueError):
                stages.run_stages(plan, root, logs, python_stage(root), resume=True, revision='r1')
            with self.assertRaises(ValueError):
                stages.run_stages(plan[1:], root, logs, python_stage(root), resume=True, revision='r1')

    def test_preflight_locks(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'a.txt').write_bytes(b'one\r\ntwo\r\n')
            lock = root / 'sources.lock.json'
            lf = hashlib.sha256(b'one\ntwo\n').hexdigest()
            lock.write_text(json.dumps({'sources': {'a.txt': lf, 'missing.txt': '0' * 64}, 'files': ['ignored']}))
            self.assertEqual(stages.preflight_locks(root, [lock]), 1)
            (root / 'a.txt').write_bytes(b'one\n')
            with self.assertRaises(ValueError):
                stages.preflight_locks(root, [lock])

    def test_scoped_git_environment(self):
        with tempfile.TemporaryDirectory() as folder:
            environment = stages.scoped_git_environment([folder, Path(folder) / 'absent'], {'GIT_CONFIG_COUNT': '1'})
            self.assertEqual(environment['GIT_CONFIG_COUNT'], '2')
            self.assertEqual(environment['GIT_CONFIG_VALUE_1'], Path(folder).resolve().as_posix())


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), '-c', 'user.name=t', '-c', 'user.email=t@t', *args],
                                   text=True).strip()


class DependencyTests(unittest.TestCase):
    def test_snapshot_checkout_and_verify(self):
        with tempfile.TemporaryDirectory() as folder:
            origin = Path(folder) / 'origin'
            origin.mkdir()
            git(origin, 'init', '-q')
            (origin / 'src').mkdir()
            (origin / 'src/a.h').write_text('int a;\n')
            git(origin, 'add', '.')
            git(origin, 'commit', '-q', '-m', 'one')
            first = git(origin, 'rev-parse', 'HEAD')
            (origin / 'src/a.h').write_text('int b;\n')
            git(origin, 'commit', '-q', '-am', 'two')
            url = origin.as_posix()
            clone = dependencies.checkout(Path(folder) / 'clone', url, first)
            self.assertEqual(dependencies.git(clone, 'rev-parse', 'HEAD'), first)
            target, receipt = Path(folder) / 'deps/a', Path(folder) / 'deps/a-receipt.json'
            digest = dependencies.snapshot(clone, first, target, receipt)
            self.assertEqual((target / 'src/a.h').read_text(), 'int a;\n')
            self.assertEqual(dependencies.verified(target, receipt, digest), target)
            (target / 'src/a.h').write_text('int c;\n')
            with self.assertRaises(ValueError):
                dependencies.verified(target, receipt, digest)
            with self.assertRaises(ValueError):
                dependencies.verified(target, receipt, '0' * 64)
            with self.assertRaises(ValueError):
                dependencies.snapshot(clone, 'HEAD', target, receipt)
            (clone / 'src/a.h').write_text('edited\n')
            with self.assertRaises(ValueError):
                dependencies.checkout(clone, url, first)
            self.assertEqual(dependencies.checkout(clone, url, first, patched=True), clone)
            with self.assertRaises(ValueError):
                dependencies.checkout(clone, url + '-other', first, patched=True)


class StyleTests(unittest.TestCase):
    def test_pattern_and_limit(self):
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / 'project_a.py').write_text('x = 1\n' + 'y' * 141 + '\n')
            (Path(folder) / 'prepare_b.py').write_text('x = 2\n')
            self.assertEqual(projector_style.main(folder), 0)
            self.assertEqual(projector_style.main(folder, 'project_*.py'), 1)
            self.assertEqual(projector_style.main(folder, 'project_*.py', 160), 0)
            self.assertEqual(projector_style.check([Path(folder) / 'project_a.py']),
                             ['project_a.py:2: 141 characters (limit 140)'])
            with self.assertRaises(SystemExit):
                projector_style.main(folder, 'none_*.py')


DECODER = r'''
enum { SHADER_0, SHADER_INPUT_1 };
#define SHADER_OPT(opt) ((uint64_t)(1ULL << (opt)))
struct CCFeatures {
    uint8_t c[2][2][4];
    bool opt_alpha, opt_fog; // trailing comment
    int numInputs;
    int16_t shader_id;
};
void gfx_cc_get_features(uint64_t id0, uint64_t id1, CCFeatures* f) {
    for (int i = 0; i < 16; ++i) f->c[i / 8][i / 4 % 2][i % 4] = id0 >> (4 * i) & 0xf;
    f->opt_alpha = (id1 & SHADER_OPT(0)) != 0;
    f->opt_fog = (id1 & SHADER_OPT(1)) != 0;
    f->numInputs = f->c[0][0][0] == SHADER_INPUT_1;
    f->shader_id = (int16_t)(id1 >> 48);
}
'''


class DecoderDifferentialTests(unittest.TestCase):
    def test_members(self):
        self.assertEqual(decoder_differential.members(DECODER), ['c', 'opt_alpha', 'opt_fog', 'numInputs', 'shader_id'])

    def test_identical_and_drifted_decoders(self):
        with tempfile.TemporaryDirectory() as folder:
            output = decoder_differential.run(DECODER, DECODER, Path(folder) / 'same', 4)
            self.assertIn('PASS 256 decoder differential cases', output)
            # Upper id bits are 64-bit: a projection truncating them must fail.
            truncated = DECODER.replace('(int16_t)(id1 >> 48)', '(int16_t)((uint32_t)id1 >> 16)')
            with self.assertRaises(RuntimeError):
                decoder_differential.run(DECODER, truncated, Path(folder) / 'truncated', 4)
            layout = DECODER.replace('    int numInputs;\n', '    int16_t numInputs;\n')
            with self.assertRaises(RuntimeError):
                decoder_differential.run(DECODER, layout, Path(folder) / 'layout', 4)


if __name__ == '__main__':
    unittest.main()
