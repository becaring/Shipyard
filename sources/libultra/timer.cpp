#include "libultra/timer.h"
#include "async/native.h"
#include <vector>
#include <memory>

namespace Shipyard { namespace Libultra {
namespace {
class Timer {
public:
    Timer(uint32_t interval, TimerCallback callback, void* context)
        : interval_(interval), callback_(callback), context_(context), worker_(&Timer::Run, this) {}
    ~Timer() { stop_.set(); worker_.join(); }
private:
    static void Run(void* value) {
        Timer* timer=static_cast<Timer*>(value);
        uint32_t interval=timer->interval_;
        while (interval && !timer->stop_.wait_for_ms(interval))
            interval=timer->callback_(interval,timer->context_);
    }
    Timer(const Timer&); Timer& operator=(const Timer&);
    uint32_t interval_; TimerCallback callback_; void* context_;
    Async::manual_reset_event stop_;
    Async::joining_thread worker_;
};
class Timers {
public:
    ~Timers() { Stop(); }
    bool Add(uint32_t interval, TimerCallback callback, void* context) {
        if (!interval || !callback) return false;
        Async::lock_guard<Async::mutex> lock(gate_);
        // Reserve before launching: allocation failure cannot leave a live
        // worker whose callback context is no longer owned by this registry.
        timers_.reserve(timers_.size()+1);
        std::unique_ptr<Timer> timer(new Timer(interval,callback,context));
        timers_.push_back(timer.get()); timer.release(); return true;
    }
    void Stop() {
        Async::lock_guard<Async::mutex> lock(gate_);
        for (size_t i=0;i<timers_.size();++i) delete timers_[i];
        timers_.clear();
    }
private:
    Async::mutex gate_; std::vector<Timer*> timers_;
};
Timers timers;
}
bool AddTimer(uint32_t interval, TimerCallback callback, void* context) {
    try { return timers.Add(interval,callback,context); } catch (...) { return false; }
}
void StopTimers() { timers.Stop(); }
} }
