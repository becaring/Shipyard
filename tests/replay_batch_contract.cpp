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

    // One 150 ms hitch keeps six presentations; two overruns of a 45 ms budget
    // drop to the larger fit of the pair; quiet updates restore them only after
    // ten in a row, at the smallest fit seen.
    Shipyard::Overlap::ReplayBudget budget;
    assert(budget.observe(6, 0.030, 0.045, 6) == 6);
    assert(budget.observe(6, 0.150, 0.045, 6) == 6);
    assert(budget.observe(6, 0.030, 0.045, 6) == 6);
    assert(budget.observe(6, 0.061, 0.045, 6) == 6);
    assert(budget.observe(6, 0.061, 0.045, 6) == 4);
    assert(budget.observe(4, 0.044, 0.045, 6) == 4);
    for (int update = 0; update < 9; ++update) assert(budget.observe(4, update == 4 ? 0.036 : 0.030, 0.045, 6) == 4);
    assert(budget.observe(4, 0.044, 0.045, 6) == 4);
    for (int update = 0; update < 9; ++update) assert(budget.observe(4, update == 4 ? 0.036 : 0.030, 0.045, 6) == 4);
    assert(budget.observe(4, 0.030, 0.045, 6) == 5);
    assert(budget.observe(5, 0.500, 0.045, 6) == 5);
    assert(budget.observe(5, 0.500, 0.045, 6) == 1);
    assert(budget.observe(1, 0.0, 0.045, 3) == 1);
    assert(budget.observe(6, 0.010, 0.045, 3) == 1);
    std::puts("Interpolation cadence, final maps, owned batch isolation and replay budget passed");
}
