"""Material, dispatch and framebuffer-discard projections, executed against the originals on the host."""

import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from tree_sitter import Language, Parser
import tree_sitter_cpp
from xbox360_platform import (fast3d_dispatch, fast3d_framebuffer_discard, fast3d_material_replay, fast3d_opcode,
                              fast3d_pipeline_stamps, texture_state)
from xbox360_platform.prism import walk

HERE = Path(__file__).resolve().parent


def host_run(test, source, name):
    """Compile `source` with the newest host x64 MSVC and run it; returns stdout."""
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    with tempfile.TemporaryDirectory(prefix='shipyard-' + name + '-') as folder:
        out = Path(folder)
        (out / 'contract.cpp').write_text(source, encoding='utf-8')
        (out / 'build.cmd').write_text(
            '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\nif errorlevel 1 exit /b 1\n'
            'cl /nologo /std:c++17 /O2 /EHsc /MT /W3 /I"' + str(ROOT / 'sources') +
            '" contract.cpp /Fecontract.exe\nexit /b %errorlevel%\n')
        built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(out / 'build.cmd')], cwd=out,
                               capture_output=True, text=True)
        test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(out / 'contract.exe')], cwd=out, capture_output=True, text=True, timeout=120)
        test.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        return ran.stdout


# Upstream Fast3D's dispatch structures, with small overlapping tables.
DISPATCH = '''class UcodeHandler {
  public:
    inline constexpr UcodeHandler(
        std::initializer_list<std::pair<int8_t, std::pair<const char*, GfxOpcodeHandlerFunc>>> initializer) {
        std::fill(std::begin(mHandlers), std::end(mHandlers),
                  std::pair<const char*, GfxOpcodeHandlerFunc>(nullptr, nullptr));

        for (const auto& [opcode, handler] : initializer) {
            mHandlers[static_cast<uint8_t>(opcode)] = handler;
        }
    }

    inline bool contains(int8_t opcode) const {
        return mHandlers[static_cast<uint8_t>(opcode)].first != nullptr;
    }

    inline std::pair<const char*, GfxOpcodeHandlerFunc> at(int8_t opcode) const {
        return mHandlers[static_cast<uint8_t>(opcode)];
    }

  private:
    std::pair<const char*, GfxOpcodeHandlerFunc> mHandlers[std::numeric_limits<uint8_t>::max() + 1];
};

static constexpr UcodeHandler rdpHandlers = {
    { RDP_A, { "RDP_A", h0 } },
    { SHARED, { "RDP_SHARED", h1 } },
};

static constexpr UcodeHandler otrHandlers = {
    { OTR_G_VTX_OTR_FILEPATH, { "OTR_FILE", h2 } },
    { SHARED, { "OTR_SHARED", h3 } },
};

static constexpr UcodeHandler f3dex2Handlers = {
    { F3_A, { "F3DEX2_A", h4 } },
    { RDP_A, { "F3DEX2_RDP", h5 } },
    { SHARED, { "F3DEX2_SHARED", h5 } },
};

static constexpr UcodeHandler f3dexHandlers = {
    { F3_B, { "F3DEX_B", h6 } },
};

static constexpr UcodeHandler f3dHandlers = {
    { F3_B, { "F3D_B", h7 } },
    { F3_A, { "F3D_A", h8 } },
};

static constexpr UcodeHandler s2dexHandlers = {
    { S2_A, { "S2_A", h9 } },
};

static constexpr std::array ucode_handlers = {
    &f3dHandlers,    // ucode_f3db
    &f3dHandlers,    // ucode_f3d
    &f3dexHandlers,  // ucode_f3dex
    &f3dexHandlers,  // ucode_f3dexb
    &f3dex2Handlers, // ucode_f3dex2
    &s2dexHandlers,  // ucode_s2dex
};

const char* GfxGetOpcodeName(int8_t opcode) {
    if (otrHandlers.contains(opcode)) {
        return otrHandlers.at(opcode).first;
    }

    if (rdpHandlers.contains(opcode)) {
        return rdpHandlers.at(opcode).first;
    }

    if (ucode_handler_index < ucode_handlers.size()) {
        if (ucode_handlers[ucode_handler_index]->contains(opcode)) {
            return ucode_handlers[ucode_handler_index]->at(opcode).first;
        } else {
            SPDLOG_CRITICAL("Unhandled OP code: 0x{:X}, for loaded ucode: {}", (uint8_t)opcode,
                            (uint32_t)ucode_handler_index);
        }
    } else {
        SPDLOG_CRITICAL("Unhandled OP code: 0x{:X}, invalid ucode: {}", (uint8_t)opcode, (uint32_t)ucode_handler_index);
    }

    return nullptr;
}

static void gfx_step() {
    auto& cmd = g_exec_stack.currCmd();
    int8_t opcode = (int8_t)(cmd->words.w0 >> 24);

    if (opcode == F3DEX2_G_LOAD_UCODE) {
        gfx_set_ucode_handler((UcodeHandlers)(cmd->words.w0 & 0xFFFFFF));
        ++cmd;
        return;
    }

    if (otrHandlers.contains(opcode)) {
        // OTR filepath handlers expect w1 to be a valid string pointer.
        if (opcode == OTR_G_VTX_OTR_FILEPATH || opcode == OTR_G_SETTIMG_OTR_FILEPATH ||
            opcode == OTR_G_DL_OTR_FILEPATH || opcode == OTR_G_PUSHCD || opcode == OTR_G_MTX_OTR_FILEPATH) {
            uintptr_t w1 = (uintptr_t)cmd->words.w1;
            if (w1 < 0x10000) {
                ++g_exec_stack.currCmd();
                return;
            }
        }
        if (otrHandlers.at(opcode).second(&cmd)) {
            return;
        }
    } else if (rdpHandlers.contains(opcode)) {
        if (rdpHandlers.at(opcode).second(&cmd)) {
            return;
        }
    } else if (ucode_handler_index < ucode_handlers.size()) {
        if (ucode_handlers[ucode_handler_index]->contains(opcode)) {
            if (ucode_handlers[ucode_handler_index]->at(opcode).second(&cmd)) {
                return;
            }
        } else {
            SPDLOG_CRITICAL("Unhandled OP code: 0x{:X}, for loaded ucode: {}", (uint8_t)opcode,
                            (uint32_t)ucode_handler_index);
        }
    } else {
        SPDLOG_CRITICAL("Unhandled OP code: 0x{:X}, invalid ucode: {}", (uint8_t)opcode, (uint32_t)ucode_handler_index);
    }

    ++cmd;
}
'''


