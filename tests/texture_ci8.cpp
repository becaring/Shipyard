#include "fast3d/texture_ci8.h"
#include <assert.h>
#include <string.h>

int main() {
    unsigned char palette0[256] = {}, palette1[256] = {};
    // Big-endian RGBA5551: red, green, blue, and transparent white.
    palette0[2] = 0xf8; palette0[3] = 0x01;
    palette0[4] = 0x07; palette0[5] = 0xc1;
    palette1[0] = 0x00; palette1[1] = 0x3f;
    palette1[2] = 0xff; palette1[3] = 0xfe;
    const unsigned char source[] = {1, 2, 99, 99, 128, 129, 99, 99};
    const unsigned char expected[] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 0
    };
    unsigned char output[20];
    memset(output, 0xcd, sizeof(output));
    assert(Shipyard::Fast3D::DecodeCi8Rows(output, source, palette0, palette1, 4, 2, 2));
    assert(memcmp(output, expected, sizeof(expected)) == 0);
    assert(output[16] == 0xcd); // Output uses cropped width, not input stride.
    assert(!Shipyard::Fast3D::DecodeCi8Rows(output, source, palette0, palette1, 0, 2, 2));
    assert(!Shipyard::Fast3D::DecodeCi8Rows(output, source, palette0, palette1, 1, 2, 2));
    assert(Shipyard::Fast3D::DecodeCi8Rows(output, source, palette0, palette1, 4, 4, 1));
    assert(output[0] == 255 && output[7] == 255);
    return 0;
}
