"""Materialize the derived modelview/projection matrix only at observations."""
import re
from .resource_lookup import _once


def header(source):
    source=_once(source,'    float MP_matrix[4][4];','    float MP_matrix[4][4];\n    bool xboxMPDirty;')
    return _once(source,'    static void MatrixMul(float res[4][4], const float a[4][4], const float b[4][4]);',
        '    static void MatrixMul(float res[4][4], const float a[4][4], const float b[4][4]);\n    void EnsureMatrixProduct();')


def source(text):
    allowed={'GfxSpMatrix','GfxSpPopMatrix','GfxSpVertex','GfxSpMovewordF3dex2'}
    covered=0
    for match in re.finditer(r'^void Interpreter::(\w+)\([^\n]*\) \{.*?^}',text,re.M|re.S):
        count=match[0].count('mRsp->MP_matrix')
        if count and match[1] not in allowed: raise ValueError('Unreviewed MP observer: '+match[1])
        covered+=count
    if covered!=text.count('mRsp->MP_matrix'): raise ValueError('MP access inventory changed')
    eager='MatrixMul(mRsp->MP_matrix, mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1], mRsp->P_matrix);'
    text=_once(text,eager,'mRsp->xboxMPDirty = true;')
    text=_once(text,'MatrixMul(mRsp->MP_matrix, mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1],\n                          mRsp->P_matrix);','mRsp->xboxMPDirty = true;')
    # Empty-stack pop retains the last valid product in the pinned interpreter.
    text=_once(text,'            --mRsp->modelview_matrix_stack_size;',
        '            if (mRsp->modelview_matrix_stack_size == 1) EnsureMatrixProduct();\n            --mRsp->modelview_matrix_stack_size;')
    copy="""            memcpy(mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1],
                   mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 2], sizeof(matrix));"""
    text=_once(text,copy,'            if (!(parameters & mtx_load))\n'+copy)
    text=_once(text,'void Interpreter::SpReset() {','void Interpreter::SpReset() {\n    EnsureMatrixProduct();')
    text=_once(text,'void Interpreter::GfxSpVertex(size_t n_vertices, size_t dest_index, const F3DVtx* vertices) {',
        'void Interpreter::GfxSpVertex(size_t n_vertices, size_t dest_index, const F3DVtx* vertices) {\n    EnsureMatrixProduct();')
    if 'int32_t fx_a = encode(mRsp->MP_matrix[row][col_a]);' in text:
        text=_once(text,'            int32_t fx_a = encode(mRsp->MP_matrix[row][col_a]);',
            '            EnsureMatrixProduct();\n            int32_t fx_a = encode(mRsp->MP_matrix[row][col_a]);')
    # Initialization is explicit even on XDK aggregate-lowering paths.
    text=_once(text,'    mRsp = new RSP();','    mRsp = new RSP();\n    mRsp->xboxMPDirty = false;')
    return _once(text,'void Interpreter::GfxSpMatrix(uint8_t parameters, const int32_t* addr) {',
        """void Interpreter::EnsureMatrixProduct() {
    if (!mRsp->xboxMPDirty) return;
    MatrixMul(mRsp->MP_matrix, mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1], mRsp->P_matrix);
    mRsp->xboxMPDirty = false;
}

void Interpreter::GfxSpMatrix(uint8_t parameters, const int32_t* addr) {""")
