#include "chrono.h"
#include "async/native.h"
namespace Shipyard { namespace Clock {
system_clock::time_point system_clock::now(){
    FILETIME file;GetSystemTimeAsFileTime(&file);
    uint64_t ticks=(static_cast<uint64_t>(file.dwHighDateTime)<<32)|file.dwLowDateTime;
    return time_point(duration(static_cast<int64_t>(ticks)-INT64_C(116444736000000000)));
}
steady_clock::time_point steady_clock::now(){
    static __declspec(thread) int64_t frequency = 0;
    if(!frequency){
        LARGE_INTEGER result;
        if(!QueryPerformanceFrequency(&result)||result.QuadPart<=0)
            throw Shipyard::Async::native_error("QueryPerformanceFrequency",GetLastError());
        if(result.QuadPart>INT64_C(9223372036))throw std::overflow_error("QPC frequency exceeds conversion range");
        frequency=result.QuadPart;
    }
    LARGE_INTEGER counter;
    if(!QueryPerformanceCounter(&counter))
        throw Shipyard::Async::native_error("QueryPerformanceCounter",GetLastError());
    // Real XDK/Windows performance frequencies fit this bound; enforce the
    // multiplication precondition rather than silently overflow on other hosts.
    int64_t seconds=counter.QuadPart/frequency,remainder=counter.QuadPart%frequency;
    return time_point(duration(seconds*INT64_C(1000000000)+remainder*INT64_C(1000000000)/frequency));
}
} }
