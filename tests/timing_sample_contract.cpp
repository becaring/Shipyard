// A game-thread sample merges into render-owned totals after a handoff.
#include <cassert>
#include <cstdio>
#include <thread>
#include "diagnostics/timing_totals.h"

int main() {
    typedef Shipyard::Diagnostics::TimingSample<3> Sample;
    Shipyard::Diagnostics::TimingTotals<3> totals;
    totals.Add(0, 5);
    Sample off;
    off.Begin(0, 10);
    off.End(0, 20);
    assert(off.totals.calls[0] == 0);
    Sample sample;
    std::thread game([&sample]() {
        sample.Reset(true);
        sample.End(1, 7);  // no Begin: ignored
        sample.Begin(0, 10);
        sample.End(0, 19);
        sample.Begin(0, 30);
        sample.End(0, 32);
        sample.Begin(2, 1);
        sample.Begin(9, 1);  // out of range: ignored
        sample.End(9, 2);
    });
    game.join();
    // An open stage never reaches the totals.
    assert(sample.totals.calls[2] == 0 && sample.totals.calls[1] == 0);
    totals.Merge(sample.totals);
    assert(totals.calls[0] == 3 && totals.total[0] == 16 && totals.maximum[0] == 9);
    sample.Reset(false);
    assert(!sample.enabled && sample.totals.calls[0] == 0 && sample.starts[2] == 0);
    puts("timing sample: thread-owned merge passed");
}
