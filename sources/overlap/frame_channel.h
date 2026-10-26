// Two game-frame pools transfer immutable packets to the render owner.
// The game owner collects retired storage and queued mutations at idle handoff.
// Stop preserves pointers held by builders and renderers until acknowledgment.
#ifndef SHIPYARD_OVERLAP_FRAME_CHANNEL_H
#define SHIPYARD_OVERLAP_FRAME_CHANNEL_H
#include "async/native.h"
#include <stdint.h>
#include <functional>
#include <deque>
#include <memory>
#include <vector>

namespace Shipyard {
namespace Overlap {
template <class Packet> class FrameChannel {
public:
    typedef std::function<void()> Action;
    struct Ticket { uint64_t generation; unsigned slot; Ticket() : generation(0), slot(0) {} };
    FrameChannel() : issued_(0), nextReady_(1), retired_(0), paused_(false), finished_(false), stopped_(false) {}

    // One producer calls build/collect; one renderer calls acquire/retire/pump.
    // Join both owners and collect retired packets before destroying the channel.
    bool acquireBuild(Ticket& ticket, Packet*& packet) {
        packet = NULL;
        Lock lock(mutex_);
        for (;;) {
            if (stopped_ || finished_) return false;
            const unsigned index = static_cast<unsigned>(issued_ % 2);
            if (paused_ || (slots_[index].state != Empty && slots_[index].state != Retired) ||
                (slots_[index].state == Retired && (activeJob_ || !jobs_.empty()))) {
                changed_.wait(lock);
                continue;
            }
            lock.unlock();
            collectRetired();
            runIdleActions();
            lock.lock();
            if (stopped_ || finished_) return false;
            if (paused_ || slots_[index].state != Empty) continue;
            Slot& slot = slots_[index];
            slot.generation = ++issued_;
            slot.state = Building;
            ticket.generation = slot.generation;
            ticket.slot = index;
            packet = &slot.packet;
            return true;
        }
    }
    bool publish(const Ticket& ticket) {
        Lock lock(mutex_);
        if (!matches(ticket, Building)) return false;
        if (stopped_) {
            retireSlot(slots_[ticket.slot]);
            return false;
        }
        slots_[ticket.slot].state = Ready;
        changed_.notify_all();
        return true;
    }
    bool cancelBuild(const Ticket& ticket) {
        Lock lock(mutex_);
        if (!matches(ticket, Building)) return false;
        retireSlot(slots_[ticket.slot]);
        return true;
    }
    bool tryAcquireReady(Ticket& ticket, const Packet*& packet) {
        Lock lock(mutex_);
        return takeReady(ticket, packet);
    }
    bool acquireReady(Ticket& ticket, const Packet*& packet) {
        packet = NULL;
        Lock lock(mutex_);
        for (;;) {
            if (!jobs_.empty()) {
                lock.unlock();
                pumpRenderJobs();
                lock.lock();
                continue;
            }
            if (takeReady(ticket, packet)) return true;
            if (stopped_ || (finished_ && !hasBuilding()) || (paused_ && drained())) return false;
            changed_.wait(lock);
        }
    }
    bool retire(const Ticket& ticket) {
        Lock lock(mutex_);
        if (!matches(ticket, Rendering)) return false;
        retireSlot(slots_[ticket.slot]);
        return true;
    }
    // Logical removal can happen while N+1 builds; physical frees wait for N.
    void deferRelease(uint64_t generation, const Action& action) {
        Lock lock(mutex_);
        releases_.push_back(Release(generation, action));
    }
    void collectRetired() {
        std::vector<Action> actions;
        {
            Lock lock(mutex_);
            // Render jobs may borrow retired packets or allocations independently of the producer.
            if (hasBuilding() || activeJob_ || !jobs_.empty()) return;
            for (unsigned i = 0; i != 2; ++i) {
                Slot& slot = slots_[i];
                if (slot.state == Retired && slot.generation <= retired_) {
                    slot.packet = Packet();
                    slot.state = Empty;
                }
            }
            for (typename std::deque<Release>::iterator it = releases_.begin(); it != releases_.end();) {
                if (it->generation <= retired_) {
                    actions.push_back(it->action);
                    it = releases_.erase(it);
                } else ++it;
            }
            changed_.notify_all();
        }
        for (size_t i = 0; i != actions.size(); ++i) actions[i]();
    }
    void queueIdleAction(const Action& action) {
        Lock lock(mutex_);
        idleActions_.push_back(action);
        changed_.notify_all();
    }
    void stopFeeder() {
        Lock lock(mutex_);
        paused_ = true;
        changed_.notify_all();
    }
    void resumeFeeder() {
        Lock lock(mutex_);
        if (!finished_ && !stopped_) paused_ = false;
        changed_.notify_all();
    }
    void finishProducer() {
        Lock lock(mutex_);
        finished_ = true;
        changed_.notify_all();
    }
    void requestStop() {
        Lock lock(mutex_);
        stopped_ = true;
        for (unsigned i = 0; i != 2; ++i) {
            if (slots_[i].state == Ready) retireSlot(slots_[i]);
        }
        while (!jobs_.empty()) {
            jobs_.front()->done = true;
            jobs_.front()->cancelled = true;
            jobs_.pop_front();
        }
        changed_.notify_all();
    }
    void waitDrained() {
        Lock lock(mutex_);
        while (!drained()) changed_.wait(lock);
    }
    bool isDrained() {
        Lock lock(mutex_);
        return drained();
    }
    // A producer may fence borrowed memory while retaining its Building slot.
    bool waitRetired(uint64_t generation) {
        Lock lock(mutex_);
        while (!stopped_ && retired_ < generation) changed_.wait(lock);
        return retired_ >= generation;
    }
    uint64_t retiredGeneration() {
        Lock lock(mutex_);
        return retired_;
    }
    // The renderer pumps jobs between replays and inside acquireReady's wait.
    // Callbacks must not throw or wait for the producer.
    bool runOnRender(const Action& action) {
        std::shared_ptr<Job> job(new Job(action));
        Lock lock(mutex_);
        if (stopped_) return false;
        jobs_.push_back(job);
        changed_.notify_all();
        while (!job->done) changed_.wait(lock);
        return !job->cancelled;
    }
    unsigned pumpRenderJobs() {
        unsigned pumped = 0;
        Lock lock(mutex_);
        while (!jobs_.empty()) {
            std::shared_ptr<Job> job = jobs_.front();
            jobs_.pop_front();
            activeJob_ = job;
            lock.unlock();
            try {
                job->action();
            } catch (...) {
                lock.lock();
                job->done = job->cancelled = true;
                activeJob_.reset();
                changed_.notify_all();
                throw;
            }
            lock.lock();
            job->done = true;
            activeJob_.reset();
            ++pumped;
            changed_.notify_all();
        }
        return pumped;
    }
private:
    typedef Shipyard::Async::unique_lock<Shipyard::Async::mutex> Lock;
    enum State { Empty, Building, Ready, Rendering, Retired };
    struct Slot {
        Packet packet;
        uint64_t generation, retiredGeneration;
        State state;
        Slot() : generation(0), retiredGeneration(0), state(Empty) {}
    };
    struct Release {
        uint64_t generation;
        Action action;
        Release(uint64_t value, const Action& callback) : generation(value), action(callback) {}
    };
    struct Job {
        Action action;
        bool done, cancelled;
        explicit Job(const Action& callback) : action(callback), done(false), cancelled(false) {}
    };
    bool matches(const Ticket& ticket, State state) const {
        return ticket.slot < 2 && ticket.generation != 0 &&
            slots_[ticket.slot].generation == ticket.generation && slots_[ticket.slot].state == state;
    }
    bool hasBuilding() const {
        return slots_[0].state == Building || slots_[1].state == Building;
    }
    bool drained() const {
        for (unsigned i = 0; i != 2; ++i) {
            if (slots_[i].state == Building || slots_[i].state == Ready || slots_[i].state == Rendering) return false;
        }
        return jobs_.empty() && !activeJob_;
    }
    void retireSlot(Slot& slot) {
        slot.retiredGeneration = slot.generation;
        slot.state = Retired;
        while (slots_[static_cast<unsigned>(retired_ % 2)].retiredGeneration == retired_ + 1) ++retired_;
        changed_.notify_all();
    }
    bool takeReady(Ticket& ticket, const Packet*& packet) {
        packet = NULL;
        while (nextReady_ <= retired_) ++nextReady_;
        Slot& slot = slots_[static_cast<unsigned>((nextReady_ - 1) % 2)];
        if (stopped_ || slot.state != Ready || slot.generation != nextReady_) return false;
        // A renderer can own only one immutable packet until its final replay.
        if (slots_[0].state == Rendering || slots_[1].state == Rendering) return false;
        ticket.generation = nextReady_++;
        ticket.slot = static_cast<unsigned>((ticket.generation - 1) % 2);
        slot.state = Rendering;
        packet = &slot.packet;
        return true;
    }
    void runIdleActions() {
        std::deque<Action> actions;
        {
            Lock lock(mutex_);
            if (hasBuilding()) return;
            actions.swap(idleActions_);
        }
        for (size_t i = 0; i != actions.size(); ++i) actions[i]();
    }
    FrameChannel(const FrameChannel&);
    FrameChannel& operator=(const FrameChannel&);
    Shipyard::Async::mutex mutex_;
    Shipyard::Async::condition_variable changed_;
    Slot slots_[2];
    uint64_t issued_, nextReady_, retired_;
    bool paused_, finished_, stopped_;
    std::deque<Release> releases_;
    std::deque<Action> idleActions_;
    std::deque<std::shared_ptr<Job> > jobs_;
    std::shared_ptr<Job> activeJob_;
};
class AudioGenerationGate {
public:
    AudioGenerationGate() : pending_(0), completed_(0), stopped_(false) {}
    bool begin(uint64_t generation) {
        Lock lock(mutex_);
        if (stopped_ || pending_ != 0 || generation <= completed_) return false;
        pending_ = generation;
        return true;
    }
    bool complete(uint64_t generation) {
        Lock lock(mutex_);
        if (generation == 0 || generation != pending_) return false;
        completed_ = pending_;
        pending_ = 0;
        changed_.notify_all();
        return true;
    }
    bool wait(uint64_t generation) {
        Lock lock(mutex_);
        while (!stopped_ && completed_ < generation) changed_.wait(lock);
        return completed_ >= generation;
    }
    void requestStop() {
        Lock lock(mutex_);
        stopped_ = true;
        changed_.notify_all();
    }
private:
    typedef Shipyard::Async::unique_lock<Shipyard::Async::mutex> Lock;
    AudioGenerationGate(const AudioGenerationGate&);
    AudioGenerationGate& operator=(const AudioGenerationGate&);
    Shipyard::Async::mutex mutex_;
    Shipyard::Async::condition_variable changed_;
    uint64_t pending_, completed_;
    bool stopped_;
};
}
}

#endif
