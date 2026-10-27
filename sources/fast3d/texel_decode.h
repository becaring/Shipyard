#pragma once

#include <stdint.h>

namespace Shipyard { namespace Fast3D {

// An N64 RGBA5551 texel as the four RGBA bytes Fast3D's importers write, in
// one word (R in the high byte). Each table value is exactly SCALE_5_8, that
// is v * 255 / 31, of its 5-bit channel.
static const uint8_t kScale5To8[32] = { 0, 8, 16, 24, 32, 41, 49, 57, 65, 74, 82, 90, 98, 106, 115, 123,
                                        131, 139, 148, 156, 164, 172, 180, 189, 197, 205, 213, 222, 230, 238, 246, 255 };

inline uint32_t Rgba5551(uint16_t color) {
    return (uint32_t(kScale5To8[color >> 11]) << 24) | (uint32_t(kScale5To8[(color >> 6) & 31]) << 16) |
           (uint32_t(kScale5To8[(color >> 1) & 31]) << 8) | ((color & 1) ? 0xFFu : 0u);
}

// Stores the R, G, B, A bytes in memory order: one word store on the
// big-endian Xenon (upload rows are 4-byte aligned), byte stores elsewhere.
inline void StoreRgba(unsigned char* out, uint32_t rgba) {
#if defined(_M_PPC)
    *reinterpret_cast<uint32_t*>(out) = rgba;
#else
    out[0] = (unsigned char)(rgba >> 24);
    out[1] = (unsigned char)(rgba >> 16);
    out[2] = (unsigned char)(rgba >> 8);
    out[3] = (unsigned char)rgba;
#endif
}

} }
