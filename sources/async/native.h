#ifndef SHIPYARD_ASYNC_NATIVE_H
#define SHIPYARD_ASYNC_NATIVE_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(_XBOX)
#include <xtl.h>
#else
#include <windows.h>
#endif
#include <stdexcept>
#include <utility>

namespace Shipyard { namespace Async {

class native_error : public std::runtime_error {
public:
    native_error(const char* operation, DWORD error) : std::runtime_error(operation), error_(error) {}
    DWORD native_code() const { return error_; }
private:
    DWORD error_;
};

// Like std::mutex, callers must not recursively lock or destroy a held mutex.
class mutex {
public:
    mutex();
    ~mutex();
    void lock();
    bool try_lock();
    void unlock();
private:
    mutex(const mutex&);
    mutex& operator=(const mutex&);
    CRITICAL_SECTION section_;
};

template<class Mutex> class lock_guard {
public:
    explicit lock_guard(Mutex& target) : target_(target) { target_.lock(); }
    ~lock_guard() { target_.unlock(); }
private:
    lock_guard(const lock_guard&);
    lock_guard& operator=(const lock_guard&);
    Mutex& target_;
};

template<class Mutex> class unique_lock {
public:
    explicit unique_lock(Mutex& target) : target_(target), owns_(false) { lock(); }
    ~unique_lock() { if (owns_) target_.unlock(); }
    void lock() {
        if (owns_) throw std::logic_error("unique_lock already owns mutex");
        target_.lock(); owns_ = true;
    }
    void unlock() {
        if (!owns_) throw std::logic_error("unique_lock does not own mutex");
        target_.unlock(); owns_ = false;
    }
    bool owns_lock() const { return owns_; }
private:
    unique_lock(const unique_lock&);
    unique_lock& operator=(const unique_lock&);
    Mutex& target_;
    bool owns_;
};

// Persistent broadcast, not a condition variable. Callers own reset policy.
class manual_reset_event {
public:
    explicit manual_reset_event(bool signaled = false);
    ~manual_reset_event();
    void set();
    void reset();
    void wait() const;
    bool wait_for_ms(DWORD milliseconds) const;
private:
    manual_reset_event(const manual_reset_event&);
    manual_reset_event& operator=(const manual_reset_event&);
    HANDLE event_;
};

// Native-event waiter list. Registration and mutex release cannot lose a notify.
class condition_variable {
public:
    condition_variable();
    void notify_one();
    void notify_all();
    void wait(unique_lock<mutex>& lock);
    template<class Predicate> void wait(unique_lock<mutex>& lock, Predicate predicate) {
        while (!predicate()) wait(lock);
    }
private:
    condition_variable(const condition_variable&);
    condition_variable& operator=(const condition_variable&);
    struct waiter;
    void remove(waiter* target);
    mutex gate_;
    waiter* first_;
};

// Owns one native worker; joins on destruction/replacement. No detach API.
// Callback context must remain alive until join. Uncaught callbacks terminate.
class joining_thread {
public:
    typedef void (*entry_point)(void*);
    joining_thread();
    joining_thread(entry_point entry, void* context);
#ifdef _XBOX
    // Pin in the entry trampoline before invoking the user callback.
    joining_thread(entry_point entry, void* context, unsigned processor);
#endif
    joining_thread(joining_thread&& other);
    joining_thread& operator=(joining_thread&& other);
    ~joining_thread();
    bool joinable() const;
    void join();
private:
    joining_thread(const joining_thread&);
    joining_thread& operator=(const joining_thread&);
    struct invocation;
    static DWORD WINAPI run(void* context);
    void join_on_destruction();
    HANDLE thread_;
    DWORD thread_id_;
};

} } // namespace Shipyard::Async
#endif
