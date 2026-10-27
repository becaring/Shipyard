#include "native.h"
#include <exception>
#include <memory>

namespace Shipyard {
namespace Async {

mutex::mutex() {
    InitializeCriticalSection(&section_);
}
mutex::~mutex() {
    DeleteCriticalSection(&section_);
}
void mutex::lock() {
    EnterCriticalSection(&section_);
}
bool mutex::try_lock() {
    return TryEnterCriticalSection(&section_) != FALSE;
}
void mutex::unlock() {
    LeaveCriticalSection(&section_);
}

manual_reset_event::manual_reset_event(bool signaled)
    : event_(CreateEvent(NULL, TRUE, signaled ? TRUE : FALSE, NULL)) {
    if (!event_)
        throw native_error("CreateEvent", GetLastError());
}
manual_reset_event::~manual_reset_event() {
    CloseHandle(event_);
}
void manual_reset_event::set() {
    if (!SetEvent(event_))
        throw native_error("SetEvent", GetLastError());
}
void manual_reset_event::reset() {
    if (!ResetEvent(event_))
        throw native_error("ResetEvent", GetLastError());
}
void manual_reset_event::wait() const {
    if (WaitForSingleObject(event_, INFINITE) != WAIT_OBJECT_0)
        throw native_error("WaitForSingleObject", GetLastError());
}
bool manual_reset_event::wait_for_ms(DWORD milliseconds) const {
    if (milliseconds == INFINITE)
        throw std::invalid_argument("finite event timeout required");
    DWORD result = WaitForSingleObject(event_, milliseconds);
    if (result == WAIT_OBJECT_0)
        return true;
    if (result == WAIT_TIMEOUT)
        return false;
    throw native_error("WaitForSingleObject", GetLastError());
}

// Condition waits reuse events instead of creating one per wait. A managed
// thread's scope owns one event; nested or unmanaged waits borrow from a small
// pool, and fall back to a private event when the pool is exhausted.
namespace {
struct WaitEventScope;
__declspec(thread) WaitEventScope* activeWaitEventScope = NULL;
struct WaitEventScope {
    std::unique_ptr<manual_reset_event> event;
    bool busy;
    WaitEventScope* previous;
    WaitEventScope() : busy(false), previous(activeWaitEventScope) { activeWaitEventScope = this; }
    ~WaitEventScope() { activeWaitEventScope = previous; }
};
struct WaitEventPool {
    enum { Capacity = 16 };
    mutex gate;
    manual_reset_event* events[Capacity];
    bool busy[Capacity];
    WaitEventPool() {
        for (unsigned i = 0; i < Capacity; ++i) { events[i] = NULL; busy[i] = false; }
    }
    manual_reset_event* acquire(unsigned& slot) {
        lock_guard<mutex> lock(gate);
        for (unsigned i = 0; i < Capacity; ++i) {
            if (busy[i]) continue;
            if (!events[i]) events[i] = new manual_reset_event();
            busy[i] = true;
            slot = i;
            return events[i];
        }
        return NULL;
    }
    void release(unsigned slot) { lock_guard<mutex> lock(gate); busy[slot] = false; }
};
static void* volatile waitEventPoolPointer = NULL;
static WaitEventPool* GetWaitEventPool() {
    WaitEventPool* pool = static_cast<WaitEventPool*>(InterlockedCompareExchangePointer(&waitEventPoolPointer, NULL, NULL));
    if (!pool) {
        std::unique_ptr<WaitEventPool> candidate(new WaitEventPool());
        pool = static_cast<WaitEventPool*>(InterlockedCompareExchangePointer(&waitEventPoolPointer, candidate.get(), NULL));
        if (!pool) pool = candidate.release();
    }
    // At most sixteen handles survive until process exit, including main-thread waits.
    // No static destructor can race a different module's shutdown waits.
    return pool;
}
}

struct condition_variable::waiter {
    union { unsigned char bytes[sizeof(manual_reset_event)]; void* alignment; } owned;
    manual_reset_event* event;
    WaitEventScope* borrowed;
    WaitEventPool* pool;
    unsigned poolSlot;
    waiter* next;
    waiter() : event(NULL), borrowed(NULL), pool(NULL), poolSlot(0), next(NULL) {
        WaitEventScope* scope = activeWaitEventScope;
        if (scope && !scope->busy) {
            if (!scope->event.get()) scope->event.reset(new manual_reset_event());
            else scope->event->reset();
            scope->busy = true;
            borrowed = scope;
            event = scope->event.get();
        } else {
            pool = GetWaitEventPool();
            event = pool->acquire(poolSlot);
            if (event) {
                try { event->reset(); }
                catch (...) { pool->release(poolSlot); throw; }
            } else {
                pool = NULL;
                event = new (owned.bytes) manual_reset_event();
            }
        }
    }
    ~waiter() {
        if (borrowed) borrowed->busy = false;
        else if (pool) pool->release(poolSlot);
        else event->~manual_reset_event();
    }
};

condition_variable::condition_variable() : first_(NULL) {
}
void condition_variable::remove(waiter* target) {
    waiter** link = &first_;
    while (*link && *link != target)
        link = &(*link)->next;
    if (*link)
        *link = target->next;
}
void condition_variable::wait(unique_lock<mutex>& lock) {
    if (!lock.owns_lock())
        throw std::logic_error("condition wait requires owned mutex");
    waiter entry;
    {
        lock_guard<mutex> guard(gate_);
        entry.next = first_;
        first_ = &entry;
        lock.unlock();
    }
    try {
        entry.event->wait();
    } catch (...) {
        {
            lock_guard<mutex> guard(gate_);
            remove(&entry);
        }
        lock.lock();
        throw;
    }
    // Also synchronizes waiter destruction with notify's SetEvent call.
    {
        lock_guard<mutex> guard(gate_);
        remove(&entry);
    }
    lock.lock();
}
void condition_variable::notify_one() {
    lock_guard<mutex> guard(gate_);
    if (first_) {
        waiter* target = first_;
        target->event->set();
        first_ = target->next;
    }
}
void condition_variable::notify_all() {
    lock_guard<mutex> guard(gate_);
    while (first_) {
        waiter* target = first_;
        target->event->set();
        first_ = target->next;
    }
}

struct joining_thread::invocation {
    entry_point entry;
    void* context;
    unsigned processor;
    invocation(entry_point fn, void* arg, unsigned target = (unsigned)-1)
        : entry(fn), context(arg), processor(target) {
    }
};

joining_thread::joining_thread() : thread_(NULL), thread_id_(0) {
}
joining_thread::joining_thread(entry_point entry, void* context) : thread_(NULL), thread_id_(0) {
    if (!entry)
        throw std::invalid_argument("thread entry must not be null");
    std::unique_ptr<invocation> call(new invocation(entry, context));
    // Transfer before launch: the worker may finish before CreateThread returns.
    invocation* raw = call.release();
    thread_ = CreateThread(NULL, 128 * 1024, &joining_thread::run, raw, 0, &thread_id_);
    if (!thread_) {
        DWORD error = GetLastError();
        delete raw;
        throw native_error("CreateThread", error);
    }
}
#ifdef _XBOX
joining_thread::joining_thread(entry_point entry, void* context, unsigned processor)
    : thread_(NULL), thread_id_(0) {
    if (!entry)
        throw std::invalid_argument("thread entry must not be null");
    std::unique_ptr<invocation> call(new invocation(entry, context, processor));
    invocation* raw = call.release();
    thread_ = CreateThread(NULL, 128 * 1024, &joining_thread::run, raw, 0, &thread_id_);
    if (!thread_) {
        DWORD error = GetLastError();
        delete raw;
        throw native_error("CreateThread", error);
    }
}
#endif
DWORD WINAPI joining_thread::run(void* context) {
    std::unique_ptr<invocation> call(static_cast<invocation*>(context));
    entry_point entry = call->entry;
    void* argument = call->context;
#ifdef _XBOX
    const unsigned processor = call->processor;
#endif
    call.reset();
#ifdef _XBOX
    // XDK has no TerminateThread; pin this trampoline before application work
    // instead of creating a suspended worker with no recoverable failure path.
    if (processor != (unsigned)-1 &&
        XSetThreadProcessor(GetCurrentThread(), processor) == (DWORD)-1)
        std::terminate();
#endif
    try {
        WaitEventScope waitEventScope;
        entry(argument);
    } catch (...) {
        std::terminate();
    }
    return 0;
}
joining_thread::joining_thread(joining_thread&& other)
    : thread_(other.thread_), thread_id_(other.thread_id_) {
    other.thread_ = NULL;
    other.thread_id_ = 0;
}
joining_thread& joining_thread::operator=(joining_thread&& other) {
    if (this != &other) {
        join();
        thread_ = other.thread_;
        thread_id_ = other.thread_id_;
        other.thread_ = NULL;
        other.thread_id_ = 0;
    }
    return *this;
}
joining_thread::~joining_thread() {
    join_on_destruction();
}
bool joining_thread::joinable() const {
    return thread_ != NULL;
}
void joining_thread::join() {
    if (!thread_)
        return;
    if (GetCurrentThreadId() == thread_id_)
        throw std::logic_error("thread cannot join itself");
    if (WaitForSingleObject(thread_, INFINITE) != WAIT_OBJECT_0)
        throw native_error("thread join", GetLastError());
    CloseHandle(thread_);
    thread_ = NULL;
    thread_id_ = 0;
}
void joining_thread::join_on_destruction() {
    try {
        join();
    } catch (...) {
        std::terminate();
    }
}

} // namespace Async
} // namespace Shipyard
