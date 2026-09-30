#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fast3d/coordinate.h"

static uint32_t state = 0x19740513U;
static uint32_t Next() { state = state * 1664525U + 1013904223U; return state; }

static float Original(float raw, int shift, float start, bool half, float dimension) {
    float value = raw / 32.0f;
    if (shift != 0) {
        if (shift <= 10) {
            value /= 1 << shift;
        } else {
            value *= 1 << (16 - shift);
        }
    }
    value -= start / 4.0f;
    if (half) value += 0.5f;
    return value / dimension;
}

int main() {
    const float edges[] = {-32768.0f, -1.0f, -0.0f, 0.0f, 1.0f,
                           32767.0f, 65535.0f, 65536.0f};
    const float starts[] = {-8192.0f, -0.0f, 0.0f, 0.125f, 16383.75f};
    const float dimensions[] = {1.0f, 3.0f, 257.0f, 65535.0f};
    unsigned cases = 0;
    for (unsigned s = 0; s < 16; ++s) {
        for (unsigned h = 0; h < 2; ++h) {
            for (unsigned r = 0; r < sizeof(edges) / sizeof(edges[0]); ++r) {
                for (unsigned o = 0; o < sizeof(starts) / sizeof(starts[0]); ++o) {
                    for (unsigned d = 0; d < sizeof(dimensions) / sizeof(dimensions[0]); ++d) {
                        const float expected = Original(edges[r], s, starts[o], h != 0, dimensions[d]);
                        const float actual = Shipyard::Fast3D::Coordinate(edges[r], s, starts[o], h != 0, dimensions[d]);
                        if (memcmp(&expected, &actual, sizeof(float))) {
                            printf("edge mismatch shift=%u half=%u raw=%u start=%u dim=%u\n", s, h, r, o, d);
                            return 1;
                        }
                        ++cases;
                    }
                }
            }
        }
    }
    for (unsigned i = 0; i < 65536; ++i) {
        const float raw = (float)(int16_t)Next();
        const float start = (float)(int16_t)Next() * 0.125f;
        const int shift = (int)(Next() & 15);
        const bool half = (Next() & 1) != 0;
        const float dimension = (float)(1 + (Next() & 2047));
        const float expected = Original(raw, shift, start, half, dimension);
        const float actual = Shipyard::Fast3D::Coordinate(raw, shift, start, half, dimension);
        if (memcmp(&expected, &actual, sizeof(float))) {
            printf("domain mismatch %u\n", i);
            return 2;
        }
        ++cases;
    }
    printf("PASS %u exact coordinate cases\n", cases);
    return 0;
}
