#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _MSC_VER
#define SHIPYARD_UPDATE_INLINE __forceinline
#else
#define SHIPYARD_UPDATE_INLINE inline
#endif

namespace Shipyard { namespace Fast3D {

// Per-update commands retain decoded operands and preparation indices. Extra
// raw command words belong in the caller's decoded operand storage, not every
// record. The caller owns resource references until all presentations finish.
template<class Command, class Vertex>
struct UpdateInstruction {
    Command* source;
    uint32_t word0, word1, ucode;
    uint32_t kind, payload[3];
    const Vertex* vertexSource;
    uint32_t materialPlan, positionPlan, lightPlan;

    // Explicit initialization writes each compact field once. Avoid bulk
    // zeroing followed by overwriting the header and preparation indices.
    SHIPYARD_UPDATE_INLINE void Initialize(Command* command, uint32_t firstWord,
                                          uint32_t secondWord, uint32_t microcode,
                                          uint32_t commandKind) {
        source = command;
        word0 = firstWord;
        word1 = secondWord;
        ucode = microcode;
        kind = commandKind;
        payload[0] = payload[1] = payload[2] = 0;
        vertexSource = 0;
        materialPlan = positionPlan = lightPlan = ~uint32_t(0);
    }
};

// Called synchronously after a command handler. consumedBytes is the distance
// from its initial command to its final command, excluding the initial word.
// Decode only words in that span. Missing tails retain the zero values of the
// old zero-initialized raw snapshot, without reading adjacent commands.
template<class Command>
SHIPYARD_UPDATE_INLINE uint32_t CapturedExtraWord(const Command* before,
                                                uintptr_t consumedBytes,
                                                unsigned index, unsigned word) {
    if (consumedBytes < index * sizeof(Command)) return 0;
    return uint32_t(word ? before[index].words.w1 : before[index].words.w0);
}

} }

#undef SHIPYARD_UPDATE_INLINE