def stamp(text, calls):
    for call in calls:
        if text.count('if (' + call + '(&cmd))') != 1:
            raise AssertionError('dispatch site not exactly once: ' + call)
        text = text.replace('if (' + call + '(&cmd))', 'if (XboxDispatchStamped(' + call + ', &cmd, opcode))')
    return text


class ComposedDispatchTests(unittest.TestCase):
    def test_composed_dispatch_matches_sequential_for_every_opcode(self):
        parser = Parser(Language(tree_sitter_cpp.language()))
        tables = fast3d_opcode.FAST3D_TABLES

        def lower(text, composed):
            return fast3d_opcode.lower(text, parser, walk, tables, composed_dispatch=composed)[0]

        sequential = lower(DISPATCH, False)
        # Majora: C-array ucode table, lowered composed, then stamped.
        c_array = DISPATCH.replace('static constexpr std::array ucode_handlers = {',
                                   'static const UcodeHandler* const ucode_handlers[] = {')
        c_array = c_array.replace('ucode_handlers.size()', '(sizeof(ucode_handlers)/sizeof(ucode_handlers[0]))')
        composed = stamp(lower(DISPATCH, True), fast3d_pipeline_stamps.COMPOSED_CALLS)
        composed_array = stamp(lower(c_array, True), fast3d_pipeline_stamps.COMPOSED_CALLS)
        # Smash: stamped sequential dispatch, then composed.
        stamp_first = lower(stamp(DISPATCH, fast3d_pipeline_stamps.SEQUENTIAL_CALLS), True)
        self.assertIn('if (XboxDispatchStamped(handler.second, &cmd, opcode)) return;', stamp_first)
        self.assertIn('&& otrHandlers.contains(opcode)) {', composed)
        driver = (HERE / 'fast3d_dispatch_contract.cpp').read_text()
        for marker, text in (('// SEQUENTIAL', sequential), ('// COMPOSED C ARRAY', composed_array),
                             ('// COMPOSED', composed), ('// STAMPED THEN COMPOSED', stamp_first)):
            driver = driver.replace(marker + '\n', text + '\n', 1)
        self.assertIn('PASS composed dispatch', host_run(self, driver, 'dispatch'))

    def test_changed_dispatch_chain_is_rejected(self):
        parser = Parser(Language(tree_sitter_cpp.language()))
        changed = DISPATCH.replace('    } else if (rdpHandlers.contains(opcode)) {',
                                   '    } else if (rdpHandlers.contains(opcode) && opcode != 0) {')
        with self.assertRaises(ValueError):
            fast3d_opcode.lower(changed, parser, walk, fast3d_opcode.FAST3D_TABLES, composed_dispatch=True)


