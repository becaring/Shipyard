/* ControlDeck's GUI-dependent input blocks read live ImGui state. Render
 * captures them while the game worker waits at each handoff; game-worker
 * queries answer from the capture (overlap_lus.control_deck). */
#ifndef SHIPYARD_OVERLAP_INPUT_BLOCKED_H
#define SHIPYARD_OVERLAP_INPUT_BLOCKED_H

namespace Shipyard {
namespace Overlap {
struct InputBlocked {
    int blocked[3];
    InputBlocked() { blocked[0] = blocked[1] = blocked[2] = 0; }
    // Render thread only: the deck's queries answer live off the game worker.
    template <class Deck> void Capture(Deck& deck) {
        blocked[0] = deck.GamepadGameInputBlocked() ? 1 : 0;
        blocked[1] = deck.KeyboardGameInputBlocked() ? 1 : 0;
        blocked[2] = deck.MouseGameInputBlocked() ? 1 : 0;
    }
    int Read(unsigned kind, int* value) const {
        if (kind >= 3 || !value) return 0;
        *value = blocked[kind];
        return 1;
    }
};
} // namespace Overlap
} // namespace Shipyard
#endif
