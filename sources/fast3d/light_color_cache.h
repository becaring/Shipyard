#pragma once
#include <stdint.h>

namespace Shipyard {
namespace Fast3D {

// Value-only RGB cache. The consumer owns capacity, color layout, refresh
// timing, eligibility, and the byte-to-float conversion. Zero initialization
// represents an uncached black entry with three zero values.
template <unsigned Capacity> struct LightColorValueCache {
    uint32_t rgb[Capacity];
    float value[Capacity][3];

    template <class Convert> void Refresh(unsigned index, const uint8_t* color, Convert convert) {
        const uint32_t key = (uint32_t(color[0]) << 16) | (uint32_t(color[1]) << 8) | color[2];
        if (rgb[index] != key) {
            rgb[index] = key;
            for (unsigned channel = 0; channel < 3; ++channel)
                value[index][channel] = convert(color[channel]);
        }
    }

    const float (*Values() const)[3] {
        return value;
    }
};

} // namespace Fast3D
} // namespace Shipyard
