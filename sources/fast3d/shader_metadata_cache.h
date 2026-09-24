#pragma once

#include <stdint.h>

namespace Shipyard { namespace Fast3D {

// Immutable rendering-API result only. The consumer owns program creation,
// failure exits, state transition order, variant bounds and clip policy.
struct ShaderMetadata {
    uint8_t numInputs;
    bool usedTextures[2];
};

template<unsigned VariantCount> struct ShaderMetadataCache {
    struct Entry {
        const void* backend;
        const void* program;
        ShaderMetadata value;
        bool valid;
    } entries[VariantCount];

    void Invalidate() {
        for (unsigned i = 0; i < VariantCount; ++i) entries[i].valid = false;
    }

    bool Lookup(unsigned variant, const void* backend, const void* program,
                ShaderMetadata& result) const {
        const Entry& entry = entries[variant];
        if (!program || !entry.valid || entry.backend != backend || entry.program != program) return false;
        result = entry.value;
        return true;
    }

    void Store(unsigned variant, const void* backend, const void* program,
               const ShaderMetadata& value) {
        if (!program) return;
        Entry& entry = entries[variant];
        entry.backend = backend;
        entry.program = program;
        entry.value = value;
        entry.valid = true;
    }
};

} }
