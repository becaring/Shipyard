import pathlib
import runpy
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
aspect = runpy.run_path(str(ROOT / 'python/xbox360_platform/vertex_invariants.py'))['aspect_denominator']
normals = runpy.run_path(str(ROOT / 'python/xbox360_platform/vertex_invariants.py'))['normal_components']
colors = runpy.run_path(str(ROOT / 'python/xbox360_platform/vertex_invariants.py'))['light_colors']
SOURCE = '''float Aspect(float x) {
    return x * (4.0f / 3.0f) / ((float)width / (float)height);
}
void Vertex() {
    for (unsigned i=0; i<count; ++i) {
        x = Aspect(x);
    }
}
void Next() {}
'''


def transform(source):
    return aspect(source, start='void Vertex()', end='void Next()',
        loop='    for (unsigned i=0; i<count; ++i) {', eligible='validPacket',
        framebuffer='framebuffer', denominator='(float)width / (float)height',
        original_statement='        x = Aspect(x);', numerator='x * (4.0f / 3.0f)')


class VertexInvariantTests(unittest.TestCase):
    def test_color_conversion_is_lazy_with_original_accumulator(self):
        body = '''void Vertex() {
    for (unsigned vertex=0; vertex<count; ++vertex) {
                if (intensity > 0.0f) {
                    r += intensity * light[i].r;
                    g += intensity * light[i].g;
                    b += intensity * light[i].b;
                }
    }
}
void Next() {}
'''
        def lower(text):
            return colors(text, start='void Vertex()', end='void Next()',
                loop='    for (unsigned vertex=0; vertex<count; ++vertex) {',
                positive='                if (intensity > 0.0f) {', eligible='disjoint',
                capacity='32', components=('light[i].r', 'light[i].g', 'light[i].b'))
        result = lower(body)
        self.assertLess(result.index('if (intensity > 0.0f)'), result.index('shipyardLightColors[i][0] ='))
        self.assertIn('r += intensity * (shipyardLightColorBatch ? shipyardLightColors[i][0] : light[i].r);', result)
        self.assertIn('if (shipyardLightColorBatch && !shipyardLightColorReady[i])', result)
        with self.assertRaises(ValueError):
            lower(body.replace('r += intensity * light[i].r;', 'r += intensity;'))

    def test_normal_reads_keep_alias_fallback_and_expression_order(self):
        body = 'void Vertex() {\n    if (lighting) {\n' + ('        sum += n[0] * a + n[1] * b + n[2] * c;\n' * 4) + '    }\n}\nvoid Next() {}\n'
        def lower(text):
            return normals(text, start='void Vertex()', end='void Next()', lighting='    if (lighting) {',
                           eligible='disjoint', needed='used', components=('n[0]', 'n[1]', 'n[2]'))
        result = lower(body)
        for axis in range(3):
            self.assertEqual(result.count('(shipyardNormalBatch ? shipyardNormal[%d] : n[%d])' % (axis, axis)), 4)
            self.assertEqual(result.count('shipyardNormal[%d] = (float)n[%d];' % (axis, axis)), 1)
        self.assertIn('const bool shipyardNormalBatch = disjoint && (used);', result)
        with self.assertRaises(ValueError):
            lower(body.replace('n[0]', 'n[3]', 1))

    def test_lazy_guarded_evaluation_and_original_fallback(self):
        projected = transform(SOURCE)
        self.assertIn('if (validPacket && !framebuffer) {\n            if (!shipyardAspectReady)', projected)
        self.assertIn('x = x * (4.0f / 3.0f) / shipyardAspectDenominator;', projected)
        self.assertIn('} else {\n            x = Aspect(x);', projected)
        self.assertLess(projected.index('for (unsigned'), projected.index('shipyardAspectDenominator ='))

    def test_arithmetic_and_inventory_drift_rejected(self):
        for changed in (SOURCE.replace('4.0f / 3.0f', '1.333f'),
                        SOURCE.replace('        x = Aspect(x);', '        x = Other(x);'),
                        SOURCE + SOURCE):
            with self.assertRaises(ValueError):
                transform(changed)


if __name__ == '__main__':
    unittest.main()
