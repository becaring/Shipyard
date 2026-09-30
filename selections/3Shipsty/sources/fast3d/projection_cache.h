#pragma once

#include <stdint.h>
#include <string.h>
#ifndef _XBOX
#include <float.h>
#endif

namespace Shipyard {
namespace Fast3D {

// The two FPSCR RN bits are part of a projected value's identity. A finite
// division can round differently even when all three source words match.
#ifdef _XBOX
static __declspec(naked) uint32_t ProjectionRoundingMode() {
    __asm {
        mffs fp0
        stfd fp0,-8(sp)
        lwz r3,-4(sp)
        rlwinm r3,r3,0,30,31
        blr
    }
}
#else
inline uint32_t ProjectionRoundingMode() {
    return (_controlfp(0, 0) & _MCW_RC) >> 8;
}
#endif

// Value only: the caller owns array capacity, vertex-index mapping, cull
// policy, and any later reread of source vertices. Float references permit
// PPC integer loads of the source bits without first spilling FP arguments.
struct ProjectionValueCache {
    float x, y;
    uint32_t keyX, keyY, keyW, roundingMode;
    bool valid;

    // The XDK uses __forceinline; host compilers use inline.
    // clang-format off
#ifdef _MSC_VER
    __forceinline
#else
    inline
#endif
    void Load(const float& inputX, const float& inputY, const float& inputW) {
        Load(inputX, inputY, inputW, ProjectionRoundingMode());
    }

    // A caller may share this token across arithmetic that cannot change RN.
    // Divisions update exception flags but preserve the two rounding bits.
    // Keep the source-word guards and exceptional-operation behavior below.
#ifdef _MSC_VER
    __forceinline
#else
    inline
#endif
    void Load(const float& inputX, const float& inputY, const float& inputW, uint32_t mode) {
        uint32_t bitsX;
        uint32_t bitsY;
        uint32_t bitsW;
        memcpy(&bitsX, &inputX, 4);
        memcpy(&bitsY, &inputY, 4);
        memcpy(&bitsW, &inputW, 4);
        if (valid && keyX == bitsX && keyY == bitsY && keyW == bitsW && roundingMode == mode)
            return;
        x = inputX / inputW;
        y = inputY / inputW;
        keyX = bitsX;
        keyY = bitsY;
        keyW = bitsW;
        roundingMode = mode;
        // Repeat exceptional operations, including either zero divisor and
        // any infinity/NaN input. Raw-bit keys retain signed zero and NaN data.
        valid = (bitsX & 0x7f800000U) != 0x7f800000U && (bitsY & 0x7f800000U) != 0x7f800000U &&
                (bitsW & 0x7f800000U) != 0x7f800000U && (bitsW & 0x7fffffffU) != 0;
    }
    // clang-format on
};

} // namespace Fast3D
} // namespace Shipyard
