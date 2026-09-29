#ifndef SHIPYARD_BS_NATIVE_SUPPORT_H
#define SHIPYARD_BS_NATIVE_SUPPORT_H
#include "async/future.h"
#include <functional>

namespace Shipyard {
namespace Async {
// Unevaluated operand only; XDK's STL predates std::declval.
template <class T> T&& unevaluated_value();
template <class F> struct invoke_result {
    typedef decltype(unevaluated_value<typename std::decay<F>::type>()()) type;
};
template <class R> struct complete_promise {
    template <class F> static void run(const F& task, const std::shared_ptr<promise<R> >& result) {
        result->set_value(task());
    }
};
template <> struct complete_promise<void> {
    template <class F>
    static void run(const F& task, const std::shared_ptr<promise<void> >& result) {
        task();
        result->set_value();
    }
};
inline void run_function(void* arg) {
    std::unique_ptr<std::function<void()> > owned(static_cast<std::function<void()>*>(arg));
    (*owned)();
}
// Every caller is a pool or service worker (resource and save pools,
// network, randomizer generation). On Xbox, successive workers alternate
// between the service hardware threads instead of inheriting the title
// thread's hardware thread 0. Threads that need a specific hardware thread
// use make_thread_on_processor.
inline joining_thread make_thread(const std::function<void()>& fn) {
    std::unique_ptr<std::function<void()> > owned(new std::function<void()>(fn));
#ifdef _XBOX
    static volatile LONG serviceIndex = -1;
    joining_thread thread(&run_function, owned.get(),
                          ServiceProcessorFor((size_t)InterlockedIncrement(&serviceIndex)));
#else
    joining_thread thread(&run_function, owned.get());
#endif
    owned.release();
    return std::move(thread);
}
#ifdef _XBOX
inline joining_thread make_thread_on_processor(const std::function<void()>& fn,
                                               unsigned processor) {
    std::unique_ptr<std::function<void()> > owned(new std::function<void()>(fn));
    joining_thread thread(&run_function, owned.get(), processor);
    owned.release();
    return std::move(thread);
}
#endif
inline unsigned int hardware_concurrency() {
#ifdef _XBOX
    return MAXIMUM_PROCESSORS; // XDK winnt.h declares six logical hardware threads.
#else
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
#endif
}
} // namespace Async
} // namespace Shipyard
#endif
