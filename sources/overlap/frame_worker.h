// One worker owns whole game callbacks and blocks after publishing each packet.
// Render builds GUI at that handoff, then permits the next callback during replay.
// The caller picks the producer's hardware thread; render stays on the caller's.
// Native jobs and replay fences never require render to wait for a building frame.
#ifndef SHIPYARD_OVERLAP_FRAME_WORKER_H
#define SHIPYARD_OVERLAP_FRAME_WORKER_H
#include "overlap/frame_channel.h"
#include <stdexcept>

namespace Shipyard {
namespace Overlap {
template <class Packet> class FrameWorker {
public:
    typedef typename FrameChannel<Packet>::Ticket Ticket;
    typedef typename FrameChannel<Packet>::Action Action;
    typedef void (*Callback)(void*);

    FrameWorker(Callback callback, void* context)
        : callback_(callback), context_(context), packet_(NULL), lastPublished_(0), permitted_(0),
          stopping_(false), aborted_(false), failed_(false), done_(false) {}
    ~FrameWorker() {
        if (thread_.joinable()) {
            abort();
            thread_.join();
        }
    }
    void start(unsigned processor) {
#ifdef _XBOX
        thread_ = Shipyard::Async::joining_thread(entry, this, processor);
#else
        (void)processor;
        thread_ = Shipyard::Async::joining_thread(entry, this);
#endif
    }
    Packet& buildingPacket() {
        if (!packet_ && !channel_.acquireBuild(building_, packet_))
            throw std::runtime_error("Game frame channel stopped");
        return *packet_;
    }
    bool publishAndWait() {
        if (!packet_) return false;
        const uint64_t generation = building_.generation;
        packet_ = NULL;
        if (!channel_.publish(building_)) return false;
        lastPublished_ = generation;
        Lock lock(gate_);
        while (!aborted_ && permitted_ < generation) handoff_.wait(lock);
        return !aborted_;
    }
    bool acquireReady(Ticket& ticket, const Packet*& packet) { return channel_.acquireReady(ticket, packet); }
    bool retire(const Ticket& ticket) { return channel_.retire(ticket); }
    void permitNext(const Ticket& ticket) {
        Lock lock(gate_);
        if (ticket.generation > permitted_) permitted_ = ticket.generation;
        handoff_.notify_all();
    }
    // Call after this packet retires, while its producer is still at handoff.
    void stopAtHandoff(const Ticket& ticket) {
        Lock lock(gate_);
        stopping_ = true;
        if (ticket.generation > permitted_) permitted_ = ticket.generation;
        handoff_.notify_all();
    }
    void joinPumping() {
        while (!done_.wait_for_ms(1)) channel_.pumpRenderJobs();
        channel_.pumpRenderJobs();
        thread_.join();
        if (failed_) throw std::runtime_error("Game frame worker failed");
    }
    bool fencePreviousReplay() { return channel_.waitRetired(lastPublished_); }
    uint64_t retiredGeneration() { return channel_.retiredGeneration(); }
    bool runOnRender(const Action& action) { return channel_.runOnRender(action); }
    unsigned pumpRenderJobs() { return channel_.pumpRenderJobs(); }
    void deferRelease(const Action& action) {
        channel_.deferRelease(packet_ ? building_.generation : lastPublished_, action);
    }
    uint64_t buildingGeneration() const { return packet_ ? building_.generation : lastPublished_; }
    uint64_t previousGeneration() const { return lastPublished_; }
    void abort() {
        {
            Lock lock(gate_);
            stopping_ = aborted_ = true;
            handoff_.notify_all();
        }
        channel_.requestStop();
    }

private:
    typedef Shipyard::Async::unique_lock<Shipyard::Async::mutex> Lock;
    static void entry(void* context) { static_cast<FrameWorker*>(context)->produce(); }
    void produce() {
        try {
            for (;;) {
                {
                    Lock lock(gate_);
                    if (stopping_) break;
                }
                if (!channel_.acquireBuild(building_, packet_)) break;
                callback_(context_);
                if (packet_) {
                    channel_.cancelBuild(building_);
                    packet_ = NULL;
                }
            }
        } catch (...) {
            Lock lock(gate_);
            failed_ = true;
        }
        if (packet_) {
            channel_.cancelBuild(building_);
            packet_ = NULL;
        }
        channel_.finishProducer();
        channel_.collectRetired();
        done_.set();
    }
    FrameWorker(const FrameWorker&);
    FrameWorker& operator=(const FrameWorker&);
    FrameChannel<Packet> channel_;
    Callback callback_;
    void* context_;
    Ticket building_;
    Packet* packet_;
    uint64_t lastPublished_, permitted_;
    Shipyard::Async::mutex gate_;
    Shipyard::Async::condition_variable handoff_;
    bool stopping_, aborted_, failed_;
    Shipyard::Async::manual_reset_event done_;
    Shipyard::Async::joining_thread thread_;
};
}
}

#endif
