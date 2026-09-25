#include "async/detached.h"
#include "async/native.h"
#include <memory>
#include <exception>
#include <limits>
namespace Shipyard {
namespace Async {
namespace {
// Constructed before application work because XDK function-local static
// initialization is unsynchronized. Detached workers can finish during CRT
// teardown, so this tracker and its synchronization live for process lifetime.
struct detached_state {
    mutex gate;
    condition_variable changed;
    size_t active;
    detached_state() : active(0) {
    }
};
detached_state& detached = *new detached_state();
void begin_detached() {
    lock_guard<mutex> lock(detached.gate);
    if (detached.active == (std::numeric_limits<size_t>::max)())
        throw std::overflow_error("Detached work count overflow");
    ++detached.active;
}
void finish_detached() {
    lock_guard<mutex> lock(detached.gate);
    --detached.active;
    if (!detached.active)
        detached.changed.notify_all();
}
} // namespace
static DWORD WINAPI run_detached(void* context) {
    std::unique_ptr<std::function<void()> > callable(static_cast<std::function<void()>*>(context));
    try {
        (*callable)();
        // Release captures before publishing completion to waiting owners.
        callable.reset();
        finish_detached();
    } catch (...) {
        std::terminate();
    }
    return 0;
}
void launch_detached(const std::function<void()>& callable) {
    std::unique_ptr<std::function<void()> > ownedCallable(new std::function<void()>(callable));
    begin_detached();
    // A worker can complete before CreateThread returns: transfer first.
    std::function<void()>* workerCallable = ownedCallable.release();
    HANDLE thread = CreateThread(NULL, 128 * 1024, run_detached, workerCallable, 0, NULL);
    if (!thread) {
        DWORD error = GetLastError();
        delete workerCallable;
        finish_detached();
        throw native_error("CreateThread", error);
    }
    // Close the native handle after launch; failure leaves ownership unresolved.
    if (!CloseHandle(thread))
        std::terminate();
}
void wait_for_detached() {
    unique_lock<mutex> lock(detached.gate);
    while (detached.active)
        detached.changed.wait(lock);
}
} // namespace Async
} // namespace Shipyard
