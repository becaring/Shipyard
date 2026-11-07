"""Walk an interpolation batch's command list once and replay it.

Interpolated presentations of one game frame share a command list; they differ
only in matrix replacements and lerped tile coordinates. The batch's first Run
walks the list as usual and also evaluates every later presentation's
matrices, vertex positions, culling and lerped texture offsets, recording each
renderer call with one vertex stream per presentation
(fast3d/walk_replay.h). Later Runs reissue the recorded calls instead of
walking. Anything a replay cannot reproduce (a framebuffer readback, a
texture rewritten after a draw sampled it, a depth branch or tile size that
differs between presentations) taints the record, and those presentations
walk the list themselves.

Triangles and display-list culls are kept when any presentation keeps them:
geometry rejected in one presentation lies outside its view volume, and the
GPU clips it there.
"""

import re

HEADER_ANCHOR = re.compile(r'^    float mInterpolationT[^;\n]*;\n', re.M)
RUN_DEBUGGING = '    auto dbg = mGfxDebugger;\n    const bool xboxDebugging = dbg->IsDebugging();\n'
RUN_END = '    Flush();\n    mGfxFrameBuffer = 0;\n'
FLUSH_DRAW = '''        if (mXboxTriangles.Indexed(mBufVboNumTris))
            mXboxTriangles.Draw(mBufVbo, mBufVboLen, mBufVboNumTris);
        else
            mRapi->DrawTriangles(mBufVbo, mBufVboLen, mBufVboNumTris);
'''
ROW_START = '    float* shipyardVertexOutput = mBufVbo + mBufVboLen;\n'
ROW_END = '    mBufVboLen = (size_t)(shipyardVertexOutput - mBufVbo);\n'
TRIANGLE = 'void Interpreter::GfxSpTri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {'
TRIANGLE_END = '    mXboxDeferred.Materialize(vtx1_idx, mRsp->loaded_vertices);'
LERP = '    gfx->mRdp->texture_tile[tile].lrt = coords[3] + t * (coords[7] - coords[3]);\n'
TILE_INTERP = '    if (gfx->mInterpolationIndex == gfx->mInterpolationIndexTarget) {\n'
SEGMENT_INTERP = '        case G_MW_SEGMENT_INTERP: {\n'
CULL_DL = '    return gfx_end_dl_handler_common(cmd0);\n}\n// SHIPYARD DISPLAY LIST CULL END'
BRANCH_Z = '    uint32_t zval = (uint32_t)((*cmd0)->words.w1);\n'
METADATA = ('comb->xboxShaderMetadata.Lookup(tm, mRapi,', 'comb->xboxShaderMetadata.Store(tm, mRapi,')

MEMBERS = r'''    // SHIPYARD WALK REPLAY (fast3d_walk_replay.py)
    enum { kXboxMaxReplays = 23 };
    struct XboxSubframe {
        float P[4][4];
        float modelview[11][4][4];
        float MP[4][4];
        bool dirty;
        struct Position {
            float x, y, z, w;
            uint8_t clip;
        } positions[MAX_VERTICES + 4];
    };
    // A tile's lerped coordinates for each later presentation, valid while
    // the tile still holds the coordinates this presentation lerped.
    struct XboxTileLerp {
        float main[4];
        float sub[kXboxMaxReplays][4];
        bool valid;
    };
    XboxSubframe* mXboxSub;
    XboxTileLerp mXboxSubTiles[8];
    unsigned mXboxSubCount;
    long long mXboxWalkStart;
    Shipyard::Fast3D::WalkLog<kXboxMaxReplays>* mXboxWalk;
    class XboxWalkRecorder* mXboxRecorder;
    const std::unordered_map<Mtx*, MtxF>* mXboxBatchMaps[kXboxMaxReplays + 1];
    float mXboxBatchT[kXboxMaxReplays + 1];
    unsigned mXboxBatchCount;
    const void* mXboxBatchCommands;
    GfxRenderingAPI* mXboxLiveRapi;
    // RenderReplays brackets each batch; maps and fractions are per presentation.
    void XboxBeginWalkBatch(const void* commands, const std::unordered_map<Mtx*, MtxF>* maps,
                            const float* fractions, unsigned count);
    void XboxEndWalkBatch();
    bool XboxBeginRecording(const void* commands, bool debugging);
    void XboxEndRecording();
    bool XboxWalkReplay(const void* commands, bool debugging);
    void XboxSubProduct(unsigned replay, unsigned size);
    void XboxSubMatrix(uint8_t parameters, const int32_t* addr);
    void XboxSubPopMatrix(uint32_t count);
    void XboxSubVertices(size_t n_vertices, size_t dest_index, const F3DVtx* vertices);
    bool XboxSubAccept(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect);
    bool XboxSubVisible(unsigned first, unsigned last);
    void XboxSubBranchZ(unsigned vertex, uint32_t zval);
    void XboxSubTileLerp(int tile, const float coords[8]);
    void XboxSubRows(const float* start, const float* end, LoadedVertex* const* pack, int count, bool is_rect,
                     const bool* usedTextures, uint32_t tm, const uint32_t* effective_tile,
                     const uint32_t* tex_width, const uint32_t* tex_height, bool halfTexel,
                     const GfxClipParameters& clip);
    void XboxRecordDraw(bool indexed);
'''

