#include "overlap/frame_channel.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <thread>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); \
    std::abort(); } } while (false)

struct Packet {
    int value;
    std::shared_ptr<int> lifetime;
    Packet() : value(0) {}
};
typedef Shipyard::Overlap::FrameChannel<Packet> Channel;
typedef Channel::Ticket Ticket;

template <class T> void blocked(std::future<T>& worker) {
    CHECK(worker.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
}
template <class T> T joined(std::future<T>& worker) {
    CHECK(worker.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    return worker.get();
}

void pool_reuse_and_final_replay() {
    Channel channel;
    Ticket first, second, render;
    Packet* build = NULL;
    const Packet* replay = NULL;
    CHECK(!channel.tryAcquireReady(render, replay));
    CHECK(channel.acquireBuild(first, build));
    CHECK(first.generation == 1 && first.slot == 0);
    build->value = 71;
    build->lifetime.reset(new int(13));
    std::weak_ptr<int> life(build->lifetime);
    CHECK(channel.publish(first));
    CHECK(channel.acquireReady(render, replay));
    CHECK(replay->value == 71 && render.generation == 1);
    CHECK(channel.acquireBuild(second, build));
    CHECK(second.generation == 2 && second.slot == 1);
    build->value = 82;
    CHECK(channel.publish(second));
    Ticket unavailable;
    const Packet* noPacket = NULL;
    CHECK(!channel.tryAcquireReady(unavailable, noPacket));
    std::atomic<bool> attempting(false);
    auto next = std::async(std::launch::async, [&]() {
        attempting = true;
        Ticket third;
        Packet* nextBuild = NULL;
        bool acquired = channel.acquireBuild(third, nextBuild);
        CHECK(acquired && third.generation == 3 && third.slot == first.slot);
        CHECK(life.expired());
        nextBuild->value = 93;
        CHECK(channel.publish(third));
        return acquired;
    });
    while (!attempting) std::this_thread::yield();
    blocked(next);
    // All replays still read the immutable first packet; a stale ticket cannot release it.
    CHECK(replay->value == 71 && !life.expired());
    Ticket stale = render;
    stale.generation += 2;
    CHECK(!channel.retire(stale));
    blocked(next);
    CHECK(replay->value == 71);
    CHECK(channel.retire(render));
    CHECK(joined(next));
    CHECK(channel.tryAcquireReady(render, replay) && replay->value == 82);
    CHECK(channel.retire(render));
    CHECK(channel.acquireReady(render, replay) && replay->value == 93);
    CHECK(channel.retire(render));
    CHECK(!channel.retire(render));
    channel.collectRetired();
    CHECK(channel.isDrained());
}

void deferred_release_and_idle_gui() {
    Channel channel;
    Ticket frame, render;
    Packet* build = NULL;
    const Packet* replay = NULL;
    CHECK(channel.acquireBuild(frame, build));
    int* allocation = new int(42);
    int guiValue = 0;
    bool freed = false;
    std::thread::id gameOwner = std::this_thread::get_id();
    channel.deferRelease(frame.generation, [&]() {
        CHECK(std::this_thread::get_id() == gameOwner);
        delete allocation;
        freed = true;
    });
    auto gui = std::async(std::launch::async, [&]() {
        channel.queueIdleAction([&]() { CHECK(std::this_thread::get_id() == gameOwner); guiValue = 17; });
        return true;
    });
    CHECK(joined(gui));
    channel.collectRetired();
    CHECK(!freed && guiValue == 0 && *allocation == 42);
    CHECK(channel.publish(frame));
    CHECK(channel.acquireReady(render, replay));
    channel.collectRetired();
    CHECK(!freed && guiValue == 0 && *allocation == 42);
    CHECK(channel.retire(render));
    CHECK(!freed);
    CHECK(channel.acquireBuild(frame, build));
    CHECK(freed && guiValue == 17);
    CHECK(channel.cancelBuild(frame));
    channel.collectRetired();
}

void drain_ready_terminal_and_pause() {
    Channel channel;
    Ticket first, terminal, render;
    Packet* build = NULL;
    const Packet* replay = NULL;
    CHECK(channel.acquireBuild(first, build));
    build->value = 10;
    CHECK(channel.publish(first));
    CHECK(channel.acquireReady(render, replay));
    CHECK(channel.acquireBuild(terminal, build));
    build->value = 20;
    CHECK(channel.publish(terminal));
    channel.stopFeeder();
    auto drain = std::async(std::launch::async, [&]() { channel.waitDrained(); return true; });
    blocked(drain);
    CHECK(channel.retire(render));
    blocked(drain);
    CHECK(channel.acquireReady(render, replay) && replay->value == 20);
    blocked(drain);
    CHECK(channel.retire(render));
    CHECK(joined(drain));
    auto feeder = std::async(std::launch::async, [&]() {
        Ticket next;
        Packet* packet = NULL;
        CHECK(channel.acquireBuild(next, packet));
        CHECK(channel.cancelBuild(next));
        return true;
    });
    blocked(feeder);
    channel.resumeFeeder();
    CHECK(joined(feeder));
    CHECK(channel.acquireBuild(terminal, build));
    build->value = 30;
    CHECK(channel.publish(terminal));
    channel.finishProducer();
    CHECK(channel.acquireReady(render, replay) && replay->value == 30);
    CHECK(channel.retire(render));
    CHECK(!channel.acquireReady(render, replay));
    channel.collectRetired();
}

void shutdown_preserves_active_and_building() {
    Channel channel;
    Ticket first, second, render;
    Packet* build = NULL;
    const Packet* replay = NULL;
    CHECK(channel.acquireBuild(first, build));
    build->lifetime.reset(new int(1));
    std::weak_ptr<int> active(build->lifetime);
    CHECK(channel.publish(first));
    CHECK(channel.acquireReady(render, replay));
    CHECK(channel.acquireBuild(second, build));
    build->lifetime.reset(new int(2));
    std::weak_ptr<int> building(build->lifetime);
    channel.requestStop();
    CHECK(!active.expired() && !building.expired());
    build->value = 99;
    CHECK(!channel.publish(second));
    channel.collectRetired();
    CHECK(!active.expired() && !building.expired());
    CHECK(replay->lifetime && *replay->lifetime == 1);
    CHECK(channel.retire(render));
    channel.collectRetired();
    CHECK(active.expired() && building.expired() && channel.isDrained());

    Channel blockedChannel;
    CHECK(blockedChannel.acquireBuild(first, build));
    CHECK(blockedChannel.publish(first));
    CHECK(blockedChannel.acquireBuild(second, build));
    CHECK(blockedChannel.publish(second));
    auto producer = std::async(std::launch::async, [&]() {
        Ticket ticket;
        Packet* packet = NULL;
        return blockedChannel.acquireBuild(ticket, packet);
    });
    blocked(producer);
    blockedChannel.requestStop();
    CHECK(!joined(producer));
    blockedChannel.collectRetired();
    CHECK(blockedChannel.isDrained());

    Channel unfinished;
    CHECK(unfinished.acquireBuild(first, build));
    unfinished.requestStop();
    auto drain = std::async(std::launch::async, [&]() { unfinished.waitDrained(); return true; });
    blocked(drain);
    CHECK(unfinished.cancelBuild(first));
    CHECK(joined(drain));
    unfinished.collectRetired();
}

void render_jobs_progress_without_mid_replay_wait() {
    Channel channel;
    Ticket frame, render;
    Packet* build = NULL;
    const Packet* replay = NULL;
    CHECK(channel.acquireBuild(frame, build));
    CHECK(channel.publish(frame));
    CHECK(channel.acquireReady(render, replay));
    CHECK(channel.acquireBuild(frame, build));
    CHECK(channel.publish(frame));
    int nativeValue = 0;
    std::thread::id renderOwner = std::this_thread::get_id();
    auto producer = std::async(std::launch::async, [&]() {
        CHECK(channel.runOnRender([&]() { CHECK(std::this_thread::get_id() == renderOwner); nativeValue = 6; }));
        Ticket next;
        Packet* packet = NULL;
        CHECK(channel.acquireBuild(next, packet));
        CHECK(channel.cancelBuild(next));
        return true;
    });
    blocked(producer);
    CHECK(channel.pumpRenderJobs() == 1 && nativeValue == 6);
    blocked(producer);
    CHECK(channel.retire(render));
    CHECK(joined(producer));
    CHECK(channel.acquireReady(render, replay));
    CHECK(channel.retire(render));
    channel.collectRetired();

    Channel stopped;
    auto job = std::async(std::launch::async, [&]() { return stopped.runOnRender([&]() { nativeValue = 9; }); });
    blocked(job);
    stopped.requestStop();
    CHECK(!joined(job) && nativeValue == 6 && stopped.pumpRenderJobs() == 0);

    Channel waiting;
    auto builder = std::async(std::launch::async, [&]() {
        Ticket ticket;
        Packet* packet = NULL;
        CHECK(waiting.acquireBuild(ticket, packet));
        CHECK(waiting.runOnRender([&]() { CHECK(std::this_thread::get_id() == renderOwner); nativeValue = 18; }));
        packet->value = nativeValue;
        CHECK(waiting.publish(ticket));
        waiting.finishProducer();
        return true;
    });
    // acquireReady must service native work requested before a packet can publish.
    CHECK(waiting.acquireReady(render, replay) && replay->value == 18);
    CHECK(waiting.retire(render) && joined(builder));
    CHECK(!waiting.acquireReady(render, replay));
    waiting.collectRetired();

    Channel inFlight;
    Shipyard::Async::manual_reset_event entered, release;
    auto requester = std::async(std::launch::async, [&]() {
        return inFlight.runOnRender([&]() { entered.set(); release.wait(); });
    });
    auto renderer = std::async(std::launch::async, [&]() {
        while (!entered.wait_for_ms(0)) {
            if (inFlight.pumpRenderJobs() != 0) break;
            std::this_thread::yield();
        }
        return true;
    });
    CHECK(entered.wait_for_ms(2000));
    inFlight.requestStop();
    CHECK(!inFlight.isDrained());
    blocked(requester);
    release.set();
    CHECK(joined(requester) && joined(renderer));
    CHECK(inFlight.isDrained());
}

void audio_generation_fence() {
    Shipyard::Overlap::AudioGenerationGate audio;
    CHECK(audio.begin(1));
    CHECK(!audio.begin(2));
    std::atomic<int> gameWrite(0);
    auto producer = std::async(std::launch::async, [&]() {
        CHECK(audio.wait(1));
        gameWrite = 1;
        return true;
    });
    blocked(producer);
    CHECK(gameWrite == 0 && !audio.complete(0) && !audio.complete(2));
    blocked(producer);
    CHECK(audio.complete(1) && joined(producer) && gameWrite == 1);
    CHECK(audio.begin(2));
    CHECK(!audio.complete(1));
    auto next = std::async(std::launch::async, [&]() { return audio.wait(2); });
    blocked(next);
    audio.requestStop();
    CHECK(!joined(next));
    CHECK(!audio.begin(3));
}

void borrowed_write_fence_while_building() {
    Channel channel;
    Ticket first, next, render;
    Packet* build = NULL;
    const Packet* replay = NULL;
    CHECK(channel.acquireBuild(first, build));
    CHECK(channel.publish(first));
    CHECK(channel.acquireReady(render, replay));
    CHECK(channel.acquireBuild(next, build));
    std::atomic<int> borrowedWrite(0);
    auto producer = std::async(std::launch::async, [&]() {
        bool retired = channel.waitRetired(first.generation);
        if (retired) borrowedWrite = 55;
        return retired;
    });
    blocked(producer);
    CHECK(borrowedWrite == 0);
    int nativeWrite = 0;
    auto job = std::async(std::launch::async, [&]() {
        return channel.runOnRender([&]() { nativeWrite = 3; });
    });
    blocked(job);
    CHECK(channel.pumpRenderJobs() == 1 && joined(job) && nativeWrite == 3);
    blocked(producer);
    CHECK(channel.retire(render) && joined(producer) && borrowedWrite == 55);
    CHECK(channel.cancelBuild(next));
    channel.collectRetired();

    Channel stopped;
    CHECK(stopped.acquireBuild(first, build));
    CHECK(stopped.publish(first));
    CHECK(stopped.acquireReady(render, replay));
    CHECK(stopped.acquireBuild(next, build));
    auto fence = std::async(std::launch::async, [&]() { return stopped.waitRetired(first.generation); });
    blocked(fence);
    stopped.requestStop();
    CHECK(!joined(fence));
    CHECK(!stopped.isDrained());
    CHECK(stopped.cancelBuild(next) && stopped.retire(render));
    stopped.collectRetired();
}

void shutdown_cleanup_waits_for_borrowing_render_job() {
    Channel channel;
    Ticket ticket;
    Packet* build = NULL;
    CHECK(channel.acquireBuild(ticket, build));
    build->value = 103;
    build->lifetime.reset(new int(29));
    std::weak_ptr<int> packetLifetime(build->lifetime);
    const Packet* borrowedPacket = build;
    int* borrowedResource = new int(47);
    std::atomic<int> physicalFrees(0);
    std::thread::id gameOwner = std::this_thread::get_id();
    channel.deferRelease(ticket.generation, [&]() {
        CHECK(std::this_thread::get_id() == gameOwner);
        delete borrowedResource;
        ++physicalFrees;
    });
    CHECK(channel.publish(ticket));
    Shipyard::Async::manual_reset_event entered, release;
    // This requester is independent of the idle producer performing collection.
    auto requester = std::async(std::launch::async, [&]() {
        return channel.runOnRender([&]() {
            CHECK(*borrowedResource == 47 && borrowedPacket->value == 103);
            entered.set();
            release.wait();
            CHECK(*borrowedResource == 47 && borrowedPacket->value == 103);
            CHECK(borrowedPacket->lifetime && *borrowedPacket->lifetime == 29);
        });
    });
    auto renderer = std::async(std::launch::async, [&]() {
        while (channel.pumpRenderJobs() == 0) std::this_thread::yield();
        return true;
    });
    CHECK(entered.wait_for_ms(2000));
    channel.requestStop();
    channel.collectRetired();
    CHECK(physicalFrees == 0 && !packetLifetime.expired());
    CHECK(!channel.isDrained());
    blocked(requester);
    release.set();
    CHECK(joined(requester) && joined(renderer));
    CHECK(channel.isDrained());
    CHECK(physicalFrees == 0 && !packetLifetime.expired());
    channel.collectRetired();
    CHECK(physicalFrees == 1 && packetLifetime.expired());
    channel.collectRetired();
    CHECK(physicalFrees == 1);
}

int main() {
    pool_reuse_and_final_replay();
    deferred_release_and_idle_gui();
    drain_ready_terminal_and_pause();
    shutdown_preserves_active_and_building();
    render_jobs_progress_without_mid_replay_wait();
    audio_generation_fence();
    borrowed_write_fence_while_building();
    shutdown_cleanup_waits_for_borrowing_render_job();
    std::puts("frame channel: pool reuse, owner releases, idle GUI, terminal drain, stop, render jobs, audio and write fence passed");
}
