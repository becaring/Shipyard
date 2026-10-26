// The game owner tracks persistent buffers borrowed by recorded display lists.
// Alternating buffers stay writable; reuse waits for the last older reader.
#ifndef SHIPYARD_OVERLAP_WRITABLE_SPANS_H
#define SHIPYARD_OVERLAP_WRITABLE_SPANS_H
#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace Shipyard {
namespace Overlap {
class WritableSpans {
public:
    uint64_t LatestReader(const void* pointer, size_t bytes, uint64_t generation, uint64_t retired) const {
        if (!pointer || !bytes) return 0;
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        uint64_t result = 0;
        for (size_t i = 0; i < spans_.size(); ++i) {
            const Span& span = spans_[i];
            if (span.generation <= retired || span.generation >= generation) continue;
            const bool overlaps = address <= span.address ? span.address - address < bytes
                                                         : address - span.address < span.bytes;
            if (overlaps && span.generation > result) result = span.generation;
        }
        return result;
    }
    void Record(const void* pointer, size_t bytes, uint64_t generation, uint64_t retired) {
        for (size_t i = 0; i < spans_.size();) {
            if (spans_[i].generation <= retired) {
                spans_[i] = spans_.back();
                spans_.pop_back();
            } else {
                ++i;
            }
        }
        if (!pointer || !bytes) return;
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        for (size_t i = 0; i < spans_.size(); ++i) {
            Span& span = spans_[i];
            if (span.address == address && span.bytes == bytes) {
                span.generation = generation;
                return;
            }
        }
        spans_.push_back(Span(address, bytes, generation));
    }
    void Reset() { spans_.clear(); }
    size_t Count() const { return spans_.size(); }
private:
    struct Span {
        uintptr_t address;
        size_t bytes;
        uint64_t generation;
        Span(uintptr_t value, size_t size, uint64_t reader) : address(value), bytes(size), generation(reader) {}
    };
    std::vector<Span> spans_;
};
}
}

#endif
