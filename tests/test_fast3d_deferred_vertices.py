"""Deferred lit/fog vertices: projection anchors and the eager-equality contract."""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from tests.test_renderer_replay import compile_run
from xbox360_platform import fast3d_deferred_vertices as deferred
from xbox360_platform import vertex_invariants

SOURCE = '''Interpreter::Interpreter() {
    mCertified = 0;
}
void Interpreter::GfxSpVertex(size_t n_vertices, size_t dest_index, const F3DVtx* vertices) {
    EnsureMatrixProduct();
    for (size_t i = 0; i < n_vertices; i++) {
    }
}
void Interpreter::GfxSpModifyVertex(uint16_t vtx_idx, uint8_t where, uint32_t val) {
    SUPPORT_CHECK(where == G_MWO_POINT_ST);
    v->u = s;
}
void Interpreter::GfxSpTri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {
    if (v1->clip_rej & v2->clip_rej & v3->clip_rej) {
        return;
    }
    XboxEmitTriangle(vtx1_idx, vtx2_idx, vtx3_idx, is_rect);
}
void Interpreter::Run() {
    sXboxTriangleRunEnabled = !xboxDebugging;
}
'''

MATRIX = '''void Interpreter::GfxSpMatrix(uint8_t parameters, const int32_t* addr) {
    float matrix[4][4];
    { auto it = find(); if (it != end) {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                int as_int = (int)(v * 65536.0f);
            }
        }
    } else {
        for (int i = 0; i < 4; i++) {
        }
    } }
}
void Interpreter::GfxSpPopMatrix(uint32_t count) {
}
void Interpreter::CalculateNormalDir(const F3DLight_t* light, float coeffs[3]) {
    float light_dir[3] = { light->dir[0] / 127.0f, light->dir[1] / 127.0f, light->dir[2] / 127.0f };
}
'''

ASPECT = '''float Aspect(float x) {
    return x * (4.0f / 3.0f) / ((float)width / (float)height);
}
void Vertex() {
    for (unsigned i=0; i<count; ++i) {
        x = Aspect(x);
    }
}
void Next() {}
'''

EMIT = '    XboxEmitTriangle(vtx1_idx, vtx2_idx, vtx3_idx, is_rect);'


class DeferredVertexProjectionTests(unittest.TestCase):
    def test_projection_shape(self):
        text = deferred.source(SOURCE, constructor='    mCertified = 0;', positions='    POSITIONS;\n', emit=EMIT)
        self.assertIn('    mCertified = 0;\n    mXboxDeferred.Reset();', text)
        method = text[text.index('bool Interpreter::XboxDeferVertices('):text.index('// SHIPYARD DEFERRED VERTICES END')]
        self.assertLess(method.index('mXboxDeferred.Discard(dest_index, n_vertices);'), method.index('.enabled'))
        self.assertLess(method.index('TransformPositionsUnaligned'), method.index('    POSITIONS;'))
        self.assertLess(method.index('    POSITIONS;'), method.index('mXboxDeferred.Commit('))
        vertex = text[text.index('void Interpreter::GfxSpVertex('):]
        self.assertIn('    EnsureMatrixProduct();\n    if (XboxDeferVertices(n_vertices, dest_index, vertices)) return;',
                      vertex)
        self.assertIn('SUPPORT_CHECK(where == G_MWO_POINT_ST);\n    mXboxDeferred.Materialize(vtx_idx, ', text)
        tri = text[text.index('void Interpreter::GfxSpTri1('):]
        self.assertLess(tri.index('return;'), tri.index('mXboxDeferred.Materialize(vtx1_idx'))
        self.assertLess(tri.index('mXboxDeferred.Materialize(vtx3_idx'), tri.index('XboxEmitTriangle('))
        self.assertIn('!xboxDebugging;\n    gShipyardDeferredVertexControl[1] = mXboxDeferred.Configure(', text)
        header = deferred.header('class Interpreter {\n    void GfxSpVertex(size_t numVertices, size_t destIndex, '
                                 'const F3DVtx* vertices);\n    int mLast;\n};', member='    int mLast;')
        self.assertIn('    int mLast;\n' + deferred.MEMBER, header)
        self.assertIn(deferred.DECLARATION + '\n    void GfxSpVertex(', header)

    def test_anchor_drift_raises(self):
        with self.assertRaises(ValueError):
            deferred.source(SOURCE.replace('    SUPPORT_CHECK(where == G_MWO_POINT_ST);\n', ''),
                            constructor='    mCertified = 0;', positions='', emit=EMIT)
        with self.assertRaises(ValueError):
            deferred.source(SOURCE, constructor='    mCertified = 0;', positions='', emit='    Missing();')

    def test_matrix_and_light_direction(self):
        text = vertex_invariants.light_direction(vertex_invariants.matrix_quantize(MATRIX))
        self.assertIn('    __declspec(align(16)) float matrix[4][4];', text)
        self.assertIn('QuantizeMatrix(it->second.mf, matrix)) {\n        for (int i = 0; i < 4; i++) {', text)
        self.assertEqual(text.count('QuantizeMatrix('), 1)
        self.assertEqual(text.count('DivideNormalByte127(Shipyard::Fast3D::NormalByteFloat(light->dir['), 3)
        with self.assertRaises(ValueError):
            vertex_invariants.light_direction(MATRIX.replace('/ 127.0f, light->dir[1]', '* 2, light->dir[1]'))

    def test_aspect_reciprocal_option(self):
        text = vertex_invariants.aspect_denominator(
            ASPECT, start='void Vertex()', end='void Next()', loop='    for (unsigned i=0; i<count; ++i) {',
            eligible='validPacket', framebuffer='framebuffer', denominator='(float)width / (float)height',
            original_statement='        x = Aspect(x);', numerator='x * (4.0f / 3.0f)', reciprocal=True)
        self.assertTrue(text.startswith('#include "fast3d/aspect_reciprocal.h"\n'))
        self.assertIn('shipyardAspect = Shipyard::Fast3D::AspectReciprocal(shipyardAspectDenominator);', text)
        self.assertIn('x = shipyardAspect.Divide(x * (4.0f / 3.0f), shipyardAspectDenominator);', text)

    def test_materialized_bytes_match_eager_loops(self):
        compile_run(self, (ROOT / 'tests/deferred_vertex_contract.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
