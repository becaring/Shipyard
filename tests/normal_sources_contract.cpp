#include "fast3d/normal_sources.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(bool condition, const char* message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void normal(uint8_t* bytes, size_t index, uint8_t a, uint8_t b, uint8_t c) {
    uint8_t* vertex = bytes + index * 16;
    memset(vertex, (int)(index + 1), 16);
    vertex[12] = a; vertex[13] = b; vertex[14] = c;
}

int main() {
    uint8_t vertices[256 * 16];
    uint8_t sources[257];
    memset(sources, 0xa5, sizeof(sources));
    require(Shipyard::Fast3D::BatchNormalSources(nullptr, 0, nullptr), "zero count");
    require(!Shipyard::Fast3D::BatchNormalSources(nullptr, 1, sources), "null input");
    require(!Shipyard::Fast3D::BatchNormalSources(vertices, 1, nullptr), "null output");
    require(sources[0] == 0xa5, "invalid input wrote output");

    // Normal bytes start at offset 12; position, UV, and alpha do not enter the key.
    normal(vertices, 0, 1, 2, 3);
    normal(vertices, 1, 4, 5, 6);
    normal(vertices, 2, 1, 2, 3);
    vertices[2 * 16 + 15] = 71;
    require(Shipyard::Fast3D::BatchNormalSources(vertices, 3, sources), "small batch");
    require(sources[0] == 0 && sources[1] == 1 && sources[2] == 0,
            "normal source selection or non-normal bytes");

    // Direct-mapped replacement preserves the original within-command behavior.
    // 0x000000 and 0x000022 hash to the same 64-entry slot.
    normal(vertices, 0, 0, 0, 0);
    normal(vertices, 1, 0, 0, 34);
    normal(vertices, 2, 0, 0, 0);
    require(Shipyard::Fast3D::BatchNormalSources(vertices, 3, sources), "collision batch");
    require(sources[0] == 0 && sources[1] == 1 && sources[2] == 2,
            "colliding source was reused after eviction");

    for (size_t i = 0; i < 256; ++i) normal(vertices, i, 12, 34, 56);
    require(Shipyard::Fast3D::BatchNormalSources(vertices, 256, sources), "maximum count");
    require(sources[0] == 0 && sources[255] == 0, "maximum-count reuse");
    for (size_t i = 0; i < 256; ++i) normal(vertices, i, 0, uint8_t(i >> 8), uint8_t(i));
    require(Shipyard::Fast3D::BatchNormalSources(vertices, 256, sources), "unique maximum batch");
    require(sources[0] == 0 && sources[255] == 255, "last representable index");
    memset(sources, 0xa5, sizeof(sources));
    sources[256] = 0xa5;
    require(!Shipyard::Fast3D::BatchNormalSources(vertices, 257, sources), "count above index limit");
    for (size_t i = 0; i < sizeof(sources); ++i)
        require(sources[i] == 0xa5, "invalid count wrote output");
    printf("PASS: normal source stride, offset, collisions, and 8-bit index boundary\n");
}
