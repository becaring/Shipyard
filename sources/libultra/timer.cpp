#include "libultra/timer.h"
#include "async/native.h"
#include <vector>
#include <memory>

namespace Shipyard { namespace Libultra {
namespace {
class Timer {
public:
    Timer(uint32_t interval, TimerCallback callback, void* context)
        : interval_(interval), callback_(callback), context_(context), worker_id_(0), worker_(&Timer::Run, this) {}
    ~Timer() { stop_.set(); worker_.join(); }
    bool on_worker_thread() const {
        return static_cast<DWORD>(InterlockedCompareExchange(&worker_id_, 0, 0)) == GetCurrentThreadId();
    }
private:
    static void Run(void* value) {
        Timer* timer=static_cast<Timer*>(value);
        InterlockedExchange(&timer->worker_id_, static_cast<LONG>(GetCurrentThreadId()));
        uint32_t interval=timer->interval_;
        while (interval && !timer->stop_.wait_for_ms(interval))
            interval=timer->callback_(interval,timer->context_);
    }
    Timer(const Timer&); Timer& operator=(const Timer&);
    uint32_t interval_; TimerCallback callback_; void* context_;
    Async::manual_reset_event stop_;
    mutable volatile LONG worker_id_;
    Async::joining_thread worker_;
};
class Timers {
public:
    Timers() : stopping_(false) {}
    ~Timers() { Stop(); }
    bool Add(uint32_t interval, TimerCallback callback, void* context) {
        if (!interval || !callback) return false;
        Async::lock_guard<Async::mutex> lock(gate_);
        if (stopping_) return false;
        // Reserve before launching: allocation failure cannot leave a live
        // worker whose callback context is no longer owned by this registry.
        timers_.reserve(timers_.size()+1);
        std::unique_ptr<Timer> timer(new Timer(interval,callback,context));
        timers_.push_back(timer.get()); timer.release(); return true;
    }
    bool Stop() {
        std::vector<Timer*> retiring;
        {
            Async::lock_guard<Async::mutex> lock(gate_);
            if (stopping_) return false;
            for (size_t i=0;i<timers_.size();++i)
                if (timers_[i]->on_worker_thread()) return false;
            stopping_=true;
            retiring.swap(timers_);
        }
        for (size_t i=0;i<retiring.size();++i) delete retiring[i];
        {
            Async::lock_guard<Async::mutex> lock(gate_);
            stopping_=false;
        }
        return true;
    }
private:
    Async::mutex gate_; std::vector<Timer*> timers_;
    bool stopping_;
};
Timers timers;
}
bool AddTimer(uint32_t interval, TimerCallback callback, void* context) {
    try { return timers.Add(interval,callback,context); } catch (...) { return false; }
}
bool StopTimers() { return timers.Stop(); }
} }
