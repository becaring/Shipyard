// Instantiate every overlap primitive with the PowerPC compiler.
#include "overlap/deferred_storage.h"
#include "overlap/depth.h"
#include "overlap/frame_worker.h"
#include "overlap/replay_batch.h"
#include "overlap/resource_owners.h"
#include "overlap/runtime.h"
#include "overlap/writable_spans.h"
#include "diagnostics/timing_totals.h"
#include <vector>

struct Packet { std::vector<int> commands; };
static void action() {}
static void update(void*) {}
static void release(void*, void*, const char*, int) {}
static int record(float) { return 1; }
struct RuntimeCache {};
struct RuntimeCapture {
    typedef RuntimeCache Cache;
    static void Begin(Cache*, bool) {}
    static bool End() { return true; }
    static void Draw(Cache&, uintptr_t) {}
};
struct RuntimePacket {
    typedef Shipyard::Overlap::ResourceOwners<Packet> Owners;
    Owners resources;
};
struct RuntimeSession : Shipyard::Overlap::Runtime<RuntimeSession, RuntimePacket, RuntimeCapture> {
    static void Log(const char*, const char*) {}
    static bool Requested() { return false; }
    static bool InteractiveGui() { return false; }
    static void CommitGuiIntent() {}
    static uintptr_t GameTexture() { return 0; }
    void BuildFrame() { worker.publishAndWait(); }
    void RenderReplays(const Ticket&, const RuntimePacket&) { DrawGui(1); DrawGui(0); }
    bool DrawPassiveGui() { return true; }
    void CaptureInput() {}
    void CaptureVariables() {}
    void ReleaseVariables() {}
};
static void runtime_xdk_contract() {
    int blocked = 0;
    RuntimeSession::RunFrame();
    RuntimeSession::Fence();
    RuntimeSession::RunOnRender(update, NULL);
    RuntimeSession::InputBlocked(0, &blocked);
    RuntimeSession::Retain(std::shared_ptr<Packet>());
    RuntimeSession::Shutdown();
}

void overlap_xdk_contract() {
    Shipyard::Overlap::FrameChannel<Packet> channel;
    Shipyard::Overlap::FrameChannel<Packet>::Ticket ticket;
    Packet* build = NULL;
    const Packet* render = NULL;
    channel.acquireBuild(ticket, build);
    channel.deferRelease(ticket.generation, action);
    channel.queueIdleAction(action);
    channel.publish(ticket);
    channel.tryAcquireReady(ticket, render);
    channel.retire(ticket);
    channel.collectRetired();
    channel.acquireReady(ticket, render);
    channel.cancelBuild(ticket);
    channel.stopFeeder();
    channel.waitDrained();
    channel.resumeFeeder();
    channel.runOnRender(action);
    channel.pumpRenderJobs();
    channel.finishProducer();
    channel.requestStop();
    channel.isDrained();
    channel.waitRetired(ticket.generation);
    Shipyard::Overlap::AudioGenerationGate audio;
    audio.begin(1);
    audio.complete(1);
    audio.wait(1);
    audio.requestStop();
    Shipyard::Overlap::FrameWorker<Packet> worker(update, NULL);
    worker.start(4);
    Shipyard::Overlap::DeferredStorage storage;
    storage.defer(worker, release, NULL, &storage, __FILE__, __LINE__);
    storage.retryAllocation(worker);
    storage.drainOwner(worker, NULL);
    worker.abort();
    worker.joinPumping();
    Shipyard::Overlap::ReplayCadence cadence;
    cadence.prepare<int>(60, 3, false, record);
    Shipyard::Overlap::WritableSpans spans;
    spans.Record(&spans, sizeof(spans), 2, spans.LatestReader(&spans, sizeof(spans), 2, 0));
    ShipyardOverlapDepthRequest request;
    ShipyardOverlapDepthResult result;
    memset(&request, 0, sizeof(request));
    Shipyard_OverlapSampleDepth(&request, &result, NULL, NULL, NULL);
    Shipyard::Overlap::ResourceOwners<Packet> owners;
    {
        Shipyard::Overlap::ResourceReaderScope<Shipyard::Overlap::ResourceOwners<Packet> > scope(owners);
        owners.push_back(std::shared_ptr<Packet>(new Packet()));
    }
    Shipyard::Diagnostics::TimingSample<4> sample;
    sample.Reset(true);
    sample.Begin(1, 1);
    sample.End(1, 2);
    Shipyard::Diagnostics::TimingTotals<4> totals;
    totals.Merge(sample.totals);
    runtime_xdk_contract();
}
