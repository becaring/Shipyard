#include "diagnostics/queue.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>
#include <vector>

using Shipyard::Diagnostics::Queue;
using Shipyard::Diagnostics::Record;
using Shipyard::Diagnostics::Config;

struct Fixture {
    CRITICAL_SECTION gate;
    std::vector<std::string> output;
    HANDLE entered;
    HANDLE release;
    std::atomic<int> failures;
    std::atomic<int> recurse;
    Queue* queue;
    Fixture() : entered(CreateEvent(NULL, TRUE, FALSE, NULL)),
        release(CreateEvent(NULL, TRUE, TRUE, NULL)), failures(0), recurse(0), queue(NULL) {
        InitializeCriticalSection(&gate);
    }
    ~Fixture() { CloseHandle(entered); CloseHandle(release); DeleteCriticalSection(&gate); }
};

static void consume(const Record& record, void* context) {
    Fixture& f = *static_cast<Fixture*>(context);
    if (record.bytes == "hold") {
        SetEvent(f.entered);
        WaitForSingleObject(f.release, INFINITE);
    }
    if (record.bytes == "recurse" && f.recurse.exchange(0)) {
        Record nested; nested.bytes = "nested";
        if (!f.queue->submit(nested)) ++f.failures;
    }
    if (record.bytes == "failure") throw std::runtime_error("simulated sink failure");
    EnterCriticalSection(&f.gate);
    f.output.push_back(record.bytes);
    LeaveCriticalSection(&f.gate);
}
static void flush(void*) {}
static void sink_error(void* context) { ++static_cast<Fixture*>(context)->failures; }
static void worker_started(void* context) {
    Fixture& f = *static_cast<Fixture*>(context);
    Record record; record.bytes = "started";
    if (!f.queue->submit(record)) ++f.failures;
}
static Config config(Fixture& f, bool sync = false) {
    Config c = {};
    c.context = &f; c.consume = consume; c.flush = flush; c.sink_error = sink_error;
    c.byte_limit = 32; c.line_limit = 2; c.wake_bytes = 16;
    c.interval_ms = 10; c.force_synchronous = sync;
    return c;
}
static DWORD WINAPI submit_one(void* value) {
    Queue* q = static_cast<Queue*>(value);
    Record record; record.bytes = "later";
    return q->submit(record) ? 0 : 1;
}
static DWORD WINAPI shutdown_one(void* value) {
    static_cast<Queue*>(value)->shutdown(); return 0;
}
static bool output_is(Fixture& f, const char* first, const char* second = NULL) {
    EnterCriticalSection(&f.gate);
    bool ok = f.output.size() == (second ? 2u : 1u) &&
        f.output[0] == first && (!second || f.output[1] == second);
    LeaveCriticalSection(&f.gate); return ok;
}
int main() {
    {
        Fixture f; Queue q(config(f, true)); f.queue = &q;
        Record a; a.bytes = "recurse"; f.recurse = 1;
        if (!q.submit(a) || !output_is(f, "nested", "recurse")) return 1;
        q.shutdown();
        Record b; b.bytes = "after";
        if (!q.submit(b) || f.output.back() != "after") return 2;
    }
    {
        Fixture f; Queue q(config(f)); f.queue = &q;
        ResetEvent(f.release);
        Record hold; hold.bytes = "hold";
        if (!q.submit(hold) || WaitForSingleObject(f.entered, 2000) != WAIT_OBJECT_0) return 3;
        Record queued; queued.bytes = "queue";
        if (!q.submit(queued)) return 4;
        HANDLE blocked = CreateThread(NULL, 0, submit_one, &q, 0, NULL);
        Sleep(50);
        if (WaitForSingleObject(blocked, 0) != WAIT_TIMEOUT || q.stats().backpressure == 0) return 5;
        HANDLE shutting = CreateThread(NULL, 0, shutdown_one, &q, 0, NULL);
        SetEvent(f.release);
        if (WaitForSingleObject(blocked, 2000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(shutting, 2000) != WAIT_OBJECT_0) return 6;
        DWORD code = 1; GetExitCodeThread(blocked, &code);
        CloseHandle(blocked); CloseHandle(shutting);
        if (code || f.output.size() != 3 || f.output[0] != "hold" ||
            f.output[1] != "queue" || f.output[2] != "later") return 7;
        Record after; after.bytes = "post";
        if (!q.submit(after) || f.output.back() != "post") return 8;
    }
    {
        Fixture f; Config c = config(f); c.byte_limit = 12;
        c.line_limit = 8; c.wake_bytes = 1000;
        Queue q(c); f.queue = &q;
        ResetEvent(f.release);
        Record hold; hold.bytes = "hold";
        Record queued; queued.bytes = "queue";
        if (!q.submit(hold) || WaitForSingleObject(f.entered, 2000) != WAIT_OBJECT_0 ||
            !q.submit(queued)) return 13;
        HANDLE blocked = CreateThread(NULL, 0, submit_one, &q, 0, NULL);
        Sleep(50);
        if (WaitForSingleObject(blocked, 0) != WAIT_TIMEOUT || q.stats().backpressure == 0) return 14;
        SetEvent(f.release);
        if (WaitForSingleObject(blocked, 2000) != WAIT_OBJECT_0) return 15;
        CloseHandle(blocked);
        q.flush(); q.shutdown();
        if (f.output.size() != 3 || f.output[0] != "hold" ||
            f.output[1] != "queue" || f.output[2] != "later") return 16;
    }
    {
        Fixture f; Queue q(config(f)); f.queue = &q;
        Record a; a.bytes = "recurse"; f.recurse = 1;
        if (!q.submit(a)) return 9;
        q.flush();
        if (!output_is(f, "recurse", "nested")) return 10;
        Record failure; failure.bytes = "failure";
        if (!q.submit(failure)) return 11;
        Record survived; survived.bytes = "survived";
        if (!q.submit(survived)) return 21;
        q.shutdown();
        if (f.failures != 1 || q.stats().accepted != q.stats().processed ||
            f.output.back() != "survived") return 12;
    }
    {
        Fixture f; Config c = config(f); c.worker_started = worker_started;
        Queue q(c); f.queue = &q;
        Record outer; outer.bytes = "outer";
        if (!q.submit(outer)) return 17;
        q.flush(); q.shutdown();
        if (f.failures != 0 || !output_is(f, "outer", "started")) return 18;
    }
    {
        Fixture f; Config c = config(f); c.line_limit = 1;
        Queue q(c); f.queue = &q; f.recurse = 1;
        Record record; record.bytes = "recurse";
        if (!q.submit(record)) return 19;
        q.flush(); q.shutdown();
        if (f.failures != 0 || !output_is(f, "nested", "recurse")) return 20;
    }
    std::puts("diagnostic queue saturation, fallback, recursion, drain, shutdown race, sink failure, post-shutdown PASS");
}
