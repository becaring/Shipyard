// Fraction batches must remain independent after the next game recording begins.
#include "overlap/replay_batch.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>

typedef std::map<unsigned, float> Map;
struct Recorder {
    unsigned generation;
    Recorder() : generation(1) {}
    Map operator()(float fraction) const {
        Map map;
        map[generation] = fraction;
        return map;
    }
};

int main() {
    Shipyard::Overlap::ReplayCadence cadence;
    Recorder recorder;
    Shipyard::Overlap::ReplayBatch<Map> first = cadence.prepare<Map>(60, 3, false, recorder);
    assert(first.fps == 60 && first.gameFps == 20 && first.updateRate == 3);
    assert(first.maps.size() == 3 && first.maps[2].empty());
    assert(std::fabs(first.maps[0][1] - 1.0f / 3.0f) < 0.00001f);
    assert(std::fabs(first.maps[1][1] - 2.0f / 3.0f) < 0.00001f);
    recorder.generation = 2;
    Shipyard::Overlap::ReplayBatch<Map> next = cadence.prepare<Map>(50, 3, false, recorder);
    assert(next.maps.size() == 2);
    assert(std::fabs(next.maps[0][2] - 0.4f) < 0.00001f);
    next = cadence.prepare<Map>(50, 3, false, recorder);
    assert(next.maps.size() == 3 && next.maps.back().empty());
    assert(std::fabs(next.maps[0][2] - 0.2f) < 0.00001f);
    assert(first.maps[0].count(1) == 1 && first.maps[0].count(2) == 0);
    next = cadence.prepare<Map>(20, 2, false, recorder);
    assert(next.fps == 30 && next.maps.size() == 1 && next.maps[0].empty());
    next = cadence.prepare<Map>(60, 1, true, recorder);
    assert(next.maps.size() == 1 && next.maps[0].empty());
    next = cadence.prepare<Map>(60, 3, false, recorder);
    assert(next.maps.size() == 3 && next.maps[2].empty());
    std::puts("Interpolation cadence, final maps and owned batch isolation passed");
}
