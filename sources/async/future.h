#ifndef SHIPYARD_ASYNC_FUTURE_H
#define SHIPYARD_ASYNC_FUTURE_H

#include "native.h"
#include <exception>
#include <memory>
#include <type_traits>

namespace Shipyard { namespace Async {

// Reached duration contract: modern SoH extraction polls milliseconds(0).
// This is a bounded duration adapter, not a replacement for all std::chrono.
class milliseconds {
public:
    explicit milliseconds(long long count) : count_(count) {}
    long long count() const { return count_; }
private:
    long long count_;
};
struct future_status { enum Value { ready, timeout, deferred }; };

enum future_errc { broken_promise, future_already_retrieved, promise_already_satisfied, no_state };

class future_error : public std::logic_error {
public:
    explicit future_error(future_errc code) : std::logic_error(message(code)), code_(code) {}
    future_errc code() const { return code_; }
private:
    static const char* message(future_errc code) {
        switch (code) {
        case broken_promise: return "broken promise";
        case future_already_retrieved: return "future already retrieved";
        case promise_already_satisfied: return "promise already satisfied";
        default: return "future has no state";
        }
    }
    future_errc code_;
};

template<class T> class promise;
template<class T> class future;
template<class T> class shared_future;

namespace detail {
inline std::exception_ptr broken_promise_exception() {
#if defined(_XBOX)
    return std::copy_exception(future_error(broken_promise));
#else
    return std::make_exception_ptr(future_error(broken_promise));
#endif
}

template<class T> struct result_storage { std::unique_ptr<T> value; };
template<> struct result_storage<void> {};

template<class T> struct shared_state : result_storage<T> {
    static_assert(!std::is_reference<T>::value,
                  "Shipyard::Async futures do not support reference results");
    mutex gate;
    manual_reset_event completion;
    bool ready;
    bool retrieved;
    std::exception_ptr exception;
    // Allocate the abandonment error at construction, never in the destructor.
    std::exception_ptr abandoned;

    shared_state() : ready(false), retrieved(false), abandoned(broken_promise_exception()) {}

    void check_pending() const {
        if (ready) throw future_error(promise_already_satisfied);
    }
    void publish() {
        ready = true;
        // Publish while holding gate; readers acquire it after the blocking wait.
        completion.set();
    }
    void abandon() {
        lock_guard<mutex> lock(gate);
        if (!ready) {
            exception = abandoned;
            publish();
        }
    }
    void wait() {
        completion.wait();
        lock_guard<mutex> lock(gate);
        // The event is never reset; the payload is immutable after publication.
    }
    future_status::Value wait_for(milliseconds duration) {
        long long remaining = duration.count();
        if (remaining < 0) remaining = 0;
        for (;;) {
            // INFINITE is a sentinel, never a finite timeout. Long durations
            // are split into finite waits without narrowing or overflow.
            DWORD interval = remaining > 0xfffffffeLL ? 0xfffffffeUL : static_cast<DWORD>(remaining);
            if (completion.wait_for_ms(interval)) {
                lock_guard<mutex> lock(gate);
                return future_status::ready;
            }
            if (remaining <= interval) return future_status::timeout;
            remaining -= interval;
        }
    }
    void rethrow_failure() const {
        if (!(exception == std::exception_ptr())) std::rethrow_exception(exception);
    }
};
} // namespace detail

template<class T> class future {
public:
    future() {}
    future(future&& other) : state_(std::move(other.state_)) {}
    future& operator=(future&& other) {
        if (this != &other) state_ = std::move(other.state_);
        return *this;
    }
    bool valid() const { return state_.get() != NULL; }
    void wait() const { require_state(); state_->wait(); }
    future_status::Value wait_for(milliseconds duration) const { require_state(); return state_->wait_for(duration); }
    T get() {
        require_state();
        std::shared_ptr<detail::shared_state<T> > owned;
        owned.swap(state_);
        owned->wait();
        owned->rethrow_failure();
        return std::move(*owned->value);
    }
    shared_future<T> share();
private:
    future(const future&);
    future& operator=(const future&);
    explicit future(const std::shared_ptr<detail::shared_state<T> >& state) : state_(state) {}
    void require_state() const { if (!valid()) throw future_error(no_state); }
    std::shared_ptr<detail::shared_state<T> > state_;
    friend class promise<T>;
    friend class shared_future<T>;
};

template<class T> class shared_future {
public:
    shared_future() {}
    shared_future(const shared_future& other) : state_(other.state_) {}
    shared_future& operator=(const shared_future& other) { state_ = other.state_; return *this; }
    shared_future(shared_future&& other) : state_(std::move(other.state_)) {}
    shared_future& operator=(shared_future&& other) {
        if (this != &other) state_ = std::move(other.state_);
        return *this;
    }
    shared_future(future<T>&& other) : state_(std::move(other.state_)) {}
    bool valid() const { return state_.get() != NULL; }
    void wait() const { require_state(); state_->wait(); }
    future_status::Value wait_for(milliseconds duration) const { require_state(); return state_->wait_for(duration); }
    const T& get() const {
        wait();
        state_->rethrow_failure();
        return *state_->value;
    }
private:
    void require_state() const { if (!valid()) throw future_error(no_state); }
    std::shared_ptr<detail::shared_state<T> > state_;
};

template<class T> shared_future<T> future<T>::share() {
    return shared_future<T>(std::move(*this));
}

template<class T> class promise {
public:
    promise() : state_(new detail::shared_state<T>()) {}
    promise(promise&& other) : state_(std::move(other.state_)) {}
    promise& operator=(promise&& other) {
        if (this != &other) {
            abandon();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    ~promise() { abandon(); }
    future<T> get_future() {
        require_state();
        lock_guard<mutex> lock(state_->gate);
        if (state_->retrieved) throw future_error(future_already_retrieved);
        state_->retrieved = true;
        return future<T>(state_);
    }
    void set_value(const T& value) {
        require_state();
        lock_guard<mutex> lock(state_->gate);
        state_->check_pending();
        state_->value.reset(new T(value));
        state_->publish();
    }
    void set_value(T&& value) {
        require_state();
        lock_guard<mutex> lock(state_->gate);
        state_->check_pending();
        state_->value.reset(new T(std::move(value)));
        state_->publish();
    }
    void set_exception(std::exception_ptr exception) {
        require_state();
        if (exception == std::exception_ptr()) throw std::invalid_argument("exception must not be null");
        lock_guard<mutex> lock(state_->gate);
        state_->check_pending();
        state_->exception = exception;
        state_->publish();
    }
private:
    promise(const promise&);
    promise& operator=(const promise&);
    void require_state() const { if (!state_) throw future_error(no_state); }
    void abandon() {
        if (state_) {
            try { state_->abandon(); }
            catch (...) { std::terminate(); }
        }
    }
    std::shared_ptr<detail::shared_state<T> > state_;
};

template<> class future<void> {
public:
    future() {}
    future(future&& other) : state_(std::move(other.state_)) {}
    future& operator=(future&& other) {
        if (this != &other) state_ = std::move(other.state_);
        return *this;
    }
    bool valid() const { return state_.get() != NULL; }
    void wait() const { require_state(); state_->wait(); }
    future_status::Value wait_for(milliseconds duration) const { require_state(); return state_->wait_for(duration); }
    void get() {
        require_state();
        std::shared_ptr<detail::shared_state<void> > owned;
        owned.swap(state_);
        owned->wait(); owned->rethrow_failure();
    }
    shared_future<void> share();
private:
    future(const future&);
    future& operator=(const future&);
    explicit future(const std::shared_ptr<detail::shared_state<void> >& state) : state_(state) {}
    void require_state() const { if (!valid()) throw future_error(no_state); }
    std::shared_ptr<detail::shared_state<void> > state_;
    friend class promise<void>;
    friend class shared_future<void>;
};

template<> class shared_future<void> {
public:
    shared_future() {}
    shared_future(const shared_future& other) : state_(other.state_) {}
    shared_future& operator=(const shared_future& other) { state_ = other.state_; return *this; }
    shared_future(shared_future&& other) : state_(std::move(other.state_)) {}
    shared_future& operator=(shared_future&& other) {
        if (this != &other) state_ = std::move(other.state_);
        return *this;
    }
    shared_future(future<void>&& other) : state_(std::move(other.state_)) {}
    bool valid() const { return state_.get() != NULL; }
    void wait() const { require_state(); state_->wait(); }
    future_status::Value wait_for(milliseconds duration) const { require_state(); return state_->wait_for(duration); }
    void get() const { wait(); state_->rethrow_failure(); }
private:
    void require_state() const { if (!valid()) throw future_error(no_state); }
    std::shared_ptr<detail::shared_state<void> > state_;
};

inline shared_future<void> future<void>::share() { return shared_future<void>(std::move(*this)); }

template<> class promise<void> {
public:
    promise() : state_(new detail::shared_state<void>()) {}
    promise(promise&& other) : state_(std::move(other.state_)) {}
    promise& operator=(promise&& other) {
        if (this != &other) { abandon(); state_ = std::move(other.state_); }
        return *this;
    }
    ~promise() { abandon(); }
    future<void> get_future() {
        require_state();
        lock_guard<mutex> lock(state_->gate);
        if (state_->retrieved) throw future_error(future_already_retrieved);
        state_->retrieved = true;
        return future<void>(state_);
    }
    void set_value() {
        require_state();
        lock_guard<mutex> lock(state_->gate);
        state_->check_pending(); state_->publish();
    }
    void set_exception(std::exception_ptr exception) {
        require_state();
        if (exception == std::exception_ptr()) throw std::invalid_argument("exception must not be null");
        lock_guard<mutex> lock(state_->gate);
        state_->check_pending(); state_->exception = exception; state_->publish();
    }
private:
    promise(const promise&);
    promise& operator=(const promise&);
    void require_state() const { if (!state_) throw future_error(no_state); }
    void abandon() {
        if (state_) { try { state_->abandon(); } catch (...) { std::terminate(); } }
    }
    std::shared_ptr<detail::shared_state<void> > state_;
};

} } // namespace Shipyard::Async
#endif
