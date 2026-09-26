"""The shared clamp lowering preserves guards and rejects expression drift."""
import pathlib
import runpy
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
hoist = runpy.run_path(str(ROOT / 'python/xbox360_platform/vertex_stream.py'))['hoist_clamp_bounds']
START = '    for (int i = 0; i < 3; ++i) {'
END = '    Finish();'
SOURCE = '''void Pack() {
    float* output = buffer;
    for (int i = 0; i < 3; ++i) {
        for (int t = 0; t < 2; ++t) {
            if (!used[t]) continue;
            if (mask & (1 << (2*t))) *output++ = (width2[t] - 0.5f) / width[t];
            if (mask & (1 << (2*t+1))) *output++ = (height2[t] - 0.5f) / height[t];
        }
    }
    Finish();
}
'''
AXES = (('boundS', 'mask & (1 << (2*t))', '(width2[t] - 0.5f) / width[t]'),
        ('boundT', 'mask & (1 << (2*t+1))', '(height2[t] - 0.5f) / height[t]'))


def transform(source):
    return hoist(source, start=START, end=END, cursor='output', used_textures='used', axes=AXES)


class ClampProjection(unittest.TestCase):
    def test_exact_expressions_only_before_vertex_loop(self):
        result = transform(SOURCE)
        prelude, body = result.split(START)
        self.assertIn('if (!used[t]) continue;', prelude)
        for name, guard, expression in AXES:
            self.assertEqual(result.count(expression), 1)
            self.assertIn('if (%s) %s[t] = %s;' % (guard, name, expression), prelude)
            self.assertIn('if (%s) *output++ = %s[t];' % (guard, name), body)

    def test_changed_or_duplicated_expression_rejected(self):
        for changed in (SOURCE.replace('width2[t] - 0.5f', 'width2[t] - 1.0f'),
                        SOURCE.replace('*output++ = (width2[t] - 0.5f) / width[t];',
                                       '*output++ = (width2[t] - 0.5f) / width[t];' * 2),
                        SOURCE.replace(START, START + START)):
            with self.assertRaises(ValueError):
                transform(changed)


if __name__ == '__main__':
    unittest.main()
