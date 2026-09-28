#pragma once

namespace Fast {
namespace ShipyardXenos {

// Retain the complete logical input, including address bits that currently
// map to the same native sampler state. A matching hardware setting alone is
// insufficient to certify a prepared Fast3D draw.
struct SamplerCertificate {
    struct Entry {
        bool valid, linear;
        unsigned s, t, filtering;
    } slots[6];

    bool Update(unsigned tile, bool linear, unsigned s, unsigned t, unsigned filtering) {
        Entry& slot = slots[tile];
        const bool changed = !slot.valid || slot.linear != linear || slot.s != s ||
                             slot.t != t || slot.filtering != filtering;
        slot.valid = true;
        slot.linear = linear;
        slot.s = s;
        slot.t = t;
        slot.filtering = filtering;
        return changed;
    }

    void Invalidate() {
        for (unsigned i = 0; i < 6; ++i)
            slots[i].valid = false;
    }
};

} // namespace ShipyardXenos
} // namespace Fast
