// Exercise the game callback handoff, native-job progress and terminal replay.
#include "overlap/frame_worker.h"
#include <cassert>
#include <cstdio>
#include <atomic>
#include <thread>

struct Packet {
    unsigned value;
    int* borrowed;
    Packet() : value(0), borrowed(NULL) {}
};

struct Game {
    Shipyard::Overlap::FrameWorker<Packet>* worker;
    unsigned built, guiValue, terminalDestroyed;
    int buffers[2];
    std::atomic<unsigned> nativeJobs, releases;
    std::atomic<bool> producerWaitingFence;
    Game() : worker(NULL), built(0), guiValue(0), terminalDestroyed(0), nativeJobs(0), releases(0),
        producerWaitingFence(false) { buffers[0] = buffers[1] = 0; }
    static void update(void* context) {
        Game& game = *static_cast<Game*>(context);
        Packet& packet = game.worker->buildingPacket();
        const unsigned n = ++game.built;
        // GUI writes become visible before the next whole game callback.
        assert(game.guiValue == n - 1);
        packet.value = n;
        packet.borrowed = &game.buffers[(n - 1) % 2];
        *packet.borrowed = static_cast<int>(n);
        if (n == 2) {
            game.producerWaitingFence.store(true);
            assert(game.worker->fencePreviousReplay());
            assert(game.worker->runOnRender([&game]() { ++game.nativeJobs; }));
            game.worker->deferRelease([&game]() { ++game.releases; });
        }
        assert(game.worker->publishAndWait());
        if (n == 4) {
            assert(game.worker->fencePreviousReplay());
            ++game.terminalDestroyed;
        }
    }
};

int main() {
    Game game;
    Shipyard::Overlap::FrameWorker<Packet> worker(Game::update, &game);
    game.worker = &worker;
    worker.start(4);
    for (unsigned n = 1; n <= 4; ++n) {
        Shipyard::Overlap::FrameWorker<Packet>::Ticket ticket;
        const Packet* packet;
        assert(worker.acquireReady(ticket, packet));
        assert(packet->value == n && *packet->borrowed == static_cast<int>(n));
        game.guiValue = n;
        if (n != 4) worker.permitNext(ticket);
        if (n == 1) {
            while (!game.producerWaitingFence.load()) std::this_thread::yield();
            assert(game.nativeJobs == 0);
        }
        // Replay accesses remain valid until the final replay's retirement.
        for (unsigned replay = 0; replay != 3; ++replay) {
            assert(*packet->borrowed == static_cast<int>(n));
            worker.pumpRenderJobs();
        }
        assert(worker.retire(ticket));
        if (n == 4) {
            assert(game.terminalDestroyed == 0);
            worker.stopAtHandoff(ticket);
        }
    }
    worker.joinPumping();
    assert(game.terminalDestroyed == 1 && game.nativeJobs == 1 && game.releases == 1);
    assert(game.built == 4);
    std::puts("Whole-game handoff, native jobs, release ownership and terminal retirement passed");
}
