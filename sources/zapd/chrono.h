// The steady_clock subset ZAPD's profiling timers use; the XDK library has no
// <chrono>. Ticks come from the performance counter.
#ifndef SHIPYARD_ZAPD_CHRONO_H
#define SHIPYARD_ZAPD_CHRONO_H
#ifndef _XBOX
#include <chrono>
#else
#include <xtl.h>

namespace std {
namespace chrono {
struct duration {
    long long ticks;
};
template <long long PerSecond> struct unit {
    long long value;
    explicit unit(long long count) : value(count) {}
    long long count() const { return value; }
};
typedef unit<1> seconds;
typedef unit<1000> milliseconds;
struct steady_clock {
    struct time_point {
        long long ticks;
        duration operator-(const time_point& other) const {
            duration result = {ticks - other.ticks};
            return result;
        }
    };
    static time_point now() {
        LARGE_INTEGER value;
        QueryPerformanceCounter(&value);
        time_point result = {value.QuadPart};
        return result;
    }
};
template <class Unit> Unit duration_cast(duration value);
template <> inline seconds duration_cast<seconds>(duration value) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return seconds(value.ticks / frequency.QuadPart);
}
template <> inline milliseconds duration_cast<milliseconds>(duration value) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return milliseconds(value.ticks * 1000 / frequency.QuadPart);
}
} // namespace chrono
} // namespace std
#endif
#endif