RUN = '''void GfxExecStack::start(F3DGfx* dlist) {
    gfx_path.clear();
}
void GfxExecStack::branch(F3DGfx* caller) {
    gfx_path.push_back(caller);
}
void GfxExecStack::call(F3DGfx* caller, F3DGfx* callee) {
    gfx_path.push_back(caller);
}
void Interpreter::Run(Gfx* commands) {
    auto dbg = mGfxDebugger;
    g_exec_stack.start((F3DGfx*)commands);
    sXboxTriangleRunEnabled = !dbg->IsDebugging();
    while (!g_exec_stack.cmd_stack.empty()) {
        if (dbg->IsDebugging()) {
            g_exec_stack.gfx_path.push_back(cmd);
        }
        gfx_step();
    }
}
'''


class DebuggerSnapshotTests(unittest.TestCase):
    def test_debugger_is_read_once_and_paths_follow_it(self):
        text = fast3d_dispatch.debugger_snapshot(RUN)
        self.assertEqual(text.count('dbg->IsDebugging()'), 1)
        self.assertEqual(text.count('    if (sXboxTrackGfxPath) gfx_path.push_back(caller);'), 2)
        self.assertIn('sXboxTriangleRunEnabled = !xboxDebugging;', text)
        self.assertLess(text.index('XboxGfxPathScope gfxPathScope(xboxDebugging);'),
                        text.index('    g_exec_stack.start((F3DGfx*)commands);'))
        self.assertLess(text.index('struct XboxGfxPathScope'), text.index('void GfxExecStack::start('))
        with self.assertRaises(ValueError):
            fast3d_dispatch.debugger_snapshot(text)


DISCARD = '''void Interpreter::Run(Gfx* commands) {
    mRapi->StartDrawToFramebuffer(mRendersToFb ? mGameFb : 0, 1);
    (mXboxPipelineKey.valid=false, mRapi->ClearFramebuffer(false, true));
    walk();
    if (mRendersToFb) {
        ComposeFinalFrame();
    }
}
void Interpreter::EndFrame() {
    mRapi->EndFrame();
}
void Interpreter::GfxSpTri1(uint8_t a, uint8_t b, uint8_t c, bool rect) {
    draw();
}
void Interpreter::SetFrameBuffer(int fb) {
    switchTarget();
}
void Interpreter::CopyFrameBuffer(int dst, int src) {
    copy();
}
void Interpreter::GfxDpFillRectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    uint32_t mode = (mRdp->other_mode_h & (3U << G_MDSFT_CYCLETYPE));
    for (int i = MAX_VERTICES; i < MAX_VERTICES + 4; i++) {
        mRsp->loaded_vertices[i].color = mRdp->fill_color;
    }
}
'''


