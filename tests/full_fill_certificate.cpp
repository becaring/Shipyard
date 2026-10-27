#include "fast3d/full_fill_certificate.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

using Shipyard::Fast3D::FullFillCovers;
using Shipyard::Fast3D::FullGameFillCovers;

static bool covers(uint32_t width, uint32_t height, int32_t left, int32_t top, int32_t right, int32_t bottom,
                   float sx, float sy, float sw, float sh, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3) {
    return FullGameFillCovers(320, 240, width, height, left, top, right, bottom, sx, sy, sw, sh, c0, c1, c2, c3);
}

// Independent copy of the upstream rectangle path's float operations. If the
// certificate accepts, the two emitted triangles must span the target edges.
static float rectangleX(int32_t coord, uint32_t nativeWidth, uint32_t targetWidth, uint32_t targetHeight) {
    float x = (float)coord;
    x = x / (4.0f * (nativeWidth / 2)) - 1.0f;
    return x * (4.0f / 3.0f) / ((float)targetWidth / (float)targetHeight);
}

static float rectangleY(int32_t coord, uint32_t nativeHeight) {
    float y = (float)coord;
    return -(y / (4.0f * (nativeHeight / 2))) + 1.0f;
}

int main() {
    // 4:3, 16:9 and scaled 1080 game targets. Edges are U10.2 after FILL's +4.
    assert(covers(1280, 960, 0, 0, 1280, 960, 0, 0, 1280, 960, 0, 0, 0, 0));
    assert(covers(1280, 720, -216, 0, 1504, 960, 0, 0, 1280, 720, 0, 0, 0, 0));
    assert(covers(1920, 1080, -216, 0, 1504, 960, 0, 0, 1920, 1080, 0, 0, 0, 0));

    // Letterbox, aspect/narrow fill, incomplete scissor and stale rectangle
    // clip flags must preserve the old target image.
    assert(!covers(1920, 1080, -216, 4, 1504, 960, 0, 0, 1920, 1080, 0, 0, 0, 0));
    assert(!covers(1920, 1080, -216, 0, 1504, 956, 0, 0, 1920, 1080, 0, 0, 0, 0));
    assert(!covers(1920, 1080, 0, 0, 1280, 960, 0, 0, 1920, 1080, 0, 0, 0, 0));
    assert(!covers(1920, 1080, -216, 0, 1504, 960, 1, 0, 1919, 1080, 0, 0, 0, 0));
    assert(!covers(1920, 1080, -216, 0, 1504, 960, 0, 0, 1919, 1080, 0, 0, 0, 0));
    assert(!covers(1920, 1080, -216, 0, 1504, 960, 0, 0, 1920, 1080, 1, 1, 0, 1));
    assert(!covers(1920, 1080, -216, 0, 1504, 960, 0, 0, 1920, 1080, 0, 1, 1, 1));

    // One-pixel scissor conversion follows the backend's float-to-int truncation.
    assert(covers(1920, 1080, -216, 0, 1504, 960, -0.25f, -0.25f, 1920.25f, 1080.25f, 0, 0, 0, 0));
    assert(!covers(1920, 1080, -216, 0, 1504, 960, 0, 0, 1919.75f, 1080, 0, 0, 0, 0));
    assert(!FullGameFillCovers(320, 240, 0, 1080, -216, 0, 1504, 960, 0, 0, 1920, 1080, 0, 0, 0, 0));
    assert(!FullGameFillCovers(1, 240, 1920, 1080, -216, 0, 1504, 960, 0, 0, 1920, 1080, 0, 0, 0, 0));

    // A consumer viewport (after AdjustVIewportOrScissor) must cover the target too.
    assert(FullFillCovers(1280, 720, -1, 1, 1, -1, 0, 0, 1280, 720, 0, 0, 1280, 720, 0, 0, 0, 0));
    assert(FullFillCovers(1280, 720, -1, 1, 1, -1, -8, -8, 1300, 740, 0, 0, 1280, 720, 0, 0, 0, 0));
    assert(!FullFillCovers(1280, 720, -1, 1, 1, -1, 1, 0, 1280, 720, 0, 0, 1280, 720, 0, 0, 0, 0));
    assert(!FullFillCovers(1280, 720, -1, 1, 1, -1, 0, 0, 1280, 719, 0, 0, 1280, 720, 0, 0, 0, 0));
    assert(!FullFillCovers(1280, 720, -1, 1, 1, -1, 0, 0, -1, 720, 0, 0, 1280, 720, 0, 0, 0, 0));
    assert(!FullFillCovers(1280, 720, -0.99f, 1, 1, -1, 0, 0, 1280, 720, 0, 0, 1280, 720, 0, 0, 0, 0));
    assert(!FullFillCovers(0, 720, -1, 1, 1, -1, 0, 0, 1280, 720, 0, 0, 1280, 720, 0, 0, 0, 0));
    // Rejection bits shared by no triangle's three corners keep both halves.
    assert(FullFillCovers(1280, 720, -1, 1, 1, -1, 0, 0, 1280, 720, 0, 0, 1280, 720, 1, 0, 1, 0));

    const uint32_t widths[] = { 960, 1280, 1600, 1920, 2048 };
    const uint32_t heights[] = { 720, 900, 960, 1080, 1536 };
    for (unsigned d = 0; d < sizeof(widths) / sizeof(widths[0]); ++d) {
        const uint32_t width = widths[d], height = heights[d];
        for (int left = -300; left <= 4; ++left) {
            for (int right = 1276; right <= 1540; ++right) {
                if (!covers(width, height, left, 0, right, 960, 0, 0, (float)width, (float)height, 0, 0, 0, 0))
                    continue;
                assert(rectangleX(left, 320, width, height) <= -1.0f);
                assert(rectangleX(right, 320, width, height) >= 1.0f);
                assert(rectangleY(0, 240) >= 1.0f);
                assert(rectangleY(960, 240) <= -1.0f);
            }
        }
    }
    printf("PASS full fill certificate\n");
    return 0;
}
