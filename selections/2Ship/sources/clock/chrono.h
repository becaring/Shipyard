#pragma once
#include <stdint.h>
#include <ctime>
#include <limits>
#include <type_traits>

namespace Shipyard {
namespace Clock {
template <int64_t A, int64_t B> struct gcd {
    static const int64_t value = gcd<B, A % B>::value;
};
template <int64_t A> struct gcd<A, 0> {
    static const int64_t value = A;
};
template <int64_t Num, int64_t Den = 1> struct ratio {
    static_assert(Num > 0 && Den > 0, "chrono periods must be positive");
    static const int64_t num = Num / gcd<Num, Den>::value, den = Den / gcd<Num, Den>::value;
};
template <class Rep, class Period = ratio<1> > class duration;
template <class To, class Rep, class Period> To duration_cast(const duration<Rep, Period>& value);
template <class R1, class P1, class R2, class P2> struct common_duration {
    typedef typename std::common_type<R1, R2>::type rep;
    typedef ratio<gcd<P1::num, P2::num>::value, (P1::den / gcd<P1::den, P2::den>::value) * P2::den>
        period;
    typedef duration<rep, period> type;
};
template <class Rep, class Period> class duration {
public:
    typedef Rep rep;
    typedef Period period;
    duration() : value_(0) {
    }
    template <class R>
    explicit duration(const R& count,
                      typename std::enable_if<std::is_convertible<R, Rep>::value &&
                                              (std::is_floating_point<Rep>::value ||
                                               !std::is_floating_point<R>::value)>::type* = 0)
        : value_(static_cast<Rep>(count)) {
    }
    template <class R, class P>
    duration(
        const duration<R, P>& value,
        typename std::enable_if<std::is_floating_point<Rep>::value ||
                                (!std::is_floating_point<R>::value &&
                                 ((P::num * Period::den) % (P::den * Period::num) == 0))>::type* =
            0)
        : value_(duration_cast<duration>(value).count()) {
    }
    rep count() const {
        return value_;
    }
    static duration zero() {
        return duration(0);
    }
    static duration min() {
        return duration(std::numeric_limits<rep>::is_integer ? (std::numeric_limits<rep>::min)()
                                                             : -(std::numeric_limits<rep>::max)());
    }
    static duration max() {
        return duration((std::numeric_limits<rep>::max)());
    }
    duration operator+() const {
        return *this;
    }
    duration operator-() const {
        return duration(-value_);
    }
    duration& operator+=(const duration& v) {
        value_ += v.value_;
        return *this;
    }
    duration& operator-=(const duration& v) {
        value_ -= v.value_;
        return *this;
    }
    duration& operator++() {
        ++value_;
        return *this;
    }
    duration& operator--() {
        --value_;
        return *this;
    }

private:
    rep value_;
};
namespace detail {
template <int64_t Num, int64_t Den> struct cast_count {
    template <class R> static R apply(R value) {
        return value * static_cast<R>(Num) / static_cast<R>(Den);
    }
};
template <int64_t Num> struct cast_count<Num, 1> {
    template <class R> static R apply(R value) {
        return value * static_cast<R>(Num);
    }
};
template <int64_t Den> struct cast_count<1, Den> {
    template <class R> static R apply(R value) {
        return value / static_cast<R>(Den);
    }
};
template <> struct cast_count<1, 1> {
    template <class R> static R apply(R value) {
        return value;
    }
};
} // namespace detail
template <class To, class Rep, class Period> To duration_cast(const duration<Rep, Period>& value) {
    typedef typename To::period TP;
    static const int64_t g1 = gcd<Period::num, TP::num>::value;
    static const int64_t g2 = gcd<TP::den, Period::den>::value;
    static const int64_t n = (Period::num / g1) * (TP::den / g2);
    static const int64_t d = (Period::den / g2) * (TP::num / g1);
    typedef typename std::common_type<
        Rep, typename std::common_type<typename To::rep, int64_t>::type>::type CR;
    return To(static_cast<typename To::rep>(
        detail::cast_count<n, d>::apply(static_cast<CR>(value.count()))));
}
template <class R1, class P1, class R2, class P2>
typename common_duration<R1, P1, R2, P2>::type operator+(const duration<R1, P1>& a,
                                                         const duration<R2, P2>& b) {
    typedef typename common_duration<R1, P1, R2, P2>::type C;
    return C(C(a).count() + C(b).count());
}
template <class R1, class P1, class R2, class P2>
typename common_duration<R1, P1, R2, P2>::type operator-(const duration<R1, P1>& a,
                                                         const duration<R2, P2>& b) {
    typedef typename common_duration<R1, P1, R2, P2>::type C;
    return C(C(a).count() - C(b).count());
}
#define SHIPYARD_CHRONO_COMPARE(op)                                                                \
    template <class R1, class P1, class R2, class P2>                                              \
    bool operator op(const duration<R1, P1>& a, const duration<R2, P2>& b) {                       \
        typedef typename common_duration<R1, P1, R2, P2>::type C;                                  \
        return C(a).count() op C(b).count();                                                       \
    }
SHIPYARD_CHRONO_COMPARE(==)
SHIPYARD_CHRONO_COMPARE(!=)
SHIPYARD_CHRONO_COMPARE(<)
SHIPYARD_CHRONO_COMPARE(>)
SHIPYARD_CHRONO_COMPARE(<=)
SHIPYARD_CHRONO_COMPARE(>=)
#undef SHIPYARD_CHRONO_COMPARE
typedef duration<int64_t, ratio<1, 1000000000> > nanoseconds;
typedef duration<int64_t, ratio<1, 1000000> > microseconds;
typedef duration<int64_t, ratio<1, 1000> > milliseconds;
typedef duration<int64_t> seconds;
typedef duration<int64_t, ratio<60> > minutes;
typedef duration<int64_t, ratio<3600> > hours;

template <class Clock, class Duration = typename Clock::duration> class time_point {
public:
    typedef Clock clock;
    typedef Duration duration;
    typedef typename duration::rep rep;
    typedef typename duration::period period;
    time_point() : elapsed_(duration::zero()) {
    }
    explicit time_point(const duration& elapsed) : elapsed_(elapsed) {
    }
    template <class D>
    time_point(const time_point<Clock, D>& other,
               typename std::enable_if<std::is_convertible<D, Duration>::value>::type* = 0)
        : elapsed_(other.time_since_epoch()) {
    }
    duration time_since_epoch() const {
        return elapsed_;
    }
    time_point& operator+=(const duration& d) {
        elapsed_ += d;
        return *this;
    }
    time_point& operator-=(const duration& d) {
        elapsed_ -= d;
        return *this;
    }

private:
    duration elapsed_;
};
template <class D1, class D2> struct common_d {
    typedef typename common_duration<typename D1::rep, typename D1::period, typename D2::rep,
                                     typename D2::period>::type type;
};
template <class Clock, class D1, class R, class P>
time_point<Clock, typename common_d<D1, duration<R, P> >::type>
operator+(const time_point<Clock, D1>& t, const duration<R, P>& d) {
    typedef typename common_d<D1, duration<R, P> >::type C;
    return time_point<Clock, C>(t.time_since_epoch() + d);
}
template <class Clock, class D1, class R, class P>
time_point<Clock, typename common_d<D1, duration<R, P> >::type>
operator-(const time_point<Clock, D1>& t, const duration<R, P>& d) {
    typedef typename common_d<D1, duration<R, P> >::type C;
    return time_point<Clock, C>(t.time_since_epoch() - d);
}
template <class Clock, class D1, class D2>
typename common_d<D1, D2>::type operator-(const time_point<Clock, D1>& a,
                                          const time_point<Clock, D2>& b) {
    return a.time_since_epoch() - b.time_since_epoch();
}
#define SHIPYARD_CHRONO_TP_COMPARE(op)                                                             \
    template <class C, class D1, class D2>                                                         \
    bool operator op(const time_point<C, D1>& a, const time_point<C, D2>& b) {                     \
        return a.time_since_epoch() op b.time_since_epoch();                                       \
    }
SHIPYARD_CHRONO_TP_COMPARE(==)
SHIPYARD_CHRONO_TP_COMPARE(!=)
SHIPYARD_CHRONO_TP_COMPARE(<)
SHIPYARD_CHRONO_TP_COMPARE(>)
SHIPYARD_CHRONO_TP_COMPARE(<=)
SHIPYARD_CHRONO_TP_COMPARE(>=)
#undef SHIPYARD_CHRONO_TP_COMPARE
struct system_clock {
    typedef Clock::duration<int64_t, ratio<1, 10000000> > duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef Clock::time_point<system_clock> time_point;
    static const bool is_steady = false;
    static time_point now();
    static std::time_t to_time_t(const time_point& point) {
        return static_cast<std::time_t>(duration_cast<seconds>(point.time_since_epoch()).count());
    }
    static time_point from_time_t(std::time_t value) {
        return time_point(duration_cast<duration>(seconds(value)));
    }
};
struct steady_clock {
    typedef nanoseconds duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef Clock::time_point<steady_clock> time_point;
    static const bool is_steady = true;
    static time_point now();
};
} // namespace Clock
} // namespace Shipyard