class FramebufferDiscardTests(unittest.TestCase):
    def test_game_fill_final_depth_and_presentation_wiring(self):
        text = fast3d_framebuffer_discard.game_fill(
            DISCARD, clear='    (mXboxPipelineKey.valid=false, mRapi->ClearFramebuffer(false, true));',
            eligible='mRendersToFb && mMsaaLevel == 1', left='L', right='R', top='T', bottom='B',
            viewport='        XYWidthHeight xboxViewport = {};')
        text = fast3d_framebuffer_discard.final_depth(text, anchor='        ComposeFinalFrame();', flag=False)
        text = fast3d_framebuffer_discard.presentation(text, depth='mRendersToFb', color='mRendersToFb')
        run = text[text.index('void Interpreter::Run('):text.index('void Interpreter::EndFrame()')]
        self.assertIn('    mXboxCanDiscardGameColor = mRendersToFb && mMsaaLevel == 1;\n'
                      '    if (mXboxCanDiscardGameColor) {\n'
                      '        mXboxPipelineKey.valid = false;\n'
                      '        Fast::ShipyardXenos::BeginGameFramebufferDepthClear();\n'
                      '    } else {\n'
                      '        (mXboxPipelineKey.valid=false, mRapi->ClearFramebuffer(false, true));\n'
                      '    }\n', run)
        self.assertIn('        Fast::ShipyardXenos::DiscardFramebufferDepth(mGameFb);\n        ComposeFinalFrame();',
                      run)
        for name in ('GfxSpTri1', 'SetFrameBuffer', 'CopyFrameBuffer'):
            method = text[text.index('void Interpreter::' + name + '('):]
            self.assertTrue(method.split('\n', 2)[1] == '    mXboxCanDiscardGameColor = false;', name)
        fill = text[text.index('void Interpreter::GfxDpFillRectangle('):]
        self.assertLess(fill.index('Fast::ShipyardXenos::TryDiscardGameFramebufferColor();'),
                        fill.index('    for (int i = MAX_VERTICES;'))
        self.assertIn('FullFillCovers(mCurDimensions.width, mCurDimensions.height,\n                L, R,', fill)
        self.assertIn('DiscardPresentationColorOnEnd', text[text.index('void Interpreter::EndFrame()'):])
        with self.assertRaises(ValueError):
            fast3d_framebuffer_discard.final_depth(text, anchor='        ComposeFinalFrame();', flag=False)

    def test_flagged_final_depth_before_last_anchor_line(self):
        source = DISCARD.replace('        ComposeFinalFrame();', '        mRapi->StartDrawToFramebuffer(0, 1);')
        text = fast3d_framebuffer_discard.final_depth(
            source, anchor='    if (mRendersToFb) {\n        mRapi->StartDrawToFramebuffer(0, 1);')
        self.assertIn('    if (mRendersToFb) {\n'
                      '        if (mXboxDiscardGameDepth) Fast::ShipyardXenos::DiscardFramebufferDepth(mGameFb);\n'
                      '        mXboxDiscardGameDepth = false;\n'
                      '        mRapi->StartDrawToFramebuffer(0, 1);', text)

    def test_full_fill_certificate(self):
        self.assertIn('PASS full fill certificate',
                      host_run(self, (HERE / 'full_fill_certificate.cpp').read_text(), 'full-fill'))


