#pragma once

namespace Shipyard { namespace Fast3D {

// Decode a rendered region while retaining the loaded image's source stride.
// Palette bytes are N64 big-endian RGBA5551; output bytes are RGBA on every CPU.
inline bool DecodeCi8Rows(unsigned char* output, const unsigned char* source,
                          const unsigned char* palette0, const unsigned char* palette1,
                          unsigned sourceStride, unsigned width, unsigned height) {
    if (!output || !source || !palette0 || !palette1 || !width || !height || sourceStride < width)
        return false;
    for (unsigned y = 0; y < height; ++y) {
        const unsigned char* row = source + y * sourceStride;
        for (unsigned x = 0; x < width; ++x) {
            const unsigned index = row[x];
            const unsigned char* palette = index < 128 ? palette0 : palette1;
            const unsigned offset = (index & 127) * 2;
            const unsigned color = (unsigned(palette[offset]) << 8) | palette[offset + 1];
            const unsigned r = color >> 11, g = (color >> 6) & 31, b = (color >> 1) & 31;
            *output++ = (unsigned char)(r * 255 / 31);
            *output++ = (unsigned char)(g * 255 / 31);
            *output++ = (unsigned char)(b * 255 / 31);
            *output++ = (color & 1) ? 255 : 0;
        }
    }
    return true;
}

} }