CONSTRUCT = r'''
    mXboxSub = NULL;
    memset(mXboxSubTiles, 0, sizeof(mXboxSubTiles));
    mXboxSubCount = 0;
    mXboxWalkStart = 0;
    mXboxWalk = NULL;
    mXboxRecorder = NULL;
    mXboxBatchCount = 0;
    mXboxBatchCommands = NULL;
    mXboxLiveRapi = NULL;'''

HELPERS = r'''// SHIPYARD WALK REPLAY BEGIN
// Live A/B control sampled at each batch start: [0] enables replays; [1..3]
// count recorded walks, replayed presentations and tainted walks. Taints are
// counted per Shipyard::Fast3D::WalkTaint reason.
extern "C" volatile unsigned gShipyardWalkReplayControl[4] = { 1, 0, 0, 0 };
extern "C" volatile unsigned gShipyardWalkReplayTaints[Shipyard::Fast3D::WalkTaintCount] = { 0 };
// Microseconds spent in recording walks [0] and in replays [1].
extern "C" volatile unsigned gShipyardWalkReplayMicros[2] = { 0, 0 };
static long long XboxWalkMicros() {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return now.QuadPart / (frequency.QuadPart / 1000000);
}

// Forwards every call to the live renderer and records the ones a replay
// reissues. Flush records each draw with its rows, kept once for all replays.
class XboxWalkRecorder : public GfxRenderingAPI {
    typedef Shipyard::Fast3D::WalkOp Op;
    void Unsupported() { log->Taint(Shipyard::Fast3D::WalkUnsupportedCall); }
    Op& Push(Op::Kind kind) { return log->Push(kind); }
  public:
    GfxRenderingAPI* live;
    Shipyard::Fast3D::WalkLog<Interpreter::kXboxMaxReplays>* log;
    XboxWalkRecorder() : live(NULL), log(NULL) {}
    const char* GetName() { return live->GetName(); }
    int GetMaxTextureSize() { return live->GetMaxTextureSize(); }
    GfxClipParameters GetClipParameters() { return live->GetClipParameters(); }
    void UnloadShader(ShaderProgram* p) { live->UnloadShader(p); Push(Op::UnloadShader).pointer = p; }
    void LoadShader(ShaderProgram* p) { live->LoadShader(p); Push(Op::LoadShader).pointer = p; }
    void ClearShaderCache() { Unsupported(); live->ClearShaderCache(); }
    ShaderProgram* CreateAndLoadNewShader(uint64_t id0, uint64_t id1) {
        ShaderProgram* p = live->CreateAndLoadNewShader(id0, id1);
        Push(Op::LoadShader).pointer = p;
        return p;
    }
    ShaderProgram* LookupShader(uint64_t id0, uint64_t id1) { return live->LookupShader(id0, id1); }
    void ShaderGetInfo(ShaderProgram* p, uint8_t* n, bool used[2]) { live->ShaderGetInfo(p, n, used); }
    uint32_t NewTexture() { return live->NewTexture(); }
    void SelectTexture(int tile, uint32_t id) {
        live->SelectTexture(tile, id);
        Op& op = Push(Op::SelectTexture);
        op.args[0] = tile;
        op.args[1] = (int)id;
        log->Selected(id);
    }
    void UploadTexture(const uint8_t* pixels, uint32_t width, uint32_t height) {
        live->UploadTexture(pixels, width, height);
        log->Uploaded();
    }
    void SetSamplerParameters(int sampler, bool linear, uint32_t cms, uint32_t cmt) {
        live->SetSamplerParameters(sampler, linear, cms, cmt);
        Op& op = Push(Op::Sampler);
        op.args[0] = sampler; op.args[1] = linear; op.args[2] = (int)cms; op.args[3] = (int)cmt;
    }
    void SetDepthTestAndMask(bool test, bool mask) {
        live->SetDepthTestAndMask(test, mask);
        Op& op = Push(Op::Depth);
        op.args[0] = test; op.args[1] = mask;
    }
    void SetZmodeDecal(bool decal) { live->SetZmodeDecal(decal); Push(Op::Decal).args[0] = decal; }
    void SetViewport(int x, int y, int width, int height) {
        live->SetViewport(x, y, width, height);
        Op& op = Push(Op::Viewport);
        op.args[0] = x; op.args[1] = y; op.args[2] = width; op.args[3] = height;
    }
    void SetScissor(int x, int y, int width, int height) {
        live->SetScissor(x, y, width, height);
        Op& op = Push(Op::Scissor);
        op.args[0] = x; op.args[1] = y; op.args[2] = width; op.args[3] = height;
    }
    void SetUseAlpha(bool alpha) { live->SetUseAlpha(alpha); Push(Op::Alpha).args[0] = alpha; }
    void DrawTriangles(float* vertices, size_t floats, size_t triangles) { live->DrawTriangles(vertices, floats, triangles); }
    void Init() { Unsupported(); live->Init(); }
    void OnResize() { Unsupported(); live->OnResize(); }
    void StartFrame() { Unsupported(); live->StartFrame(); }
    void EndFrame() { Unsupported(); live->EndFrame(); }
    void FinishRender() { Unsupported(); live->FinishRender(); }
    int CreateFramebuffer() { Unsupported(); return live->CreateFramebuffer(); }
    void UpdateFramebufferParameters(int fb, uint32_t width, uint32_t height, uint32_t msaa, bool invertY,
                                     bool target, bool depth, bool extract) {
        live->UpdateFramebufferParameters(fb, width, height, msaa, invertY, target, depth, extract);
        Op& op = Push(Op::Framebuffer);
        op.args[0] = fb; op.args[1] = (int)width; op.args[2] = (int)height; op.args[3] = (int)msaa;
        op.args[4] = invertY; op.args[5] = target; op.args[6] = depth; op.args[7] = extract;
    }
    void StartDrawToFramebuffer(int fb, float noise) {
        live->StartDrawToFramebuffer(fb, noise);
        Op& op = Push(Op::StartFramebuffer);
        op.args[0] = fb; op.value = noise;
    }
    void CopyFramebuffer(int dst, int src, int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1) {
        live->CopyFramebuffer(dst, src, sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1);
        Op& op = Push(Op::CopyFramebuffer);
        const int args[10] = { dst, src, sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1 };
        memcpy(op.args, args, sizeof(args));
    }
    void ClearFramebuffer(bool color, bool depth) {
        live->ClearFramebuffer(color, depth);
        Op& op = Push(Op::ClearFramebuffer);
        op.args[0] = color; op.args[1] = depth;
    }
    void ClearDepthRegion(int x, int y, int width, int height) {
        live->ClearDepthRegion(x, y, width, height);
        Op& op = Push(Op::ClearDepthRegion);
        op.args[0] = x; op.args[1] = y; op.args[2] = width; op.args[3] = height;
    }
    void ReadFramebufferToCPU(int fb, uint32_t width, uint32_t height, uint16_t* pixels) {
        Unsupported();
        live->ReadFramebufferToCPU(fb, width, height, pixels);
    }
    void ResolveMSAAColorBuffer(int dst, int src) {
        live->ResolveMSAAColorBuffer(dst, src);
        Op& op = Push(Op::ResolveMsaa);
        op.args[0] = dst; op.args[1] = src;
    }
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fb, const std::set<std::pair<float, float>>& coordinates) {
        Unsupported();
        return live->GetPixelDepth(fb, coordinates);
    }
    void* GetFramebufferTextureId(int fb) { return live->GetFramebufferTextureId(fb); }
    void SelectTextureFb(int fb) {
        live->SelectTextureFb(fb);
        Push(Op::SelectTextureFb).args[0] = fb;
        log->SelectedFramebuffer();
    }
    void DeleteTexture(uint32_t id) { Unsupported(); live->DeleteTexture(id); }
    void SetTextureFilter(FilteringMode mode) { Unsupported(); live->SetTextureFilter(mode); }
    FilteringMode GetTextureFilter() { return live->GetTextureFilter(); }
    void SetSrgbMode() { Unsupported(); live->SetSrgbMode(); }
    ImTextureID GetTextureById(int id) { return live->GetTextureById(id); }
    void SetCurrentPrimDepth(float depth) { live->SetCurrentPrimDepth(depth); Push(Op::PrimDepth).value = depth; }
};

void Interpreter::XboxBeginWalkBatch(const void* commands, const std::unordered_map<Mtx*, MtxF>* maps,
                                     const float* fractions, unsigned count) {
    mXboxBatchCount = 0;
    if (!gShipyardWalkReplayControl[0] || count < 2 || count > kXboxMaxReplays + 1 ||
        Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger("gXbox.WalkReplay", 1) == 0 ||
        Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger("gXbox.TraceHud", 0) != 0)
        return;
    if (!mXboxWalk) {
        mXboxWalk = new Shipyard::Fast3D::WalkLog<kXboxMaxReplays>();
        mXboxSub = new XboxSubframe[kXboxMaxReplays];
        mXboxRecorder = new XboxWalkRecorder();
    }
    for (unsigned i = 0; i < count; ++i) {
        mXboxBatchMaps[i] = &maps[i];
        mXboxBatchT[i] = fractions[i];
    }
    mXboxWalk->Begin(commands, 0);
    mXboxBatchCount = count;
    mXboxBatchCommands = commands;
}

void Interpreter::XboxEndWalkBatch() {
    mXboxBatchCount = 0;
    if (mXboxWalk) mXboxWalk->Begin(NULL, 0);
}

bool Interpreter::XboxBeginRecording(const void* commands, bool debugging) {
    if (!mXboxBatchCount || mInterpolationIndex != 0 || debugging || commands != mXboxBatchCommands)
        return false;
    typedef Shipyard::Fast3D::WalkOp Op;
    mXboxWalk->Begin(commands, mXboxBatchCount - 1);
    ++gShipyardWalkReplayControl[1];
    mXboxWalkStart = XboxWalkMicros();
    mXboxSubCount = mXboxBatchCount - 1;
    for (unsigned k = 0; k < mXboxSubCount; ++k) {
        XboxSubframe& s = mXboxSub[k];
        memcpy(s.P, mRsp->P_matrix, sizeof(s.P));
        memcpy(s.modelview, mRsp->modelview_matrix_stack, sizeof(s.modelview));
        memcpy(s.MP, mRsp->MP_matrix, sizeof(s.MP));
        s.dirty = mRsp->xboxMPDirty;
    }
    for (unsigned i = 0; i < 8; ++i) mXboxSubTiles[i].valid = false;
    // Replays start from the renderer state this walk assumes it inherits.
    if (mRenderingState.mShaderProgram) mXboxWalk->Push(Op::LoadShader).pointer = mRenderingState.mShaderProgram;
    mXboxWalk->Push(Op::Alpha).args[0] = mRenderingState.alpha_blend;
    Op& depth = mXboxWalk->Push(Op::Depth);
    depth.args[0] = (mRenderingState.depth_test_and_mask & 1) != 0;
    depth.args[1] = (mRenderingState.depth_test_and_mask & 2) != 0;
    mXboxWalk->Push(Op::Decal).args[0] = mRenderingState.decal_mode;
    mXboxRecorder->live = mRapi;
    mXboxRecorder->log = mXboxWalk;
    mRapi = mXboxRecorder;
    return true;
}

void Interpreter::XboxEndRecording() {
    mRapi = mXboxRecorder->live;
    mXboxSubCount = 0;
    if (std::uncaught_exception()) mXboxWalk->Taint(Shipyard::Fast3D::WalkThrown);
    mXboxWalk->Finish();
    gShipyardWalkReplayMicros[0] += (unsigned)(XboxWalkMicros() - mXboxWalkStart);
    if (mXboxWalk->taint) {
        ++gShipyardWalkReplayControl[3];
        ++gShipyardWalkReplayTaints[mXboxWalk->taint];
    }
}

struct XboxRecordingScope {
    Interpreter* gfx;
    ~XboxRecordingScope() { if (gfx) gfx->XboxEndRecording(); }
};

bool Interpreter::XboxWalkReplay(const void* commands, bool debugging) {
    if (!mXboxBatchCount || debugging || mInterpolationIndex < 1 ||
        !mXboxWalk->Replays(commands, (unsigned)mInterpolationIndex - 1))
        return false;
    typedef Shipyard::Fast3D::WalkOp Op;
    const unsigned replay = (unsigned)mInterpolationIndex - 1;
    Shipyard::Fast3D::WalkLog<kXboxMaxReplays>& log = *mXboxWalk;
    const long long started = XboxWalkMicros();
    log.Apply(replay);
    for (size_t i = 0; i < log.ops.size(); ++i) {
        const Op& op = log.ops[i];
        const int* a = op.args;
        switch (op.kind) {
            case Op::UnloadShader: mRapi->UnloadShader((ShaderProgram*)op.pointer); break;
            case Op::LoadShader: mRapi->LoadShader((ShaderProgram*)op.pointer); break;
            case Op::SelectTexture: mRapi->SelectTexture(a[0], (uint32_t)a[1]); break;
            case Op::SelectTextureFb: mRapi->SelectTextureFb(a[0]); break;
            case Op::Sampler: mRapi->SetSamplerParameters(a[0], a[1] != 0, (uint32_t)a[2], (uint32_t)a[3]); break;
            case Op::Depth: mRapi->SetDepthTestAndMask(a[0] != 0, a[1] != 0); break;
            case Op::Decal: mRapi->SetZmodeDecal(a[0] != 0); break;
            case Op::Viewport: mRapi->SetViewport(a[0], a[1], a[2], a[3]); break;
            case Op::Scissor: mRapi->SetScissor(a[0], a[1], a[2], a[3]); break;
            case Op::Alpha: mRapi->SetUseAlpha(a[0] != 0); break;
            case Op::PrimDepth: mRapi->SetCurrentPrimDepth(op.value); break;
            case Op::Framebuffer:
                mRapi->UpdateFramebufferParameters(a[0], (uint32_t)a[1], (uint32_t)a[2], (uint32_t)a[3],
                                                   a[4] != 0, a[5] != 0, a[6] != 0, a[7] != 0);
                break;
            case Op::StartFramebuffer: mRapi->StartDrawToFramebuffer(a[0], op.value); break;
            case Op::CopyFramebuffer:
                mRapi->CopyFramebuffer(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
                break;
            case Op::ClearFramebuffer: mRapi->ClearFramebuffer(a[0] != 0, a[1] != 0); break;
            case Op::ClearDepthRegion: mRapi->ClearDepthRegion(a[0], a[1], a[2], a[3]); break;
            case Op::ResolveMsaa: mRapi->ResolveMSAAColorBuffer(a[0], a[1]); break;
            case Op::Draw: {
                float* vertices = log.Stream(op);
#if defined(SHIPYARD_XENOS_INDEXED_TRIANGLES) && defined(_XBOX)
                if (const uint16_t* corners = log.Corners(op)) {
                    ::Fast::ShipyardXenos::DrawIndexedTriangles(vertices, op.floats, op.rows, corners, op.triangles);
                    break;
                }
#endif
                mRapi->DrawTriangles(vertices, op.floats, op.triangles);
            } break;
        }
    }
    mXboxPipelineKey.valid = false;
    ++gShipyardWalkReplayControl[2];
    gShipyardWalkReplayMicros[1] += (unsigned)(XboxWalkMicros() - started);
    return true;
}

void Interpreter::XboxSubProduct(unsigned replay, unsigned size) {
    XboxSubframe& s = mXboxSub[replay];
    if (!s.dirty || size == 0) return;
    MatrixMul(s.MP, s.modelview[size - 1], s.P);
    s.dirty = false;
}

// GfxSpMatrix's decoding, for one presentation's replacements.
static void XboxDecodeMatrix(const std::unordered_map<Mtx*, MtxF>& replacements, const int32_t* addr,
                             float matrix[4][4]) {
    std::unordered_map<Mtx*, MtxF>::const_iterator it = replacements.find((Mtx*)addr);
    if (it != replacements.end()) {
        if (!Shipyard::Fast3D::NearestRounding() || !Shipyard::Fast3D::QuantizeMatrix(it->second.mf, matrix)) {
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    float v = it->second.mf[i][j];
                    int as_int = (int)(v * 65536.0f);
                    matrix[i][j] = as_int * (1.0f / 65536.0f);
                }
            }
        }
        return;
    }
#ifndef GBI_FLOATS
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int32_t int_part = addr[i * 2 + j / 2];
            uint32_t frac_part = addr[8 + i * 2 + j / 2];
            matrix[i][j] = (int32_t)((int_part & 0xffff0000) | (frac_part >> 16)) / 65536.0f;
            matrix[i][j + 1] = (int32_t)((int_part << 16) | (frac_part & 0xffff)) / 65536.0f;
        }
    }
#else
    memcpy(matrix, addr, 16 * sizeof(float));
#endif
}

// Runs before GfxSpMatrix applies the command, on the stack depth it starts from.
void Interpreter::XboxSubMatrix(uint8_t parameters, const int32_t* addr) {
    const int8_t mtx_projection = get_attr(MTX_PROJECTION);
    const int8_t mtx_load = get_attr(MTX_LOAD);
    const int8_t mtx_push = get_attr(MTX_PUSH);
    for (unsigned k = 0; k < mXboxSubCount; ++k) {
        __declspec(align(16)) float matrix[4][4];
        XboxDecodeMatrix(*mXboxBatchMaps[k + 1], addr, matrix);
        XboxSubframe& s = mXboxSub[k];
        unsigned size = mRsp->modelview_matrix_stack_size;
        if (parameters & mtx_projection) {
            if (parameters & mtx_load) memcpy(s.P, matrix, sizeof(matrix));
            else MatrixMul(s.P, matrix, s.P);
        } else {
            if ((parameters & mtx_push) && size < 11) {
                ++size;
                if (!(parameters & mtx_load)) memcpy(s.modelview[size - 1], s.modelview[size - 2], sizeof(matrix));
            }
            if (parameters & mtx_load) {
                if (size == 0) ++size;
                memcpy(s.modelview[size - 1], matrix, sizeof(matrix));
            } else if (size > 0) {
                MatrixMul(s.modelview[size - 1], matrix, s.modelview[size - 1]);
            }
        }
        s.dirty = true;
    }
}

// Runs before GfxSpPopMatrix, mirroring its depth walk.
void Interpreter::XboxSubPopMatrix(uint32_t count) {
    unsigned size = mRsp->modelview_matrix_stack_size;
    while (count--) {
        if (size > 0) {
            if (size == 1)
                for (unsigned k = 0; k < mXboxSubCount; ++k) XboxSubProduct(k, size);
            --size;
            if (size > 0)
                for (unsigned k = 0; k < mXboxSubCount; ++k) mXboxSub[k].dirty = true;
        }
    }
}

// Each later presentation's clip positions and trivial-reject flags.
void Interpreter::XboxSubVertices(size_t n_vertices, size_t dest_index, const F3DVtx* vertices) {
    if (!vertices || !n_vertices) return;
    if (n_vertices > MAX_VERTICES || dest_index > MAX_VERTICES || n_vertices > MAX_VERTICES - dest_index ||
        !Shipyard::Fast3D::VertexSourceDisjoint(vertices, n_vertices * sizeof(F3DVtx), mRsp, sizeof(*mRsp))) {
        mXboxWalk->Taint(Shipyard::Fast3D::WalkVertexSource);
        return;
    }
    const bool nearest = Shipyard::Fast3D::NearestRounding();
    float aspectDenominator = 1.0f;
    Shipyard::Fast3D::AspectReciprocal aspect(1.0f);
    if (!mFbActive) {
        aspectDenominator = (float)mCurDimensions.width / (float)mCurDimensions.height;
        aspect = Shipyard::Fast3D::AspectReciprocal(aspectDenominator);
    }
    for (unsigned k = 0; k < mXboxSubCount; ++k) {
        XboxSubframe& s = mXboxSub[k];
        XboxSubProduct(k, mRsp->modelview_matrix_stack_size);
        __declspec(align(16)) float positions[MAX_VERTICES][4];
        const bool batch = nearest && Shipyard::Fast3D::TransformPositions(vertices, n_vertices, s.MP, positions);
        for (size_t i = 0; i < n_vertices; ++i) {
            const F3DVtx_t* v = &vertices[i].v;
            float x, y, z, w;
            if (batch) {
                x = positions[i][0]; y = positions[i][1]; z = positions[i][2]; w = positions[i][3];
            } else {
                x = v->ob[0] * s.MP[0][0] + v->ob[1] * s.MP[1][0] + v->ob[2] * s.MP[2][0] + s.MP[3][0];
                y = v->ob[0] * s.MP[0][1] + v->ob[1] * s.MP[1][1] + v->ob[2] * s.MP[2][1] + s.MP[3][1];
                z = v->ob[0] * s.MP[0][2] + v->ob[1] * s.MP[1][2] + v->ob[2] * s.MP[2][2] + s.MP[3][2];
                w = v->ob[0] * s.MP[0][3] + v->ob[1] * s.MP[1][3] + v->ob[2] * s.MP[2][3] + s.MP[3][3];
            }
            x = !mFbActive ? aspect.Divide(x * (4.0f / 3.0f), aspectDenominator) : AdjXForAspectRatio(x);
            uint8_t clip = 0;
            if (x < -w) clip |= 1;
            if (x > w) clip |= 2;
            if (y < -w) clip |= 4;
            if (y > w) clip |= 8;
            if (z > w) clip |= 32;
            XboxSubframe::Position& p = s.positions[dest_index + i];
            p.x = x; p.y = y; p.z = z; p.w = w; p.clip = clip;
        }
    }
}

// True when a later presentation keeps a triangle this one rejects.
bool Interpreter::XboxSubAccept(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {
    if (!mXboxSubCount || is_rect) return false;
    const uint32_t cull_both = get_attr(CULL_BOTH);
    const uint32_t cull_front = get_attr(CULL_FRONT);
    const uint32_t cull_back = get_attr(CULL_BACK);
    const uint32_t cull_type = mRsp->geometry_mode & cull_both;
    for (unsigned k = 0; k < mXboxSubCount; ++k) {
        const XboxSubframe::Position& p1 = mXboxSub[k].positions[vtx1_idx];
        const XboxSubframe::Position& p2 = mXboxSub[k].positions[vtx2_idx];
        const XboxSubframe::Position& p3 = mXboxSub[k].positions[vtx3_idx];
        if (p1.clip & p2.clip & p3.clip) continue;
        if (cull_type != 0) {
            const float x2 = p2.x / p2.w, y2 = p2.y / p2.w;
            const float dx1 = p1.x / p1.w - x2, dy1 = p1.y / p1.w - y2;
            const float dx2 = p3.x / p3.w - x2, dy2 = p3.y / p3.w - y2;
            float cross = dx1 * dy2 - dy1 * dx2;
            if ((p1.w < 0) ^ (p2.w < 0) ^ (p3.w < 0)) cross = -cross;
            if ((mRsp->extra_geometry_mode & G_EX_INVERT_CULLING) != 0) cross = -cross;
            if (cull_type == cull_front) {
                if (cross <= 0) continue;
            } else if (cull_type == cull_back) {
                if (cross >= 0) continue;
            } else if (cull_type == cull_both) {
                continue;
            }
        }
        return true;
    }
    return false;
}

// True when a later presentation sees part of a range this one culls.
bool Interpreter::XboxSubVisible(unsigned first, unsigned last) {
    for (unsigned k = 0; k < mXboxSubCount; ++k) {
        unsigned outside = 0x2fu;
        for (unsigned i = first; i <= last && outside; ++i) outside &= mXboxSub[k].positions[i].clip;
        if (!outside) return true;
    }
    return false;
}

void Interpreter::XboxSubBranchZ(unsigned vertex, uint32_t zval) {
    if (vertex >= MAX_VERTICES + 4 || (mRsp->extra_geometry_mode & G_EX_ALWAYS_EXECUTE_BRANCH) != 0) return;
    const bool taken = mRsp->loaded_vertices[vertex].z <= zval;
    for (unsigned k = 0; k < mXboxSubCount; ++k)
        if ((mXboxSub[k].positions[vertex].z <= zval) != taken)
            mXboxWalk->Taint(Shipyard::Fast3D::WalkBranchDiverged);
}

void Interpreter::XboxSubTileLerp(int tile, const float coords[8]) {
    if (tile < 0 || tile >= 8) return;
    XboxTileLerp& lerp = mXboxSubTiles[tile];
    lerp.main[0] = mRdp->texture_tile[tile].uls;
    lerp.main[1] = mRdp->texture_tile[tile].ult;
    lerp.main[2] = mRdp->texture_tile[tile].lrs;
    lerp.main[3] = mRdp->texture_tile[tile].lrt;
    const uint32_t width = GetTileSizeFromCoordinates(lerp.main[0], lerp.main[2]);
    const uint32_t height = GetTileSizeFromCoordinates(lerp.main[1], lerp.main[3]);
    for (unsigned k = 0; k < mXboxSubCount; ++k) {
        const float t = mXboxBatchT[k + 1];
        float* c = lerp.sub[k];
        for (int i = 0; i < 4; ++i) c[i] = coords[i] + t * (coords[i + 4] - coords[i]);
        // Shared row geometry holds while only the offsets move.
        if (GetTileSizeFromCoordinates(c[0], c[2]) != width || GetTileSizeFromCoordinates(c[1], c[3]) != height)
            mXboxWalk->Taint(Shipyard::Fast3D::WalkTileResized);
    }
    lerp.valid = true;
}

// Records, for each later presentation, the positions and lerped texture
// coordinates of the rows just packed; the rows themselves are kept once.
void Interpreter::XboxSubRows(const float* start, const float* end, LoadedVertex* const* pack, int count,
                              bool is_rect, const bool* usedTextures, uint32_t tm, const uint32_t* effective_tile,
                              const uint32_t* tex_width, const uint32_t* tex_height, bool halfTexel,
                              const GfxClipParameters& clip) {
    const size_t at = (size_t)(start - mBufVbo);
    const size_t floats = (size_t)(end - start);
    const size_t stride = floats / (size_t)count;
    unsigned offset[2] = { 0, 0 };
    bool lerped[2] = { false, false };
    unsigned next = 4;
    for (int t = 0; t < 2; ++t) {
        if (!usedTextures[t]) continue;
        offset[t] = next;
        const uint32_t tile = effective_tile[t];
        const XboxTileLerp& lerp = mXboxSubTiles[tile & 7];
        lerped[t] = tile < 8 && lerp.valid && mRdp->texture_tile[tile].uls == lerp.main[0] &&
                    mRdp->texture_tile[tile].ult == lerp.main[1] && mRdp->texture_tile[tile].lrs == lerp.main[2] &&
                    mRdp->texture_tile[tile].lrt == lerp.main[3];
        next += 2 + ((tm >> (2 * t)) & 1) + ((tm >> (2 * t + 1)) & 1);
    }
    const bool position = !is_rect;
    if (!position && !lerped[0] && !lerped[1]) return;
    mXboxWalk->Patch(at, (unsigned)count, (unsigned)stride, position, lerped[0] ? offset[0] : 0,
                     lerped[1] ? offset[1] : 0);
    const size_t per = (position ? 4 : 0) + (lerped[0] ? 2 : 0) + (lerped[1] ? 2 : 0);
    for (unsigned k = 0; k < mXboxSubCount; ++k) {
        float* out = mXboxWalk->Values(k, per * (size_t)count);
        Shipyard::Fast3D::CoordinateAxis axisS[2], axisT[2];
        for (int t = 0; t < 2; ++t) {
            if (!lerped[t]) continue;
            const uint32_t tile = effective_tile[t];
            axisS[t].Prepare(mRdp->texture_tile[tile].shifts, mXboxSubTiles[tile].sub[k][0], tex_width[t]);
            axisT[t].Prepare(mRdp->texture_tile[tile].shiftt, mXboxSubTiles[tile].sub[k][1], tex_height[t]);
        }
        for (int i = 0; i < count; ++i) {
            if (position) {
                const XboxSubframe::Position& p = mXboxSub[k].positions[pack[i] - mRsp->loaded_vertices];
                *out++ = p.x;
                *out++ = clip.invertY ? -p.y : p.y;
                *out++ = clip.z_is_from_0_to_1 ? (p.z + p.w) * 0.5f : p.z;
                *out++ = p.w;
            }
            for (int t = 0; t < 2; ++t) {
                if (!lerped[t]) continue;
                *out++ = axisS[t].Apply(pack[i]->u, halfTexel);
                *out++ = axisT[t].Apply(pack[i]->v, halfTexel);
            }
        }
    }
}

void Interpreter::XboxRecordDraw(bool indexed) {
    mXboxWalk->Draw(mBufVbo, mBufVboLen, mBufVboNumTris, indexed ? mXboxTriangles.indices : NULL,
                    mXboxTriangles.rows);
}
// SHIPYARD WALK REPLAY END

'''


