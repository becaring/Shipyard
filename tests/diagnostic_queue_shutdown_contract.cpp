#include "diagnostics/queue.h"
#include <atomic>
#include <cstdio>

using Shipyard::Diagnostics::Config;
using Shipyard::Diagnostics::Queue;
using Shipyard::Diagnostics::Record;

struct Fixture {
    HANDLE callback_entered, callback_release, first_returned;
    HANDLE second_returned, second_waiting;
    std::atomic<int> consumed, callback_exited;
    Queue* queue;
    Fixture() : callback_entered(CreateEvent(NULL, TRUE, FALSE, NULL)),
        callback_release(CreateEvent(NULL, TRUE, FALSE, NULL)),
        first_returned(CreateEvent(NULL, TRUE, FALSE, NULL)),
        second_returned(CreateEvent(NULL, TRUE, FALSE, NULL)),
        second_waiting(CreateEvent(NULL, TRUE, FALSE, NULL)),
        consumed(0), callback_exited(0), queue(NULL) {}
    ~Fixture() {
        CloseHandle(callback_entered); CloseHandle(callback_release);
        CloseHandle(first_returned); CloseHandle(second_returned);
        CloseHandle(second_waiting);
    }
};

static Fixture* observed_fixture;
static std::atomic<DWORD> second_thread_id(0);
static DWORD WINAPI ObservedWaitForSingleObject(HANDLE handle, DWORD timeout) {
    if (GetCurrentThreadId() == second_thread_id.load() && timeout == INFINITE)
        SetEvent(observed_fixture->second_waiting);
    return ::WaitForSingleObject(handle, timeout);
}

// Instrument only this call boundary. Queue still uses the real Windows
// critical sections, events, threads, and WaitForSingleObject implementation.
#define WaitForSingleObject ObservedWaitForSingleObject
#include "../sources/diagnostics/queue.cpp"
#undef WaitForSingleObject

static void Consume(const Record&, void* context) {
    ++static_cast<Fixture*>(context)->consumed;
}
static void Flush(void*) {}
static void Stopped(void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    SetEvent(fixture.callback_entered);
    WaitForSingleObject(fixture.callback_release, INFINITE);
    ++fixture.callback_exited;
}
static void StopFromWorker(void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    fixture.queue->shutdown(); // Must not join itself.
    ++fixture.callback_exited;
}
static void RequestStopFromWorker(void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    fixture.queue->shutdown(); // Request stop while the worker is starting.
    SetEvent(fixture.first_returned);
}
static void MarkStopped(void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    ++fixture.callback_exited;
    SetEvent(fixture.callback_entered);
}
static Config MakeConfig(Fixture& fixture) {
    Config config = {};
    config.context = &fixture; config.consume = Consume; config.flush = Flush;
    config.worker_stopped = Stopped;
    config.byte_limit = 32; config.line_limit = 4;
    config.wake_bytes = 1; config.interval_ms = 10;
    return config;
}
static DWORD WINAPI FirstShutdown(void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    fixture.queue->shutdown();
    SetEvent(fixture.first_returned);
    return 0;
}
static DWORD WINAPI SecondShutdown(void* context) {
    Fixture& fixture = *static_cast<Fixture*>(context);
    second_thread_id.store(GetCurrentThreadId());
    fixture.queue->shutdown();
    SetEvent(fixture.second_returned);
    return 0;
}

int main() {
    {
        Fixture fixture; Queue queue(MakeConfig(fixture)); fixture.queue = &queue;
        queue.shutdown(); queue.shutdown(); // No worker was ever started.
        if (fixture.callback_exited != 0) return 1;
    }
    {
        Fixture fixture; Queue queue(MakeConfig(fixture)); fixture.queue = &queue;
        observed_fixture = &fixture;
        Record record; record.bytes = "accepted";
        if (!queue.submit(record)) return 2;
        HANDLE first = CreateThread(NULL, 0, FirstShutdown, &fixture, 0, NULL);
        if (!first || WaitForSingleObject(fixture.callback_entered, 2000) != WAIT_OBJECT_0)
            return 3; // Watchdog only: the worker failed to reach its callback.
        HANDLE second = CreateThread(NULL, 0, SecondShutdown, &fixture, 0, NULL);
        if (!second) return 4;
        HANDLE outcomes[] = {fixture.second_returned, fixture.second_waiting};
        DWORD outcome = WaitForMultipleObjects(2, outcomes, FALSE, 2000);
        const bool returned_early = outcome == WAIT_OBJECT_0 && fixture.callback_exited == 0;
        const bool entered_join = outcome == WAIT_OBJECT_0 + 1;
        SetEvent(fixture.callback_release);
        const bool finished = WaitForSingleObject(first, 2000) == WAIT_OBJECT_0 &&
                              WaitForSingleObject(second, 2000) == WAIT_OBJECT_0;
        CloseHandle(first); CloseHandle(second);
        if (returned_early) {
            std::puts("second shutdown returned before worker_stopped exited");
            return 5;
        }
        if (!entered_join || !finished || fixture.callback_exited != 1 ||
            fixture.consumed != 1 || queue.stats().accepted != queue.stats().processed)
            return 6;
        queue.shutdown(); // Repeated completion after the worker has stopped.
    }
    {
        Fixture fixture; Config config = MakeConfig(fixture);
        config.worker_stopped = StopFromWorker;
        Queue queue(config); fixture.queue = &queue;
        Record record; record.bytes = "self";
        if (!queue.submit(record)) return 7;
        queue.shutdown();
        if (fixture.callback_exited != 1 || fixture.consumed != 1) return 8;
    }
    {
        Fixture fixture; Config config = MakeConfig(fixture);
        config.worker_started = RequestStopFromWorker;
        config.worker_stopped = MarkStopped;
        Queue queue(config); fixture.queue = &queue;
        Record record; record.bytes = "self request";
        if (!queue.submit(record)) return 9;
        if (WaitForSingleObject(fixture.first_returned, 2000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(fixture.callback_entered, 2000) != WAIT_OBJECT_0)
            return 10; // Watchdog: worker did not finish its own stop request.
        queue.shutdown(); // External caller establishes callback completion.
        if (fixture.callback_exited != 1 || fixture.consumed != 1) return 11;
    }
    std::puts("diagnostic queue overlapping shutdown completion PASS");
    return 0;
}
