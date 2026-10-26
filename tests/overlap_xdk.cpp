// Instantiate every overlap primitive with the PowerPC compiler.
#include "overlap/deferred_storage.h"
#include "overlap/depth.h"
#include "overlap/frame_worker.h"
#include "overlap/replay_batch.h"
#include "overlap/writable_spans.h"
#include <vector>

struct Packet { std::vector<int> commands; };
static void action() {}
static void update(void*) {}
static void release(void*, void*, const char*, int) {}
static int record(float) { return 1; }

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
}
