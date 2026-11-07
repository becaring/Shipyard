// Forced include for the projected ZAPD and OTRExporter units. On the XDK it
// supplies the C++11/14 library entry points their sources call; host parity
// builds use the native library and share only the range and logging hooks.
#ifndef SHIPYARD_ZAPD_COMPAT_H
#define SHIPYARD_ZAPD_COMPAT_H
#include <string>
#include <memory>
#include <vector>
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

// Runs work(context, worker) on the calling thread as worker 0 and on each helper
// thread the request asks for, then waits for the helpers. work must not throw.
void ShipyardZapdRunParallel(void (*work)(void* context, int worker), void* context);
long ShipyardZapdIncrement(volatile long* value);
// True once free memory is short; the helper asking then takes no more files.
bool ShipyardZapdRetireHelper();
// Held while a thread uses the archive writer the extraction threads share.
struct ShipyardZapdArchiveLock {
    ShipyardZapdArchiveLock();
    ~ShipyardZapdArchiveLock();
};

// Seconds this thread spent in each extraction stage; Shipyard::Zapd::Extract sums the threads.
enum ShipyardZapdStage {
    ZapdStageConfig, ZapdStageRom, ZapdStageXml, ZapdStageParse, ZapdStageExport, ZapdStageArchive, ZapdStageFree,
    ZapdStageFinish, ZapdStageCount
};
extern __declspec(thread) double gShipyardZapdSeconds[ZapdStageCount];
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

// Seconds per resource type within the parse and export stages.
void ShipyardZapdAddTypeSeconds(int stage, const std::string& type, double seconds);
struct ShipyardZapdTypeTimer {
    int stage;
    std::string type;
    double start;
    ShipyardZapdTypeTimer(int value, const std::string& name) : stage(value), type(name), start(ShipyardZapdClock()) {}
    ~ShipyardZapdTypeTimer() { ShipyardZapdAddTypeSeconds(stage, type, ShipyardZapdClock() - start); }
};

// ZAPD reads through fopen and fstreams, which take only native absolute paths on the XDK.
inline std::string ShipyardZapdNative(const xbox_compat::filesystem::path& value) {
    std::error_code error;
    std::string native = xbox_compat::filesystem::to_native_path(value, error);
    return error ? value.string() : native;
}

// An adapter may ship the asset folder as one stored zip, since copying thousands of
// small files is slow; ZAPD's reads and listings check that archive first.
bool ShipyardZapdBundled(const std::string& path, std::string* bytes);
void ShipyardZapdBundledList(const std::string& folder, std::vector<std::string>& files);
template <class Document> int ShipyardZapdLoadXml(Document& doc, const std::string& path) {
    std::string bytes;
    if (ShipyardZapdBundled(path, &bytes))
        return doc.Parse(bytes.data(), bytes.size());
    return doc.LoadFile(ShipyardZapdNative(path).c_str());
}

#ifdef _XBOX
#include <crt/format.h>
#include <stdio.h>
#include <cstdio>
// Nothing reads the extractor's console output, and each stdio call is a debug-channel write here.
#define printf(...) 0
#define fprintf(...) 0
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
