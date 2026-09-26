#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cstdio>

// ACTUAL BYTE EQUALITY

extern "C" __declspec(noinline) bool Equal56(const void* a, const void* b) {
    return XboxTriangleBytesEqual(a, b, 56);
}

int main() {
    unsigned char left[160], right[160];
    unsigned checks = 0;
    for (unsigned ao = 0; ao < 8; ++ao)
        for (unsigned bo = 0; bo < 8; ++bo)
            for (unsigned n = 0; n <= 128; ++n) {
                for (unsigned i = 0; i < 160; ++i) left[i] = (unsigned char)(i * 79 + 23);
                std::memcpy(right + bo, left + ao, n);
                if (!XboxTriangleBytesEqual(left + ao, right + bo, n)) return 1;
                ++checks;
                for (unsigned j = 0; j < n; ++j) {
                    right[bo + j] ^= 0x80;
                    if (XboxTriangleBytesEqual(left + ao, right + bo, n) !=
                        (std::memcmp(left + ao, right + bo, n) == 0)) return 2;
                    right[bo + j] ^= 0x80;
                    ++checks;
                }
            }
    const uint32_t bits[] = {0, 0x80000000u, 0x7fc00000u, 0x7fc00001u, 0x7f800001u, 0xffffffffu};
    for (unsigned i = 0; i < 6; ++i)
        for (unsigned j = 0; j < 6; ++j)
            if (XboxTriangleBytesEqual(bits + i, bits + j, 4) !=
                (std::memcmp(bits + i, bits + j, 4) == 0)) return 3;
    struct Padded { char value; uint32_t word; } a, b;
    std::memset(&a, 0x55, sizeof(a));
    std::memcpy(&b, &a, sizeof(b));
    for (unsigned i = 0; i < sizeof(b); ++i) {
        reinterpret_cast<unsigned char*>(&b)[i] ^= 1;
        if (XboxTriangleBytesEqual(&a, &b, sizeof(a))) return 4;
        reinterpret_cast<unsigned char*>(&b)[i] ^= 1;
    }
    std::printf("PASS %u alignment/length/tail/byte checks, padding and signed-zero/NaN bits\n", checks);
}