MATERIAL = '''struct Interpreter {
    RDP storage;
    RDP* mRdp;
    std::vector<int> mShaderStack;
    std::map<ColorCombinerKey, ColorCombiner> mColorCombinerPool;
    TextureCache mTextureCache;
    Backend backend;
    Backend* mRapi;
    unsigned uploads, creations;
    Interpreter();
    ~Interpreter();
    ColorCombiner* LookupOrCreateColorCombiner(const ColorCombinerKey& key);
    void Decode(Decoded& out);
    bool TextureCacheLookup(const TextureCacheKey& key, const TextureCacheNode** n);
    void Delete(const TextureCacheKey& key);
    void TextureCacheClear();
    void ClearCombiners();
    void Reinit();
};
Interpreter::Interpreter() : storage(), mRdp(&storage), mRapi(&backend), uploads(0), creations(0) {
}
Interpreter::~Interpreter() {
}
ColorCombiner* Interpreter::LookupOrCreateColorCombiner(const ColorCombinerKey& key) {
    std::map<ColorCombinerKey, ColorCombiner>::iterator it = mColorCombinerPool.find(key);
    if (it == mColorCombinerPool.end()) {
        ColorCombiner value = { key, ++creations };
        it = mColorCombinerPool.insert(std::make_pair(key, value)).first;
    }
    return &it->second;
}
void Interpreter::Decode(Decoded& out) {
    uint64_t cc_id = mRdp->combine_mode;
    uint64_t cc_options = 0;
    bool use_alpha = (mRdp->other_mode_l & 3) == 1;
    uint8_t blend_src = mRdp->other_mode_l >> 30;
    bool use_fog = blend_src == 2;
    bool texture_edge = (mRdp->other_mode_l & 0x100) != 0;
    bool use_grayscale = mRdp->grayscale;
    if (texture_edge) {
        use_alpha = true;
    }
    if (use_alpha) cc_options |= 1;
    if (use_fog) cc_options |= 2;
    if (texture_edge) cc_options |= 4;
    if (mRdp->other_mode_h & 1) cc_options |= 8;
    if (use_grayscale) cc_options |= 16;
    if (!mShaderStack.empty()) {
        cc_options |= (uint64_t)mShaderStack.back() << 40;
    } else {
        cc_options |= (uint64_t)0xff << 40;
    }
    if (mRdp->loaded_texture[0].masked) cc_options |= 1 << 8;
    if (mRdp->loaded_texture[1].masked) cc_options |= 1 << 9;
    if (mRdp->loaded_texture[0].blended) cc_options |= 1 << 10;
    if (mRdp->loaded_texture[1].blended) cc_options |= 1 << 11;
    ColorCombinerKey key = {};
    key.combine_mode = mRdp->combine_mode;
    key.options = cc_options;
    ColorCombiner* comb = LookupOrCreateColorCombiner(key);
    out.key = comb->key;
    out.serial = comb->serial;
    out.alpha = use_alpha;
    out.fog = use_fog;
    out.grayscale = use_grayscale;
    out.tail = cc_id ^ comb->key.options;
}
bool Interpreter::TextureCacheLookup(const TextureCacheKey& key, const TextureCacheNode** n) {
    TextureCacheMap::iterator it = mTextureCache.map.find(key);
    if (it != mTextureCache.map.end()) {
        mTextureCache.lru.splice(mTextureCache.lru.end(), mTextureCache.lru, it->second.lru_location);
        *n = &*it;
        return true;
    }
    if (mTextureCache.map.size() >= 3) {
        TextureCacheMap::iterator victim = mTextureCache.lru.front().it;
        mTextureCache.lru.pop_front();
        mTextureCache.map.erase(victim);
    }
    it = mTextureCache.map.insert(std::make_pair(key, TextureCacheValue())).first;
    TextureCacheMapIter entry = { it };
    it->second.lru_location = mTextureCache.lru.insert(mTextureCache.lru.end(), entry);
    it->second.id = ++uploads;
    *n = &*it;
    return false;
}
void Interpreter::Delete(const TextureCacheKey& key) {
    TextureCacheMap::iterator it = mTextureCache.map.find(key);
    if (it != mTextureCache.map.end()) {
        mTextureCache.lru.erase(it->second.lru_location);
        mTextureCache.map.erase(it);
    }
}
void Interpreter::TextureCacheClear() {
    mTextureCache.lru.clear();
    mTextureCache.map.clear();
}
void Interpreter::ClearCombiners() {
    mColorCombinerPool.clear();
}
void Interpreter::Reinit() {
    mRapi->Init();
}
'''


def material_block(text):
    method = text[text.index('void Interpreter::Decode('):]
    first = method.index(fast3d_material_replay.MODE_START)
    last = method.index(fast3d_material_replay.MODE_END) + len(fast3d_material_replay.MODE_END)
    return hashlib.sha256(method[first:last].encode()).hexdigest()


