// A repeated skin draw must wait before overwriting the preceding frame's data.
#include "overlap/writable_spans.h"
#include "overlap/frame_channel.h"
#include <atomic>
#include <chrono>
#include <future>
#include <stdio.h>
#include <stdlib.h>

static void Check(bool value) { if (!value) abort(); }
struct Packet { unsigned value; Packet() : value(0) {} };

int main() {
    Shipyard::Overlap::WritableSpans direct;
    unsigned char data[32] = {};
    direct.Record(data, 16, 1, 0);
    Check(direct.LatestReader(data + 8, 8, 2, 0) == 1);
    Check(direct.LatestReader(data + 16, 16, 2, 0) == 0);
    Check(direct.LatestReader(data, 16, 1, 0) == 0);
    Check(direct.LatestReader(data, 16, 2, 1) == 0);
    direct.Record(data, 16, 2, 1);
    Check(direct.Count() == 1);
    direct.Record(data, 16, 2, 1);
    Check(direct.Count() == 1);
    direct.Record(data, 0, 3, 2);
    Check(direct.LatestReader(data, 0, 3, 0) == 0);
    direct.Reset();
    Check(direct.Count() == 0);

    Shipyard::Overlap::FrameChannel<Packet> channel;
    Shipyard::Overlap::WritableSpans spans;
    std::atomic<uint64_t> retired(0);
    std::promise<void> alternateWritten;
    unsigned char buffers[2][16] = {};
    std::future<void> alternate = alternateWritten.get_future();
    std::future<void> producer = std::async(std::launch::async, [&]() {
        Shipyard::Overlap::FrameChannel<Packet>::Ticket first, second;
        Packet* packet = NULL;
        Check(channel.acquireBuild(first, packet));
        buffers[0][0] = 0x12;
        spans.Record(buffers[0], 16, first.generation, 0);
        Check(channel.publish(first));
        Check(channel.acquireBuild(second, packet));
        Check(spans.LatestReader(buffers[1], 16, second.generation, 0) == 0);
        buffers[1][0] = 0x23;
        spans.Record(buffers[1], 16, second.generation, 0);
        alternateWritten.set_value();
        const uint64_t reader = spans.LatestReader(buffers[0], 16, second.generation, retired.load());
        Check(reader == first.generation);
        Check(channel.waitRetired(reader));
        buffers[0][0] = 0x34;
        spans.Record(buffers[0], 16, second.generation, retired.load());
        Check(channel.cancelBuild(second));
        channel.collectRetired();
    });
    Shipyard::Overlap::FrameChannel<Packet>::Ticket rendering;
    const Packet* packet = NULL;
    Check(channel.acquireReady(rendering, packet));
    Check(alternate.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    Check(producer.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    Check(buffers[0][0] == 0x12);
    Check(buffers[1][0] == 0x23);
    retired.store(rendering.generation);
    Check(channel.retire(rendering));
    Check(producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    producer.get();
    Check(buffers[0][0] == 0x34);
    puts("writable spans: alternation, overlap, retirement and repeated draw passed");
    return 0;
}
