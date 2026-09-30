#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Shipyard {
namespace Fast3D {

// A command's records have a 16-byte stride and a three-byte normal at offset
// 12. Output entries are indices into that same command, so 256 is the limit.
// The caller owns lighting eligibility, alias safety and value publication.
inline bool BatchNormalSources(const void* records, size_t count, uint8_t* sources) {
    if (count > 256 || (count && (!records || !sources)))
        return false;
    if (count <= 1) {
        if (count)
            sources[0] = 0;
        return true;
    }

    uint32_t slots[64];
    for (unsigned slot = 0; slot < 64; ++slot)
        slots[slot] = 0xffffffffu;
    const uint8_t* bytes = static_cast<const uint8_t*>(records);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* normal = bytes + i * 16 + 12;
        const uint32_t key = (uint32_t(normal[0]) << 16) | (uint32_t(normal[1]) << 8) | normal[2];
        const unsigned slot = (key * 0x9e3779b9u) >> 26;
        const uint32_t entry = slots[slot];
        if (entry != 0xffffffffu && (entry & 0xffffffu) == key) {
            sources[i] = uint8_t(entry >> 24);
        } else {
            sources[i] = uint8_t(i);
            slots[slot] = (uint32_t(i) << 24) | key;
        }
    }
    return true;
}

} // namespace Fast3D
} // namespace Shipyard
