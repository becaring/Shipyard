#ifndef SHIPYARD_LOGGING_FORMAT_TYPES_H
#define SHIPYARD_LOGGING_FORMAT_TYPES_H

#ifdef _XBOX
#include <ppcintrinsics.h>
namespace Shipyard { namespace Logging {
inline unsigned CountLeadingZeros32(unsigned value) { return _CountLeadingZeros(value); }
inline unsigned CountLeadingZeros64(unsigned __int64 value) {
    const unsigned high = static_cast<unsigned>(value >> 32);
    return high ? CountLeadingZeros32(high) : 32 + CountLeadingZeros32(static_cast<unsigned>(value));
}
} }
#define FMT_BUILTIN_CLZ(value) Shipyard::Logging::CountLeadingZeros32(value)
#define FMT_BUILTIN_CLZLL(value) Shipyard::Logging::CountLeadingZeros64(value)
#endif

#define FMT_USE_WINDOWS_H 0
#define FMT_USE_VARIADIC_TEMPLATES 0
#define FMT_HEADER_ONLY 1
#include <fmt/format.h>

#include "../async/native.h"
#include "../atomic/atomic.h"
#include "../clock/chrono.h"
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fmt {
template <class T> inline const void* ptr(T* value) { return static_cast<const void*>(value); }
inline const void* ptr(const void* value) { return value; }
}

namespace Shipyard { namespace Logging {
namespace chrono = Shipyard::Clock;
using Shipyard::Async::condition_variable;
using Shipyard::Async::joining_thread;
using Shipyard::Async::lock_guard;
using Shipyard::Async::mutex;
using Shipyard::Async::unique_lock;

class logging_recursive_mutex {
public:
    logging_recursive_mutex() { InitializeCriticalSection(&section_); }
    ~logging_recursive_mutex() { DeleteCriticalSection(&section_); }
    void lock() { EnterCriticalSection(&section_); }
    void unlock() { LeaveCriticalSection(&section_); }
    bool try_lock() { return TryEnterCriticalSection(&section_) != 0; }
private:
    logging_recursive_mutex(const logging_recursive_mutex&);
    logging_recursive_mutex& operator=(const logging_recursive_mutex&);
    CRITICAL_SECTION section_;
};

class logging_view : public fmt::StringRef {
public:
    logging_view() : fmt::StringRef("", 0) {}
    logging_view(const char* value, size_t length) : fmt::StringRef(value, length) {}
    logging_view(const char* value) : fmt::StringRef(value) {}
    logging_view(const std::string& value) : fmt::StringRef(value) {}
    const char* begin() const { return data(); }
    const char* end() const { return data() + size(); }
};

class logging_buffer : public fmt::internal::MemoryBuffer<char, 250> {
    typedef fmt::internal::MemoryBuffer<char, 250> Base;
public:
    logging_buffer() {}
    char* data() { return ptr_; }
    const char* data() const { return ptr_; }
    char* begin() { return data(); }
    const char* begin() const { return data(); }
    char* end() { return data() + size(); }
    const char* end() const { return data() + size(); }
    logging_buffer(logging_buffer&& value) : Base(std::move(value)) {
        Base empty;
        static_cast<Base&>(value) = std::move(empty);
    }
    logging_buffer& operator=(logging_buffer&& value) {
        if (this != &value) {
            Base::operator=(std::move(value));
            Base empty;
            static_cast<Base&>(value) = std::move(empty);
        }
        return *this;
    }
private:
    logging_buffer(const logging_buffer&);
    logging_buffer& operator=(const logging_buffer&);
};

template <class C> typename C::iterator LoggingBegin(C& value) { return value.begin(); }
template <class C> typename C::const_iterator LoggingBegin(const C& value) { return value.begin(); }
template <class C> typename C::iterator LoggingEnd(C& value) { return value.end(); }
template <class C> typename C::const_iterator LoggingEnd(const C& value) { return value.end(); }
template <class T, size_t N> T* LoggingBegin(T (&value)[N]) { return value; }
template <class T, size_t N> T* LoggingEnd(T (&value)[N]) { return value + N; }

inline void LoggingThreadEntry(void* context) {
    std::unique_ptr<std::function<void()> > call(static_cast<std::function<void()>*>(context));
    (*call)();
}
inline joining_thread LoggingMakeThread(const std::function<void()>& call) {
    std::unique_ptr<std::function<void()> > owned(new std::function<void()>(call));
    joining_thread thread(&LoggingThreadEntry, owned.get());
    owned.release();
    return std::move(thread);
}
} }
#endif
