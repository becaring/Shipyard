#include <float.h>
#include <string.h>

// Native IEEE binary32 numeric bits are independent of CPU byte order.
typedef char
    ShipyardFloatMustBeBinary32[(sizeof(float) == 4 && sizeof(unsigned int) == 4 &&
                                 FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128)
                                    ? 1
                                    : -1];

#ifndef SHIPYARD_CRT_ROUNDF
#define SHIPYARD_CRT_ROUNDF roundf
#define SHIPYARD_CRT_TRUNCF truncf
#define SHIPYARD_CRT_NEARBYINTF nearbyintf
#endif

namespace {
enum Rounding { NearestEven, TowardZero, Downward, Upward, AwayAtHalf };

float Integral(float value, Rounding mode) {
    unsigned int bits;
    memcpy(&bits, &value, sizeof(bits));
    const unsigned int sign = bits & 0x80000000U;
    const unsigned int magnitude = bits & 0x7fffffffU;
    const unsigned int exponent = magnitude >> 23;
    if (exponent == 255) {
        // Only signaling NaNs require arithmetic to raise FE_INVALID.
        return (magnitude & 0x007fffffU) ? value + value : value;
    }
    if (exponent >= 150 || magnitude == 0)
        return value;

    unsigned int result;
    if (exponent < 127) {
        bool increment = (mode == Downward && sign != 0) || (mode == Upward && sign == 0);
        if (mode == AwayAtHalf)
            increment = magnitude >= 0x3f000000U;
        if (mode == NearestEven)
            increment = magnitude > 0x3f000000U;
        result = sign | (increment ? 0x3f800000U : 0);
    } else {
        const unsigned int unit = 1U << (150 - exponent);
        const unsigned int mask = unit - 1;
        const unsigned int remainder = magnitude & mask;
        if (remainder == 0)
            return value;
        const unsigned int half = unit >> 1;
        bool increment = (mode == Downward && sign != 0) || (mode == Upward && sign == 0);
        if (mode == AwayAtHalf)
            increment = remainder >= half;
        if (mode == NearestEven)
            increment = remainder > half || (remainder == half && (magnitude & unit) != 0);
        result = (bits & ~mask) + (increment ? unit : 0);
    }
    memcpy(&value, &result, sizeof(value));
    return value;
}
} // namespace

extern "C" float __cdecl SHIPYARD_CRT_ROUNDF(float value) {
    return Integral(value, AwayAtHalf);
}

extern "C" float __cdecl SHIPYARD_CRT_TRUNCF(float value) {
    return Integral(value, TowardZero);
}

extern "C" float __cdecl SHIPYARD_CRT_NEARBYINTF(float value) {
    const unsigned int control = _controlfp(0, 0) & _MCW_RC;
    const Rounding mode = control == _RC_DOWN   ? Downward
                          : control == _RC_UP   ? Upward
                          : control == _RC_CHOP ? TowardZero
                                                : NearestEven;
    return Integral(value, mode);
}