def _once(text, old, new, count=1):
    if text.count(old) != count:
        raise ValueError('Walk replay anchor drift (%d): %s' % (text.count(old), old[:90]))
    return text.replace(old, new)


def header(text, *, anchor=HEADER_ANCHOR):
    """Interpreter members; the class must already see GfxRenderingAPI and MAX_VERTICES."""
    found = anchor.findall(text)
    if len(found) != 1:
        raise ValueError('Walk replay header anchor drift')
    return '#include "fast3d/walk_replay.h"\n' + text.replace(found[0], found[0] + MEMBERS)


def transform(text, *, run='void Interpreter::Run(Gfx* commands, const std::unordered_map<Mtx*, MtxF>& mtx_replacements) {'):
    """Install recording and replay in a projected interpreter.cpp whose emit
    loop packs rows through shipyardVertexOutput (vertex_stream.lower)."""
    text = '#include <exception>\n' + text
    ctor = text.index('Interpreter::Interpreter() :')
    body = text.index('{\n', ctor) + 1
    text = text[:body] + CONSTRUCT + text[body:]
    text = _once(text, '    mRapi = rapi;\n', '    mRapi = rapi;\n    mXboxLiveRapi = rapi;\n')
    for old in METADATA:
        text = _once(text, old, old.replace('mRapi,', 'mXboxLiveRapi,'))

    # Matrices: later presentations' stacks follow every command first.
    text = _once(text, 'void Interpreter::GfxSpMatrix(uint8_t parameters, const int32_t* addr) {\n',
                 'void Interpreter::GfxSpMatrix(uint8_t parameters, const int32_t* addr) {\n'
                 '    if (mXboxSubCount) XboxSubMatrix(parameters, addr);\n')
    text = _once(text, 'void Interpreter::GfxSpPopMatrix(uint32_t count) {\n',
                 'void Interpreter::GfxSpPopMatrix(uint32_t count) {\n'
                 '    if (mXboxSubCount) XboxSubPopMatrix(count);\n')
    text = _once(text, 'void Interpreter::SpReset() {\n',
                 'void Interpreter::SpReset() {\n'
                 '    for (unsigned k = 0; k < mXboxSubCount; ++k) XboxSubProduct(k, mRsp->modelview_matrix_stack_size);\n')
    vertex = 'void Interpreter::GfxSpVertex(size_t n_vertices, size_t dest_index, const F3DVtx* vertices) {\n'
    start = text.index(vertex)
    product = text.index('    EnsureMatrixProduct();\n', start)
    if text.count(vertex) != 1 or product > text.index('\n}\n', start):
        raise ValueError('Walk replay vertex anchor drift')
    product += len('    EnsureMatrixProduct();\n')
    text = text[:product] + '    if (mXboxSubCount) XboxSubVertices(n_vertices, dest_index, vertices);\n' + text[product:]

    # Rejections: kept when any later presentation keeps the triangle.
    start = text.index(TRIANGLE)
    end = text.index(TRIANGLE_END, start)
    rejections = re.compile(r'^(\s+)return;$', re.M)
    if text.count(TRIANGLE) != 1 or len(rejections.findall(text[start:end])) != 4:
        raise ValueError('Walk replay triangle rejection drift')
    text = text[:start] + rejections.sub(
        r'\1if (!XboxSubAccept(vtx1_idx, vtx2_idx, vtx3_idx, is_rect)) return;', text[start:end]) + text[end:]
    text = _once(text, CULL_DL, '    if (gfx->XboxSubVisible(first, last)) return false;\n' + CULL_DL)
    text = _once(text, BRANCH_Z, BRANCH_Z + '    if (gfx->mXboxSubCount) gfx->XboxSubBranchZ(vbidx, zval);\n')

    # Tile coordinates and segments that differ per presentation.
    text = _once(text, LERP, LERP + '    if (gfx->mXboxSubCount) gfx->XboxSubTileLerp(tile, coords);\n')
    text = _once(text, TILE_INTERP,
                 '    if (gfx->mXboxSubCount) gfx->mXboxWalk->Taint(Shipyard::Fast3D::WalkSegmentInterpolation);\n'
                 + TILE_INTERP)
    text = _once(text, SEGMENT_INTERP, SEGMENT_INTERP +
                 '            if (mXboxSubCount) mXboxWalk->Taint(Shipyard::Fast3D::WalkSegmentInterpolation);\n', 2)

    # Rows and draws.
    text = _once(text, ROW_START, ROW_START + '    float* const xboxRowStart = shipyardVertexOutput;\n')
    text = _once(text, ROW_END,
                 '    if (mXboxSubCount)\n'
                 '        XboxSubRows(xboxRowStart, shipyardVertexOutput, xboxPack, xboxPackCount, is_rect, usedTextures, tm,\n'
                 '                    effective_tile, tex_width, tex_height, shipyardHalfTexel, clip_parameters);\n'
                 + ROW_END)
    text = _once(text, FLUSH_DRAW, '        const bool xboxIndexed = mXboxTriangles.Indexed(mBufVboNumTris);\n'
                 + FLUSH_DRAW.replace('mXboxTriangles.Indexed(mBufVboNumTris)', 'xboxIndexed')
                 + '        if (mXboxSubCount) XboxRecordDraw(xboxIndexed);\n')

    # Run: the batch's first presentation records, the rest replay.
    text = _once(text, run, HELPERS + run)
    start = text.index(run)
    debugging = text.index(RUN_DEBUGGING, start) + len(RUN_DEBUGGING)
    text = text[:debugging] + (
        '    if (!XboxWalkReplay(commands, xboxDebugging)) {\n'
        '    XboxRecordingScope xboxRecording = { XboxBeginRecording(commands, xboxDebugging) ? this : NULL };\n'
    ) + text[debugging:]
    end = text.index(RUN_END, start)
    return text[:end] + '    Flush();\n    }\n    mGfxFrameBuffer = 0;\n' + text[end + len(RUN_END):]
