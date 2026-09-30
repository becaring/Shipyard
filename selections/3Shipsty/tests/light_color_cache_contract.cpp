#include "fast3d/light_color_cache.h"
#include <stdint.h>
#include <stdio.h>

struct CountingFloat {
    unsigned* calls;
    float operator()(uint8_t byte) const { ++*calls; return (float)byte; }
};

int main() {
    struct Guard {
        uint32_t before;
        Shipyard::Fast3D::LightColorValueCache<33> cache;
        uint32_t after;
    } guarded = {};
    guarded.before = 0x12345678;
    guarded.after = 0x87654321;
    unsigned calls = 0;
    CountingFloat convert = { &calls };
    const uint8_t black[3] = { 0, 0, 0 };
    const uint8_t first[3] = { 4, 200, 255 };
    const uint8_t second[3] = { 4, 201, 255 };
    const float (*values)[3] = guarded.cache.Values();

    guarded.cache.Refresh(32, black, convert);
    if (calls != 0 || values[32][0] != 0.0f || values[32][1] != 0.0f || values[32][2] != 0.0f) return 1;
    guarded.cache.Refresh(32, first, convert);
    if (calls != 3 || values[32][0] != 4.0f || values[32][1] != 200.0f || values[32][2] != 255.0f) return 2;
    guarded.cache.Refresh(32, first, convert);
    if (calls != 3) return 3;
    guarded.cache.Refresh(0, second, convert);
    if (calls != 6 || values[0][1] != 201.0f || values[32][1] != 200.0f) return 4;
    guarded.cache.Refresh(32, second, convert);
    if (calls != 9 || values[32][1] != 201.0f) return 5;
    guarded.cache.Refresh(32, black, convert);
    if (calls != 12 || values[32][0] != 0.0f || values[32][1] != 0.0f || values[32][2] != 0.0f) return 6;
    if (guarded.before != 0x12345678 || guarded.after != 0x87654321) return 7;
    puts("PASS: RGB key refresh, zero black, independent slots, and last-slot canaries");
    return 0;
}
