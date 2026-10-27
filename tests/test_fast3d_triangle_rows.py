"""Shared TRI2 rows: projection anchors and the indexed/flat stream contract."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from tests.test_renderer_replay import compile_run
from xbox360_platform import fast3d_triangle_rows as rows

SOURCE = '''const size_t MAX_TRI_BUFFER = 256;
Interpreter::Interpreter() {
    mCertified = 0;
}
void Interpreter::Flush() {
    if (mBufVboLen > 0) {
        mRapi->DrawTriangles(mBufVbo, mBufVboLen, mBufVboNumTris);
        mBufVboLen = 0;
    }
}
void Interpreter::GfxSpTri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {
    if ((mRsp->geometry_mode & cull_both) != 0) {
        const uint32_t projectionRounding = Shipyard::Fast3D::ProjectionRoundingMode();
        p1.Load(v1->x, v1->y, v1->w, projectionRounding);
        float dx1 = p1.x - p2.x;
        float dy1 = p1.y - p2.y;
        float dx2 = p3.x - p2.x;
        float dy2 = p3.y - p2.y;
        float cross = dx1 * dy2 - dy1 * dx2;
    }
    XboxEmitTriangle(vtx1_idx, vtx2_idx, vtx3_idx, is_rect);
}
void Interpreter::Reconcile() {
    p.numInputs = numInputs;
    mXboxPipelineKey.Capture(*this,p);
}
void Interpreter::XboxEmitTriangle(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {
    float* out = mBufVbo + mBufVboLen;
    for (int i = 0; i < 3; i++) {
        float z = v_arr[i]->z;
        *out++ = v_arr[i]->x;
    }
    mBufVboLen = (size_t)(out - mBufVbo);
    ++mBufVboNumTris;
}
static bool sXboxTriangleRunEnabled = false;
bool gfx_tri2_handler_f3dex(F3DGfx** cmd0) {
    const bool xboxRun = sXboxTriangleRunEnabled;
    for (;;) {
        if (!xboxRun) break;
    }
    return false;
}
void Interpreter::Run() {
    g_exec_stack.start((F3DGfx*)commands);
    sXboxTriangleRunEnabled = !xboxDebugging;
}
'''


def project(text, uses=2):
    text = rows.source(text, constructor='    mCertified = 0;',
                       draw='        mRapi->DrawTriangles(mBufVbo, mBufVboLen, mBufVboNumTris);',
                       entry='void Interpreter::Reconcile() {', share='kShare')
    text = rows.cull(text, first='        const uint32_t projectionRounding = Shipyard::Fast3D::ProjectionRoundingMode();\n')
    return rows.row_emitter(text, signature='void Interpreter::XboxEmitTriangle(',
                            begin='    float* out = mBufVbo + mBufVboLen;',
                            end='    mBufVboLen = (size_t)(out - mBufVbo);',
                            loop='    for (int i = 0; i < 3; i++) {\n        float z = ', uses=uses)


class TriangleRowProjectionTests(unittest.TestCase):
    def test_projection_shape(self):
        text = project(SOURCE)
        flush = text[text.index('void Interpreter::Flush() {'):text.index('void Interpreter::GfxSpTri1(')]
        self.assertIn('        if (mXboxTriangles.Indexed(mBufVboNumTris))\n'
                      '            mXboxTriangles.Draw(mBufVbo, mBufVboLen, mBufVboNumTris);\n'
                      '        else\n            mRapi->DrawTriangles(', flush)
        self.assertTrue(flush.rstrip().endswith('    mXboxTriangles.Clear();\n}'))
        self.assertIn('    mCertified = 0;\n    mXboxTriangles.Reset();', text)
        self.assertIn('void Interpreter::Reconcile() {\n    mXboxTriangles.Invalidate();', text)
        self.assertLess(text.index('mXboxTriangles.Prepare('), text.index('mXboxPipelineKey.Capture'))
        self.assertIn('sXboxTriangleRunEnabled && gShipyardTriangleRowControl[0] != 0 && kShare;', text)
        handler = text[text.index('bool gfx_tri2_handler_f3dex'):]
        self.assertLess(handler.index('Begin(xboxRun && sXboxTriangleRows)'), handler.index('for (;;)'))
        self.assertLess(handler.index('End();'), handler.index('return false;'))
        tri = text[text.index('void Interpreter::GfxSpTri1('):text.index('void Interpreter::Reconcile()')]
        self.assertLess(tri.index('if (mXboxTriangles.run)'), tri.index('} else {'))
        self.assertLess(tri.index('} else {'), tri.index('ProjectionRoundingMode'))
        self.assertNotIn('float dx1 =', tri)
        emit = text[text.index('void Interpreter::XboxEmitTriangle('):]
        self.assertNotIn('v_arr[i]', emit[emit.index('if (xboxPackCount) {'):])
        self.assertIn('for (int i = 0; i < xboxPackCount; i++)', emit)
        self.assertIn('mBufVboLen = (size_t)(out - mBufVbo);\n    }\n    ++mBufVboNumTris;', emit)

    def test_anchor_drift_raises(self):
        with self.assertRaises(ValueError):
            project(SOURCE, uses=3)
        with self.assertRaises(ValueError):
            project(SOURCE.replace('    sXboxTriangleRunEnabled = !xboxDebugging;\n', ''))
        with self.assertRaises(ValueError):
            project(SOURCE.replace('        float dy2 = ', '        dy2 = '))
        with self.assertRaises(ValueError):
            rows.header('class Interpreter {\n};', '    void Missing();')

    def test_column_emitter(self):
        text = SOURCE.replace('''    float* out = mBufVbo + mBufVboLen;
    for (int i = 0; i < 3; i++) {
        float z = v_arr[i]->z;
        *out++ = v_arr[i]->x;
    }
    mBufVboLen = (size_t)(out - mBufVbo);''', '''    unsigned xboxStride=4;
    float* xboxRows[3]={mBufVbo+mBufVboLen,mBufVbo+mBufVboLen+xboxStride,mBufVbo+mBufVboLen+2*xboxStride};
    for(int i=0;i<3;++i)xboxRows[i][0]=v_arr[i]->x;
    Three(xboxRows[0][1],xboxRows[1][1],xboxRows[2][1],v_arr[0]->u,v_arr[1]->u,v_arr[2]->u);
    mBufVboLen += 3*xboxStride;''')
        out = rows.column_emitter(
            text, signature='void Interpreter::XboxEmitTriangle(',
            rows='    float* xboxRows[3]={mBufVbo+mBufVboLen,mBufVbo+mBufVboLen+xboxStride,mBufVbo+mBufVboLen+2*xboxStride};',
            end='    mBufVboLen += 3*xboxStride;', uses=4, loops=1)
        self.assertIn('(xboxPackCount>2?2*xboxStride:0)', out)
        self.assertIn('for(int i=0;i<xboxPackCount;++i)xboxRows[i][0]=xboxPack[i]->x;', out)
        self.assertIn('xboxPack[0]->u,xboxPack[1]->u,xboxPack[2]->u', out)
        self.assertIn('    mBufVboLen += xboxPackCount*xboxStride;\n    }', out)

    def test_indexed_rows_expand_to_flat_stream(self):
        compile_run(self, (ROOT / 'tests/triangle_rows_contract.cpp').read_text())

    def test_xdk_compiles_triangle_and_vertex_paths(self):
        xdk = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
        with tempfile.TemporaryDirectory(prefix='shipyard-fast3d-paths-') as folder:
            output = Path(folder)
            ppc = subprocess.run([str(xdk / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/fp:precise', '/EHsc',
                                  '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(xdk / 'include/xbox'),
                                  '/I' + str(ROOT / 'sources'), '/Fo' + str(output / 'paths.obj'),
                                  str(ROOT / 'tests/fast3d_paths_xdk.cpp')], cwd=output, capture_output=True, text=True)
            self.assertEqual(ppc.returncode, 0, ppc.stdout + ppc.stderr)
            self.assertEqual((output / 'paths.obj').read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