class MaterialReplayTests(unittest.TestCase):
    def install(self, text, **changes):
        options = dict(function='void Interpreter::Decode(', sha256=material_block(text),
                       values=('use_alpha', 'use_fog', 'use_grayscale'))
        options.update(changes)
        return fast3d_material_replay.install(text, **options)

    def test_memos_match_original_through_clears_and_owner_reuse(self):
        projected = self.install(MATERIAL)
        self.assertTrue(projected.startswith('#include "fast3d/material_memo.h"\n'))
        projected = projected.split('\n', 1)[1].replace(
            '        // SHIPYARD MODE MISS BEGIN\n', '        // SHIPYARD MODE MISS BEGIN\n        ++sMisses;\n')
        self.assertEqual(projected.count('++Fast::sXboxTextureEpoch;'), 4)  # Init, two erases, clear
        self.assertEqual(projected.count('++Fast::sXboxCombinerEpoch;'), 2)  # Init, pool clear
        driver = (HERE / 'fast3d_material_replay_contract.cpp').read_text()
        driver = driver.replace('// REFERENCE\n', MATERIAL).replace('// PROJECTED\n', projected)
        self.assertIn('PASS material replay', host_run(self, driver, 'material-replay'))

    def test_reaudit_and_escaping_locals_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Re-audit'):
            self.install(MATERIAL, sha256='0' * 64)
        escaped = MATERIAL.replace('    out.tail = cc_id ^ comb->key.options;', '    out.tail = cc_id ^ blend_src;')
        with self.assertRaisesRegex(ValueError, 'escapes the memo: blend_src'):
            self.install(escaped)
        midline = MATERIAL.replace('    mColorCombinerPool.clear();', '    if (1) mColorCombinerPool.clear();')
        with self.assertRaisesRegex(ValueError, 'invalidation inventory'):
            self.install(midline)

    def test_hoisted_lines_run_on_hits(self):
        marked = MATERIAL.replace('    ColorCombinerKey key = {};', '    trace.Mark(1);\n    ColorCombinerKey key = {};')
        text = self.install(marked, hoist=('    trace.Mark(1);',))
        self.assertLess(text.index('    trace.Mark(1);'), text.index('const XboxModeKey xboxModeKey'))
        self.assertEqual(text.count('trace.Mark(1);'), 1)


TILES = '''void Interpreter::GfxDpSetTile(uint8_t fmt, uint32_t siz, uint32_t line, uint32_t tmem, uint8_t tile,
                               uint32_t palette, uint32_t cmt, uint32_t maskt, uint32_t shiftt, uint32_t cms,
                               uint32_t masks, uint32_t shifts) {
    if (cms == G_TX_WRAP && masks == G_TX_NOMASK) {
        cms = G_TX_CLAMP;
    }
    if (cmt == G_TX_WRAP && maskt == G_TX_NOMASK) {
        cmt = G_TX_CLAMP;
    }

    mRdp->texture_tile[tile].palette = palette;
    mRdp->texture_tile[tile].fmt = fmt;
    mRdp->texture_tile[tile].siz = siz;
    mRdp->texture_tile[tile].cms = cms;
    mRdp->texture_tile[tile].cmt = cmt;
    mRdp->texture_tile[tile].masks = masks;
    mRdp->texture_tile[tile].maskt = maskt;
    mRdp->texture_tile[tile].shifts = shifts;
    mRdp->texture_tile[tile].shiftt = shiftt;
    mRdp->texture_tile[tile].line_size_bytes = line * 8;

    mRdp->texture_tile[tile].tmem = tmem;

    mRdp->texture_tile[tile].tmem_index =
        tmem != 0;

    mRdp->textures_changed[0] = true;
    mRdp->textures_changed[1] = true;
}
void Interpreter::GfxDpSetTileSize(uint8_t tile, uint16_t uls, uint16_t ult, uint16_t lrs, uint16_t lrt) {
    mRdp->texture_tile[tile].uls = uls;
    mRdp->texture_tile[tile].ult = ult;
    mRdp->texture_tile[tile].lrs = lrs;
    mRdp->texture_tile[tile].lrt = lrt;
    mRdp->textures_changed[0] = true;
    mRdp->textures_changed[1] = true;
}
'''

