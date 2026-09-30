Interpreter::Interpreter() {
    mRsp = new RSP();
    mCurMtxReplacements = &replacements;
}
void Interpreter::MatrixMul(float res[4][4], const float a[4][4], const float b[4][4]) {
    float tmp[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(res, tmp, sizeof(tmp));
}
void Interpreter::GfxSpMatrix(uint8_t parameters, const int32_t* addr) {
    float matrix[4][4];

    if (auto it = mCurMtxReplacements->find((Mtx*)addr); it != mCurMtxReplacements->end()) {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                float v = it->second.mf[i][j];
                int as_int = (int)(v * 65536.0f);
                matrix[i][j] = as_int * (1.0f / 65536.0f);
            }
        }
    } else {
#ifndef GBI_FLOATS
        // Original GBI where fixed point matrices are used
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j += 2) {
                int32_t int_part = addr[i * 2 + j / 2];
                uint32_t frac_part = addr[8 + i * 2 + j / 2];
                matrix[i][j] = (int32_t)((int_part & 0xffff0000) | (frac_part >> 16)) / 65536.0f;
                matrix[i][j + 1] = (int32_t)((int_part << 16) | (frac_part & 0xffff)) / 65536.0f;
            }
        }
#else
        // For a modified GBI where fixed point values are replaced with floats
        memcpy(matrix, addr, sizeof(matrix));
#endif
    }

    const int8_t mtx_projection = get_attr(MTX_PROJECTION);
    const int8_t mtx_load = get_attr(MTX_LOAD);
    const int8_t mtx_push = get_attr(MTX_PUSH);

    if (parameters & mtx_projection) {
        if (parameters & mtx_load) {
            memcpy(mRsp->P_matrix, matrix, sizeof(matrix));
        } else {
            MatrixMul(mRsp->P_matrix, matrix, mRsp->P_matrix);
        }
    } else { // G_MTX_MODELVIEW
        if ((parameters & mtx_push) && mRsp->modelview_matrix_stack_size < 11) {
            ++mRsp->modelview_matrix_stack_size;
            memcpy(mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1],
                   mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 2], sizeof(matrix));
        }
        if (parameters & mtx_load) {
            if (mRsp->modelview_matrix_stack_size == 0)
                ++mRsp->modelview_matrix_stack_size;
            memcpy(mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1], matrix, sizeof(matrix));
        } else {
            MatrixMul(mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1], matrix,
                      mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1]);
        }
        mRsp->lights_changed = 1;
    }
    MatrixMul(mRsp->MP_matrix, mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1], mRsp->P_matrix);
}
void Interpreter::GfxSpPopMatrix(uint32_t count) {
    while (count--) {
        if (mRsp->modelview_matrix_stack_size > 0) {
            --mRsp->modelview_matrix_stack_size;
            if (mRsp->modelview_matrix_stack_size > 0) {
                MatrixMul(mRsp->MP_matrix, mRsp->modelview_matrix_stack[mRsp->modelview_matrix_stack_size - 1],
                          mRsp->P_matrix);
            }
        }
    }
    mRsp->lights_changed = true;
}
void Interpreter::SpReset() {
    mRsp->modelview_matrix_stack_size = 1;
}
void Interpreter::GfxSpVertex(size_t n_vertices, size_t dest_index, const F3DVtx* vertices) {
    memcpy(observed,mRsp->MP_matrix,sizeof(observed));
}
void Interpreter::GfxSpMovewordF3dex2(uint8_t index, uint16_t offset, uintptr_t data) {
            const bool is_frac = (offset & 0x20) != 0;
            const int slot = (offset & 0x1F) >> 2; // 0..7
            const int row = slot >> 1;
            const int col_a = (slot & 1) << 1;
            const int col_b = col_a + 1;
            const uint32_t v = (uint32_t)data;

            auto encode = [](float f) -> int32_t {
                return (int32_t)lrintf(f * 65536.0f);
            };
            auto decode = [](int32_t fx) -> float {
                return (float)fx * (1.0f / 65536.0f);
            };

            int32_t fx_a = encode(mRsp->MP_matrix[row][col_a]);
            int32_t fx_b = encode(mRsp->MP_matrix[row][col_b]);

            const uint16_t new_a = (uint16_t)(v >> 16);
            const uint16_t new_b = (uint16_t)(v & 0xFFFF);

            // Replace half of each fixed-point pair without disturbing the
            // other half.  All math is done as uint32_t to avoid signed-shift
            // UB; the final cast back to int32_t reinterprets bit 31 as the
            // sign of the s15.16 value, matching the existing decoder.
            const uint32_t fx_a_u = (uint32_t)fx_a;
            const uint32_t fx_b_u = (uint32_t)fx_b;
            if (is_frac) {
                fx_a = (int32_t)((fx_a_u & 0xFFFF0000u) | new_a);
                fx_b = (int32_t)((fx_b_u & 0xFFFF0000u) | new_b);
            } else {
                fx_a = (int32_t)(((uint32_t)new_a << 16) | (fx_a_u & 0xFFFFu));
                fx_b = (int32_t)(((uint32_t)new_b << 16) | (fx_b_u & 0xFFFFu));
            }

            mRsp->MP_matrix[row][col_a] = decode(fx_a);
            mRsp->MP_matrix[row][col_b] = decode(fx_b);
}
