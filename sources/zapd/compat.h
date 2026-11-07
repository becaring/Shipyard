// Forced include for the projected ZAPD and OTRExporter units. On the XDK it
// supplies the C++11/14 library entry points their sources call; host parity
// builds use the native library and share only the range and logging hooks.
#ifndef SHIPYARD_ZAPD_COMPAT_H
#define SHIPYARD_ZAPD_COMPAT_H
#include <string>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include <math.h>
#include <limits>
#include <prism/compat.h>
#include <filesystem/filesystem.h>
#include <zapd/chrono.h>

// Range-for lowering visits the filesystem shim's iterators through free begin/end.
inline xbox_compat::filesystem::directory_iterator PrismBegin(xbox_compat::filesystem::directory_iterator& value) {
    return value;
}
inline xbox_compat::filesystem::directory_iterator PrismEnd(xbox_compat::filesystem::directory_iterator&) {
    return xbox_compat::filesystem::directory_iterator();
}
inline xbox_compat::filesystem::recursive_directory_iterator PrismBegin(
    xbox_compat::filesystem::recursive_directory_iterator& value) {
    return value;
}
inline xbox_compat::filesystem::recursive_directory_iterator PrismEnd(
    xbox_compat::filesystem::recursive_directory_iterator&) {
    return xbox_compat::filesystem::recursive_directory_iterator();
}

// Exporter diagnostics take a preformatted message or a format and one value.
void ShipyardZapdLog(const std::string& message);
template <class T> void ShipyardZapdLog(const char* format, const T&) { ShipyardZapdLog(std::string(format)); }

// Seconds spent in each extraction stage, which Shipyard::Zapd::Extract reports.
enum ShipyardZapdStage {
    ZapdStageConfig, ZapdStageRom, ZapdStageXml, ZapdStageParse, ZapdStageExport, ZapdStageArchive, ZapdStageFree,
    ZapdStageFinish, ZapdStageCount
};
extern double gShipyardZapdSeconds[ZapdStageCount];
double ShipyardZapdClock();
struct ShipyardZapdTimer {
    int stage;
    double start;
    explicit ShipyardZapdTimer(int value) : stage(value), start(ShipyardZapdClock()) {}
    ~ShipyardZapdTimer() { Stop(); }
    void Stop() {
        if (stage >= 0)
            gShipyardZapdSeconds[stage] += ShipyardZapdClock() - start;
        stage = -1;
    }
};

// ZAPD reads through fopen and fstreams, which take only native absolute paths on the XDK.
inline std::string ShipyardZapdNative(const xbox_compat::filesystem::path& value) {
    std::error_code error;
    std::string native = xbox_compat::filesystem::to_native_path(value, error);
    return error ? value.string() : native;
}

#ifdef _XBOX
#include <crt/format.h>
#define PRIi64 "lli"
#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIX64 "llX"
#define PRIx64 "llx"
#ifndef NAN
#define NAN (std::numeric_limits<float>::quiet_NaN())
#endif

// C++11 math promotes integer arguments to double.
inline double sqrt(int value) { return sqrt(static_cast<double>(value)); }
inline double sqrt(unsigned int value) { return sqrt(static_cast<double>(value)); }
inline double sqrt(long value) { return sqrt(static_cast<double>(value)); }
inline double sqrt(unsigned long value) { return sqrt(static_cast<double>(value)); }

namespace std {
inline unsigned long long strtoull(const char* text, char** end, int base) { return _strtoui64(text, end, base); }
inline long long strtoll(const char* text, char** end, int base) { return _strtoi64(text, end, base); }
inline bool isnan(double value) { return _isnan(value) != 0; }

// The XDK library declares only the long long, unsigned long long, and long double forms.
inline string to_string(int value) { return to_string(static_cast<long long>(value)); }
inline string to_string(unsigned int value) { return to_string(static_cast<unsigned long long>(value)); }
inline string to_string(long value) { return to_string(static_cast<long long>(value)); }
inline string to_string(unsigned long value) { return to_string(static_cast<unsigned long long>(value)); }
inline string to_string(double value) { return to_string(static_cast<long double>(value)); }
inline string to_string(float value) { return to_string(static_cast<long double>(value)); }

template <class T> struct shipyard_unique_if { typedef unique_ptr<T> single; };
template <class T> struct shipyard_unique_if<T[]> { typedef unique_ptr<T[]> array; };
template <class T> typename shipyard_unique_if<T>::single make_unique() { return unique_ptr<T>(new T()); }
template <class T, class A> typename shipyard_unique_if<T>::single make_unique(A&& a) {
    return unique_ptr<T>(new T(std::forward<A>(a)));
}
template <class T, class A, class B> typename shipyard_unique_if<T>::single make_unique(A&& a, B&& b) {
    return unique_ptr<T>(new T(std::forward<A>(a), std::forward<B>(b)));
}
template <class T, class A, class B, class C>
typename shipyard_unique_if<T>::single make_unique(A&& a, B&& b, C&& c) {
    return unique_ptr<T>(new T(std::forward<A>(a), std::forward<B>(b), std::forward<C>(c)));
}
template <class T> typename shipyard_unique_if<T>::array make_unique(size_t count) {
    typedef typename remove_extent<T>::type Element;
    return unique_ptr<T>(new Element[count]());
}
} // namespace std
#else
#include <cinttypes>
#endif
#endif