TILE_DRIVER = r'''#include "fast3d/texel_decode.h"
#include "fast3d/texture_ci8.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#define G_TX_WRAP 0
#define G_TX_CLAMP 2
#define G_TX_NOMASK 0
#define SCALE_5_8(VAL_) (((VAL_) * 0xFF) / 0x1F)
struct Tile {
    uint8_t fmt, siz, cms, cmt, masks, maskt, shifts, shiftt;
    float uls, ult, lrs, lrt;
    uint16_t tmem;
    uint32_t line_size_bytes;
    uint8_t palette, tmem_index;
};
struct RDP { Tile texture_tile[8]; bool textures_changed[2]; };
#define CLASS(name) namespace name { struct Interpreter { RDP storage; RDP* mRdp; bool retain; \
    Interpreter() : storage(), mRdp(&storage), retain(false) {} \
    void GfxDpSetTile(uint8_t, uint32_t, uint32_t, uint32_t, uint8_t, uint32_t, uint32_t, uint32_t, uint32_t, \
                      uint32_t, uint32_t, uint32_t); \
    void GfxDpSetTileSize(uint8_t, uint16_t, uint16_t, uint16_t, uint16_t); }; }
CLASS(Original)
CLASS(Projected)
namespace Original {
// ORIGINAL
}
namespace Projected {
// PROJECTED
}
static uint32_t seed = 0x6272f73u;
static uint32_t next() { seed = seed * 1664525u + 1013904223u; return seed >> 8; }
int main() {
    // Every RGBA5551 word expands exactly as the importers' SCALE_5_8 bytes.
    for (unsigned c = 0; c < 65536; ++c) {
        unsigned char out[4];
        Shipyard::Fast3D::StoreRgba(out, Shipyard::Fast3D::Rgba5551((uint16_t)c));
        const unsigned char expected[4] = { (unsigned char)SCALE_5_8(c >> 11), (unsigned char)SCALE_5_8((c >> 6) & 0x1f),
            (unsigned char)SCALE_5_8((c >> 1) & 0x1f), (unsigned char)((c & 1) ? 255 : 0) };
        if (std::memcmp(out, expected, 4)) { std::printf("FAIL texel %u\n", c); return 1; }
    }
    Original::Interpreter eager;
    Projected::Interpreter trial;
    unsigned retained = 0;
    for (unsigned i = 0; i < 40000; ++i) {
        const uint8_t tile = next() % 8;
        trial.retain = next() % 2 == 0;
        const Tile before = eager.storage.texture_tile[tile];
        const bool dirty[2] = { eager.storage.textures_changed[0], eager.storage.textures_changed[1] };
        const unsigned kind = next() % 4;
        if (kind == 0) {
            unsigned a[11];
            for (unsigned k = 0; k < 11; ++k) a[k] = next() % 3;
            eager.GfxDpSetTile(a[0], a[1], a[2], a[3] * 256, tile, a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
            trial.GfxDpSetTile(a[0], a[1], a[2], a[3] * 256, tile, a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
        } else if (kind == 1) {
            const Tile& t = before;
            eager.GfxDpSetTile(t.fmt, t.siz, t.line_size_bytes / 8, t.tmem, tile, t.palette, t.cmt, t.maskt, t.shiftt,
                               t.cms, t.masks, t.shifts);
            trial.GfxDpSetTile(t.fmt, t.siz, t.line_size_bytes / 8, t.tmem, tile, t.palette, t.cmt, t.maskt, t.shiftt,
                               t.cms, t.masks, t.shifts);
        } else if (kind == 2) {
            const uint16_t v = next() % 2 ? (uint16_t)before.uls : (uint16_t)(next() % 3);
            eager.GfxDpSetTileSize(tile, v, (uint16_t)before.ult, (uint16_t)before.lrs, (uint16_t)before.lrt);
            trial.GfxDpSetTileSize(tile, v, (uint16_t)before.ult, (uint16_t)before.lrs, (uint16_t)before.lrt);
        } else {
            eager.storage.textures_changed[0] = trial.storage.textures_changed[0] = false;
            eager.storage.textures_changed[1] = trial.storage.textures_changed[1] = next() % 2 == 0;
            continue;
        }
        if (std::memcmp(eager.storage.texture_tile, trial.storage.texture_tile, sizeof(eager.storage.texture_tile))) {
            std::printf("FAIL tile fields %u\n", i); return 2;
        }
        const bool same = std::memcmp(&before, &eager.storage.texture_tile[tile], sizeof(Tile)) == 0;
        for (unsigned s = 0; s < 2; ++s) {
            const bool kept = trial.storage.textures_changed[s] == dirty[s];
            // Without the certificate, or after a real change, dirtiness is the original's.
            if ((!trial.retain || !same) && trial.storage.textures_changed[s] != eager.storage.textures_changed[s]) {
                std::printf("FAIL dirty %u\n", i); return 3;
            }
            if (trial.retain && same && !kept) { std::printf("FAIL retained %u\n", i); return 4; }
            retained += trial.retain && same && !dirty[s];
        }
        eager.storage.textures_changed[0] = trial.storage.textures_changed[0];
        eager.storage.textures_changed[1] = trial.storage.textures_changed[1];
    }
    if (retained < 1000) { std::printf("FAIL only %u retained\n", retained); return 5; }
    std::printf("PASS tile retention and texel decode (%u retained imports)\n", retained);
    return 0;
}
'''


