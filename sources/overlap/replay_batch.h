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
}
}

#endif
