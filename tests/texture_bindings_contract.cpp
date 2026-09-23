#include "fast3d/texture_bindings.h"
#include <cstdio>

int main() {
    Shipyard::Fast3D::TextureBindings<6> bindings;
    const Shipyard::Fast3D::TextureSamplerState a(true, 1, 0), b(true, 2, 0);
    int first = 1, second = 2;
    if (!bindings.NeedsSelect(0, &first, 7, a)) return 1;
    bindings.Selected(0, &first, 7, a);
    bindings.Selected(1, &first, 7, a);
    if (bindings.NeedsSelect(0, &first, 7, a)) return 2;
    bindings.SamplerChanged(1, b);
    if (!bindings.NeedsSelect(0, &first, 7, b)) return 3;
    if (bindings.NeedsSelect(1, &first, 7, b)) return 4;
    bindings.Selected(0, &first, 7, b);
    if (bindings.NeedsSampler(0, b) || !bindings.NeedsSampler(0, a)) return 5;
    if (!bindings.NeedsSelect(0, &second, 7, b)) return 6;
    if (!bindings.NeedsSelect(0, &first, 8, b)) return 7;
    bindings.Selected(5, &second, 8, b);
    if (bindings.Bound<int>(5) != &second) return 8;
    bindings.Invalidate();
    for (unsigned i = 0; i < 6; ++i)
        if (bindings.Known(i) || bindings.Bound<int>(i)) return 9;
    std::puts("PASS six-slot binding and sampler certificates");
    return 0;
}
