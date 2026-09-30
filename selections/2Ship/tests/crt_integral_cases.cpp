#include <cassert>
#include <cmath>
#include <cstring>
#include <float.h>
#include <limits>
#include <fenv.h>
extern "C" double __cdecl tested_nearbyint(double);
extern "C" float __cdecl tested_nearbyintf(float);
extern "C" float __cdecl tested_roundf(float);
extern "C" float __cdecl tested_truncf(float);

template<class T> bool Same(T a, T b) {
    return (std::isnan(a) && std::isnan(b)) || std::memcmp(&a, &b, sizeof(T)) == 0;
}
int main() {
    const double values[] = { 0.0, -0.0, 0.1, -0.1, 0.5, -0.5, 1.5, -1.5,
        2.5, -2.5, 3.5, -3.5, 12345.25, -12345.75,
        4503599627370495.5, -4503599627370495.5, 4503599627370496.0,
        std::numeric_limits<double>::denorm_min(), -std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() };
    const unsigned modes[] = { _RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP };
    for (unsigned i = 0; i < 4; ++i) {
        _controlfp(modes[i], _MCW_RC);
        for (unsigned j = 0; j < sizeof(values)/sizeof(values[0]); ++j) {
            volatile double value = values[j];
            assert(Same(tested_nearbyint(value), std::nearbyint(value)));
            volatile float single = static_cast<float>(value);
            assert(Same(tested_nearbyintf(single), std::nearbyint(single)));
            assert(Same(tested_roundf(single), std::round(single)));
            assert(Same(tested_truncf(single), std::trunc(single)));
        }
        feclearexcept(FE_ALL_EXCEPT);
        volatile double rounded = tested_nearbyint(1.25);
        (void)rounded;
        assert((fetestexcept(FE_INEXACT) & FE_INEXACT) == 0);
    }
    _controlfp(_RC_NEAR, _MCW_RC);
}
