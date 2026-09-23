#pragma once

#include "async/native.h"
#include "clock/chrono.h"
#include <stdint.h>

namespace Shipyard { namespace Clock {

inline DWORD WaitMilliseconds(int64_t nanoseconds) {
    if (nanoseconds <= 0) return 0;
    const int64_t maximum = INT64_C(0xfffffffe);
    int64_t milliseconds = nanoseconds / 1000000 + (nanoseconds % 1000000 != 0);
    return static_cast<DWORD>(milliseconds > maximum ? maximum : milliseconds);
}

inline void SleepFor(nanoseconds requested) {
    if (requested.count() <= 0) return;
    const steady_clock::time_point begin = steady_clock::now();
    nanoseconds remaining = requested;
    do {
        Sleep(WaitMilliseconds(remaining.count()));
        remaining = requested - (steady_clock::now() - begin);
    } while (remaining.count() > 0);
}

}}
