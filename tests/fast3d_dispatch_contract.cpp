// Executes the original sequential Fast3D dispatch against composed dispatch
// (both table forms, both pipeline-stamp orders) for every opcode and ucode.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <utility>

struct F3DGfx {
    struct {
        uintptr_t w0, w1;
    } words;
};
typedef bool (*GfxOpcodeHandlerFunc)(F3DGfx** gfx);
enum UcodeHandlers { ucode_f3db, ucode_f3d, ucode_f3dex, ucode_f3dexb, ucode_f3dex2, ucode_s2dex, ucode_max };
static uint32_t ucode_handler_index;
static unsigned unhandled, stamped, calls[16];
static bool moves;
#define SPDLOG_CRITICAL(...) (++unhandled)
struct ExecStack {
    F3DGfx* cur;
    F3DGfx*& currCmd() { return cur; }
};
static ExecStack g_exec_stack;
static void gfx_set_ucode_handler(UcodeHandlers ucode) { ucode_handler_index = ucode; }
static bool XboxDispatchStamped(GfxOpcodeHandlerFunc handler, F3DGfx** cmd, int8_t) {
    ++stamped;
    return handler(cmd);
}
#define HANDLER(n) \
    static bool h##n(F3DGfx** cmd) { ++calls[n]; if (moves) *cmd += 2; return moves; }
HANDLER(0) HANDLER(1) HANDLER(2) HANDLER(3) HANDLER(4) HANDLER(5) HANDLER(6) HANDLER(7) HANDLER(8) HANDLER(9)
static const int8_t OTR_G_VTX_OTR_FILEPATH = 0x30, OTR_G_SETTIMG_OTR_FILEPATH = 0x3a, OTR_G_DL_OTR_FILEPATH = 0x3b,
    OTR_G_PUSHCD = 0x3c, OTR_G_MTX_OTR_FILEPATH = 0x3d, F3DEX2_G_LOAD_UCODE = (int8_t)0xDD, SHARED = 0x31,
    RDP_A = (int8_t)0xE4, F3_A = 0x01, F3_B = 0x04, S2_A = 0x09;

namespace Seq {
// SEQUENTIAL
}
namespace Comp {
// COMPOSED
}
namespace CompArray {
// COMPOSED C ARRAY
}
namespace StampFirst {
// STAMPED THEN COMPOSED
}

struct Outcome {
    unsigned calls[16], unhandled, stamped;
    uint32_t index;
    ptrdiff_t advance;
    const char* name;
};

static Outcome Execute(void (*step)(), const char* (*name)(int8_t), unsigned op, uint32_t index, uintptr_t w1) {
    F3DGfx commands[4] = {};
    commands[0].words.w0 = (uintptr_t)op << 24 | 4;
    commands[0].words.w1 = w1;
    std::memset(calls, 0, sizeof(calls));
    unhandled = stamped = 0;
    ucode_handler_index = index;
    g_exec_stack.cur = commands;
    step();
    Outcome result = {};
    std::memcpy(result.calls, calls, sizeof(calls));
    result.advance = g_exec_stack.cur - commands;
    result.index = ucode_handler_index;
    ucode_handler_index = index;
    result.name = name((int8_t)op);
    result.unhandled = unhandled;
    result.stamped = stamped;
    return result;
}

static bool Same(const Outcome& a, const Outcome& b) {
    return std::memcmp(a.calls, b.calls, sizeof(a.calls)) == 0 && a.unhandled == b.unhandled &&
           a.index == b.index && a.advance == b.advance &&
           (a.name == b.name || (a.name && b.name && std::strcmp(a.name, b.name) == 0));
}

int main() {
    unsigned cases = 0, handled = 0;
    for (uint32_t index = 0; index < 8; ++index)
        for (unsigned op = 0; op < 256; ++op)
            for (unsigned variant = 0; variant < 4; ++variant) {
                moves = (variant & 1) != 0;
                const uintptr_t w1 = (variant & 2) ? 0x20000 : 0;
                const Outcome expected = Execute(Seq::gfx_step, Seq::GfxGetOpcodeName, op, index, w1);
                const Outcome composed[] = {
                    Execute(Comp::gfx_step, Comp::GfxGetOpcodeName, op, index, w1),
                    Execute(CompArray::gfx_step, CompArray::GfxGetOpcodeName, op, index, w1),
                    Execute(StampFirst::gfx_step, StampFirst::GfxGetOpcodeName, op, index, w1),
                };
                unsigned total = 0;
                for (unsigned i = 0; i < 16; ++i)
                    total += expected.calls[i];
                handled += total;
                for (unsigned i = 0; i < 3; ++i) {
                    if (!Same(expected, composed[i]) || composed[i].stamped != total) {
                        std::printf("FAIL form %u ucode %u opcode 0x%02X variant %u\n", i, index, op, variant);
                        return 1;
                    }
                }
                ++cases;
            }
    if (handled == 0)
        return 2;
    std::printf("PASS composed dispatch matches sequential dispatch in %u cases\n", cases);
    return 0;
}
