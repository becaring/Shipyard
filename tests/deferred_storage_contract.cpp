// Storage callbacks keep copied debug metadata and cannot run twice after arena reset.
#include "overlap/deferred_storage.h"
#include "overlap/frame_worker.h"
#include <cassert>
#include <cstring>
#include <cstdio>

struct Packet {};
struct State {
    Shipyard::Overlap::FrameWorker<Packet>* worker;
    Shipyard::Overlap::DeferredStorage storage;
    unsigned frame, released;
    int arena, allocation;
    State() : worker(NULL), frame(0), released(0), arena(0), allocation(17) {}
    static void release(void* owner, void* data, const char* file, int line) {
        State& state = *static_cast<State*>(owner);
        assert(data == &state.allocation);
        assert(std::strcmp(file, "temporary-source.c") == 0 && line == 73);
        ++state.released;
    }
    static void game(void* context) {
        State& state = *static_cast<State*>(context);
        ++state.frame;
        state.storage.collect();
        if (state.frame == 1) {
            char file[] = "temporary-source.c";
            state.storage.defer(*state.worker, release, &state, &state.allocation, file, 73);
            std::memset(file, 'x', sizeof(file) - 1);
            assert(state.released == 0);
        }
        if (state.frame == 2) {
            state.storage.defer(*state.worker, release, &state, &state.allocation, "temporary-source.c", 73);
            assert(state.storage.drainOwner(*state.worker, &state));
            assert(state.released == 2);
        }
        assert(state.worker->publishAndWait());
    }
};

int main() {
    State state;
    Shipyard::Overlap::FrameWorker<Packet> worker(State::game, &state);
    state.worker = &worker;
    worker.start(4);
    for (unsigned i = 1; i <= 2; ++i) {
        Shipyard::Overlap::FrameWorker<Packet>::Ticket ticket;
        const Packet* packet;
        assert(worker.acquireReady(ticket, packet));
        if (i == 1) assert(state.released == 0);
        assert(worker.retire(ticket));
        if (i == 1) worker.permitNext(ticket);
        else worker.stopAtHandoff(ticket);
    }
    worker.joinPumping();
    assert(state.released == 2);
    std::puts("Deferred storage debug ownership and arena reset retirement passed");
}
