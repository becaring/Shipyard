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

#ifndef SHIPYARD_CRT_NEARBYINT
#define SHIPYARD_CRT_NEARBYINT nearbyint
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

double Integral(double value, Rounding mode) {
    typedef unsigned long long Bits;
    typedef char Binary64[(sizeof(double) == 8 && sizeof(Bits) == 8 &&
                           DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024) ? 1 : -1];
    Bits bits;
    memcpy(&bits, &value, sizeof(bits));
    const Bits sign = bits & 0x8000000000000000ULL;
    const Bits magnitude = bits & 0x7fffffffffffffffULL;
    const unsigned int exponent = static_cast<unsigned int>(magnitude >> 52);
    if (exponent == 2047)
        return (magnitude & 0x000fffffffffffffULL) ? value + value : value;
    if (exponent >= 1075 || magnitude == 0)
        return value;
    Bits result;
    if (exponent < 1023) {
        bool increment = (mode == Downward && sign != 0) || (mode == Upward && sign == 0);
        if (mode == NearestEven)
            increment = magnitude > 0x3fe0000000000000ULL;
        result = sign | (increment ? 0x3ff0000000000000ULL : 0);
    } else {
        const Bits unit = 1ULL << (1075 - exponent);
        const Bits mask = unit - 1;
        const Bits remainder = magnitude & mask;
        if (remainder == 0)
            return value;
        const Bits half = unit >> 1;
        bool increment = (mode == Downward && sign != 0) || (mode == Upward && sign == 0);
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

extern "C" double __cdecl SHIPYARD_CRT_NEARBYINT(double value) {
    const unsigned int control = _controlfp(0, 0) & _MCW_RC;
    const Rounding mode = control == _RC_DOWN   ? Downward
                          : control == _RC_UP   ? Upward
                          : control == _RC_CHOP ? TowardZero
                                                : NearestEven;
    return Integral(value, mode);
}
