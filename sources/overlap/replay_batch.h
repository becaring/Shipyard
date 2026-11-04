// The game owner prepares every replacement map before publishing a frame.
// Cadence matches the sequential bridge, including empty final-frame maps.
#ifndef SHIPYARD_OVERLAP_REPLAY_BATCH_H
#define SHIPYARD_OVERLAP_REPLAY_BATCH_H
#include <vector>

namespace Shipyard {
namespace Overlap {
template <class Map> struct ReplayBatch {
    int fps, gameFps, updateRate;
    std::vector<Map> maps;
    ReplayBatch() : fps(0), gameFps(0), updateRate(0) {}
};

class ReplayCadence {
public:
    ReplayCadence() : lastFps_(0), lastUpdateRate_(0), time_(0) {}
    template <class Map, class Recorder>
    ReplayBatch<Map> prepare(int targetFps, int updateRate, bool debugging, Recorder record) {
        ReplayBatch<Map> batch;
        batch.updateRate = updateRate;
        batch.gameFps = 60 / updateRate;
        batch.fps = targetFps == 20 || batch.gameFps > targetFps ? batch.gameFps : targetFps;
        if (lastFps_ != batch.fps || lastUpdateRate_ != updateRate) time_ = 0;
        if (batch.gameFps > 0 && time_ + batch.gameFps <= batch.fps)
            batch.maps.reserve((batch.fps - time_) / batch.gameFps);
        while (time_ + batch.gameFps <= batch.fps) {
            time_ += batch.gameFps;
            if (time_ != batch.fps) batch.maps.push_back(record(static_cast<float>(time_) / batch.fps));
            else batch.maps.push_back(Map());
        }
        time_ -= batch.fps;
        if (debugging) {
            batch.maps.clear();
            batch.maps.push_back(Map());
        }
        lastFps_ = batch.fps;
        lastUpdateRate_ = updateRate;
        return batch;
    }
private:
    int lastFps_, lastUpdateRate_, time_;
};

// Without a display clock, one update's presentations that overrun the update
// period hold the game back, so it runs in slow motion. The budget drops
// interpolated presentations instead and restores them once the scene has
// left room for half a second. The average presentation costs at least as
// much as one more, so the fit never grows into an overrun. A drop needs two
// overruns in a row: a single load hitch is not the scene's cost.
class ReplayBudget {
public:
    ReplayBudget() : count_(0), last_(0), raise_(0), calm_(0) {}
    // `elapsed` is the time `count` presentations took and `period` the time
    // they may take; returns how many presentations later updates get.
    unsigned observe(unsigned count, double elapsed, double period, unsigned most) {
        if (!most) most = 1;
        if (!count_ || count_ > most) count_ = most;
        double fit = elapsed > 0 ? count * period / elapsed : most;
        if (fit > most) fit = most;
        const unsigned target = fit < 1 ? 1 : static_cast<unsigned>(fit);
        const unsigned sustained = last_ > target ? last_ : target;
        last_ = target;
        if (sustained < count_) {
            count_ = sustained;
            calm_ = 0;
        } else if (target < count_) {
            calm_ = 0;
        } else if (target > count_) {
            raise_ = calm_ && raise_ < target ? raise_ : target;
            if (++calm_ >= CalmUpdates) {
                count_ = raise_;
                calm_ = 0;
            }
        } else {
            calm_ = 0;
        }
        return count_;
    }
private:
    enum { CalmUpdates = 10 };
    unsigned count_, last_, raise_, calm_;
};
}
}

#endif
