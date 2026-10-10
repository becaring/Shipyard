// Normal request batches use inline storage. Overflow preserves every sample;
// coordinate policy and the native read callback belong to the caller.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace Shipyard {
namespace Fast3D {


template<class Sample, unsigned Capacity = 64> class DepthSamples {
    struct Entry { Sample sample; bool ready; };
    Entry fixed[Capacity];
    unsigned count;
    std::vector<Entry> overflow;
    Entry& At(size_t index) { return index < Capacity ? fixed[index] : overflow[index - Capacity]; }
    size_t Size() const { return count + overflow.size(); }
    Entry* Find(float x, float y) {
        for (size_t i = 0; i < Size(); ++i) {
            Entry& entry = At(i);
            if (entry.sample.x == x && entry.sample.y == y) return &entry;
        }
        return NULL;
    }
    Entry& Add(float x, float y) {
        Entry* found = Find(x, y);
        if (found) return *found;
        Entry entry = {{x, y, 0}, false};
        if (count < Capacity) { fixed[count] = entry; return fixed[count++]; }
        overflow.push_back(entry);
        return overflow.back();
    }
  public:
    DepthSamples() : count(0) {}
    void Clear() { count = 0; overflow.clear(); }
    void Prepare(float x, float y) { Add(x, y); }
    uint16_t Read(int framebuffer, float x, float y,
                  void (*read)(int, Sample*, size_t)) {
        Entry& requested = Add(x, y);
        if (requested.ready) return requested.sample.depth;
        Sample samples[Capacity];
        Entry* destinations[Capacity];
        unsigned pending = 0;
        for (size_t i = 0; i < Size(); ++i) {
            Entry& entry = At(i);
            if (entry.ready) continue;
            samples[pending] = entry.sample;
            destinations[pending++] = &entry;
            if (pending == Capacity) {
                read(framebuffer, samples, pending);
                for (unsigned j = 0; j < pending; ++j) {
                    destinations[j]->sample.depth = samples[j].depth;
                    destinations[j]->ready = true;
                }
                pending = 0;
            }
        }
        if (pending) {
            read(framebuffer, samples, pending);
            for (unsigned j = 0; j < pending; ++j) {
                destinations[j]->sample.depth = samples[j].depth;
                destinations[j]->ready = true;
            }
        }
        return requested.sample.depth;
    }
};

} // namespace Fast3D
} // namespace Shipyard
