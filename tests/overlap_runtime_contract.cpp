// The shared runtime releases the game at the first presentation, after one passive
// GUI build at the idle handoff; every accepted presentation redraws that capture.
// Intents stop the session at the handoff and commit only after the worker joins.
#include "overlap/runtime.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

struct Resource {};
struct Cache { unsigned id; Cache() : id(0) {} };
struct Packet {
    typedef Shipyard::Overlap::ResourceOwners<Resource> Owners;
    uint64_t generation;
    Owners resources;
    Packet() : generation(0) {}
};

static std::atomic<unsigned> builds(0), tails(0), nativeJobs(0);
static std::atomic<bool> gameAtHandoff(false);
static std::thread::id renderThread;
static unsigned checks, accepted, dropped, cacheDraws, preflights, commits, captures, warmups;
static unsigned beginCaptures, endCaptures, inputs, snapshots;
static bool pendingIntent, observedConcurrentBuild, requested, allDropped, shutdownMode;
static bool capturing;
static std::shared_ptr<Resource> retained;

static void AssertIdleHandoff() {
    assert(std::this_thread::get_id() == renderThread);
    assert(gameAtHandoff);
}
static void Pause(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct FakeCapture {
    typedef ::Cache Cache;
    static void Begin(Cache*, bool suppress) {
        assert(!capturing);
        capturing = true;
        ++beginCaptures;
        if (!suppress) ++warmups;
    }
    static bool End() {
        assert(capturing);
        capturing = false;
        ++endCaptures;
        return true;
    }
    static void Draw(Cache&, uintptr_t texture) {
        assert(texture == 7);
        ++cacheDraws;
    }
};

struct Session;
typedef Shipyard::Overlap::Runtime<Session, Packet, FakeCapture> Base;
static void NativeJob(void*) {
    assert(std::this_thread::get_id() == renderThread);
    ++nativeJobs;
}

struct Session : Base {
    static void Log(const char* what, const char* detail) { printf("%s: %s\n", what, detail); }
    static bool Requested() { return requested; }
    static bool InteractiveGui() { return false; }
    static uintptr_t GameTexture() { return 7; }
    static void CommitGuiIntent() {
        assert(std::this_thread::get_id() == renderThread);
        assert(session == NULL && tails == builds);
        if (pendingIntent) ++commits;
    }
    void BuildFrame() {
        Packet& packet = worker.buildingPacket();
        packet.generation = worker.buildingGeneration();
        assert(packet.generation <= 3);
        if (packet.generation > 2) assert(worker.retiredGeneration() >= packet.generation - 2);
        ++builds;
        Retain(retained);
        if (packet.generation == 3) RunOnRender(NativeJob, NULL);
        gameAtHandoff = true;
        if (!worker.publishAndWait()) throw std::runtime_error("handoff aborted");
        gameAtHandoff = false;
        ++tails;
    }
    void RenderReplays(const Ticket&, const Packet& packet) {
        assert(renderResources.size() == 0 && RenderingPacket() == &packet);
        for (unsigned replay = 0; replay != 3; ++replay) {
            const unsigned check = ++checks;
            if (allDropped || check % 3 == 2) { ++dropped; continue; }
            ++accepted;
            assert(DrawGui(1));
            if (packet.generation == 1) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (builds <= 1 && std::chrono::steady_clock::now() < deadline) Pause(1);
                assert(builds > 1);
            } else Pause(4);
            if (builds > packet.generation) observedConcurrentBuild = true;
            Retain(retained);
            assert(DrawGui(0));
        }
    }
    bool DrawPassiveGui() {
        AssertIdleHandoff();
        assert(capturing);
        ++preflights;
        pendingIntent = preflights == 3 && !shutdownMode;
        return !pendingIntent;
    }
    void CaptureInput() { assert(std::this_thread::get_id() == renderThread); ++inputs; }
    void CaptureVariables() { AssertIdleHandoff(); ++snapshots; }
    void ReleaseVariables() {}
};

static void Scenario(bool drop, bool shutdown) {
    builds = tails = nativeJobs = 0;
    checks = accepted = dropped = cacheDraws = preflights = commits = warmups = 0;
    beginCaptures = endCaptures = inputs = snapshots = 0;
    pendingIntent = observedConcurrentBuild = false;
    allDropped = drop;
    shutdownMode = shutdown;
    requested = true;
    assert(Session::RunFrame() == 0 && warmups == 1);
    for (unsigned frame = 0; frame != 2; ++frame) assert(Session::RunFrame() == 1);
    if (shutdown) Session::Shutdown();
    else {
        if (drop) requested = false;
        assert(Session::RunFrame() == 1);
    }
    assert(Session::session == NULL && !capturing);
    assert(builds == 3 && tails == 3 && nativeJobs == 1);
    assert(checks == 9 && accepted == (drop ? 0u : 6u) && dropped == (drop ? 9u : 3u));
    assert(cacheDraws == accepted && preflights == (drop ? 0u : 3u));
    assert(beginCaptures == endCaptures && beginCaptures == preflights + 1);
    assert(commits == (drop || shutdown ? 0u : 1u));
    assert(snapshots == 3 && inputs == 1 + preflights);
    assert(drop || observedConcurrentBuild);
    assert(retained.use_count() == 1);
    // A stopped session warms up again before the next one; a refusal discards it.
    requested = true;
    assert(Session::RunFrame() == 0 && warmups == 2);
    requested = false;
    assert(Session::RunFrame() == 0 && !capturing);
    Session::Shutdown();
}

int main() {
    renderThread = std::this_thread::get_id();
    retained.reset(new Resource());
    Scenario(false, false);
    Scenario(true, false);
    Scenario(false, true);
    puts("overlap runtime: passive GUI handoff, cache replay, deferred intent and stop drain passed");
}
