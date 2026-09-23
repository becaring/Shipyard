#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Shipyard { namespace Memory {
struct AddressGap {
    uintptr_t lo, hi;

    void Exclude(uintptr_t addr, uintptr_t first, size_t length) {
        if (!length) return;
        const uintptr_t maximum = ~uintptr_t(0);
        // Saturate so an interval crossing the address-space end does not wrap.
        const uintptr_t last = (length - 1 > maximum - first)
            ? maximum : first + length - 1;
        if (addr < first) {
            if (first - 1 < hi) hi = first - 1;
        } else if (addr > last) {
            if (last + 1 > lo) lo = last + 1;
        }
    }
};
} }
