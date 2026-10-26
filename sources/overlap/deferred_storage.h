// Game-owned release records keep allocation and debug arguments through replay.
// Arena teardown drains prior replay before consuming its pending releases.
#ifndef SHIPYARD_OVERLAP_DEFERRED_STORAGE_H
#define SHIPYARD_OVERLAP_DEFERRED_STORAGE_H
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

namespace Shipyard {
namespace Overlap {
class DeferredStorage {
public:
    typedef void (*Release)(void* owner, void* data, const char* file, int line);
    template <class Worker>
    void defer(Worker& worker, Release callback, void* owner, void* data,
               const char* file, int line) {
        if (!data) return;
        std::shared_ptr<Record> release(new Record(callback, owner, data, file, line,
                                                  worker.buildingGeneration()));
        pending_.push_back(release);
        worker.deferRelease([release]() { release->run(); });
    }
    void collect() {
        for (size_t i = 0; i < pending_.size();) {
            if (pending_[i]->consumed) {
                pending_[i] = pending_.back();
                pending_.pop_back();
            } else ++i;
        }
    }
    // Call only before resetting/destroying this arena, before recording new data.
    template <class Worker> bool drainOwner(Worker& worker, void* owner) {
        if (!worker.fencePreviousReplay()) return false;
        for (size_t i = 0; i != pending_.size(); ++i) {
            if (pending_[i]->owner == owner) pending_[i]->run();
        }
        collect();
        return true;
    }
    template <class Worker> bool retryAllocation(Worker& worker) {
        if (!worker.fencePreviousReplay()) return false;
        const uint64_t retired = worker.previousGeneration();
        for (size_t i = 0; i != pending_.size(); ++i) {
            if (pending_[i]->generation <= retired) pending_[i]->run();
        }
        collect();
        return true;
    }

private:
    struct Record {
        DeferredStorage::Release callback;
        void* owner;
        void* data;
        std::string file;
        int line;
        uint64_t generation;
        bool hasFile, consumed;
        Record(DeferredStorage::Release operation, void* arena, void* allocation,
                const char* source, int sourceLine, uint64_t frame)
            : callback(operation), owner(arena), data(allocation), file(source ? source : ""),
              line(sourceLine), generation(frame), hasFile(source != NULL), consumed(false) {}
        void run() {
            if (consumed) return;
            consumed = true;
            callback(owner, data, hasFile ? file.c_str() : NULL, line);
        }
    };
    std::vector<std::shared_ptr<Record> > pending_;
};
}
}

#endif
