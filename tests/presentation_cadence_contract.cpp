// Presented rate counts displays, not GUI builds, and restarts after a clock reset.
#include "presentation/pacing.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    Shipyard::PresentationCadence cadence;
    assert(cadence.Fps() == 0);
    for (unsigned frame = 0; frame <= 30; ++frame) cadence.Observe(frame * 1000ull / 45, 1000);
    assert(std::fabs(cadence.Fps() - 45.0f) < 1.0f);
    cadence.Observe(0, 1000);
    assert(cadence.Fps() == 0);
    std::puts("Presentation cadence passed");
    return 0;
}