class TextureStateTests(unittest.TestCase):
    def test_tile_retention_and_texel_table(self):
        projected = texture_state.exact_tile_updates(TILES, enabled_expr='true', retain_expr='retain')
        self.assertEqual(projected.count('previous.masks == static_cast<decltype(previous.masks)>(masks)'), 1)
        self.assertNotIn('xboxTileCapture', projected)
        driver = TILE_DRIVER.replace('// ORIGINAL\n', TILES).replace('// PROJECTED\n', projected)
        self.assertIn('PASS tile retention', host_run(self, driver, 'tile-retention'))

    def test_retained_updates_use_the_import_certificate(self):
        text = texture_state.retained_tile_updates(TILES)
        self.assertEqual(text.count('!xboxSameTileState || !XboxCanRetainTextureImports(*this)) {'), 2)
        self.assertLess(text.index('static __declspec(noinline) bool XboxCanRetainTextureImports('),
                        text.index('void Interpreter::GfxDpSetTile('))
        with self.assertRaises(ValueError):
            texture_state.exact_tile_updates(TILES.replace('    mRdp->texture_tile[tile].maskt = maskt;\n', ''),
                                             enabled_expr='true')

    def test_texel_decode_inventory(self):
        block = ('            uint8_t a = col16 & 1;\n            uint8_t r = col16 >> 11;\n'
                 '            uint8_t g = (col16 >> 6) & 0x1f;\n            uint8_t b = (col16 >> 1) & 0x1f;\n'
                 '            mTexUploadBuffer[4 * i + 0] = SCALE_5_8(r);\n'
                 '            mTexUploadBuffer[4 * i + 1] = SCALE_5_8(g);\n'
                 '            mTexUploadBuffer[4 * i + 2] = SCALE_5_8(b);\n'
                 '            mTexUploadBuffer[4 * i + 3] = a ? 255 : 0;\n')
        text = texture_state.texel_decode(block + '            i++;\n' + block, expected=2)
        self.assertEqual(text.count('Shipyard::Fast3D::StoreRgba(&mTexUploadBuffer[4 * i], '
                                    'Shipyard::Fast3D::Rgba5551(col16));'), 2)
        with self.assertRaises(ValueError):
            texture_state.texel_decode(block, expected=2)

    def test_ci8_rows_keep_their_bytes(self):
        source = (HERE / 'texture_ci8.cpp').read_text().replace('int main() {', 'int main() {\n', 1)
        source = source.replace('    return 0;\n}', '    printf("PASS ci8\\n");\n    return 0;\n}')
        self.assertIn('PASS ci8', host_run(self, '#include <stdio.h>\n#undef NDEBUG\n' + source, 'ci8'))


if __name__ == '__main__':
    unittest.main()
