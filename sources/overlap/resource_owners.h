// Resource owners follow both recording and replay until their last CPU reader.
// A bounded pointer cache only suppresses repeats; collisions keep extra owners.
// A cached pointer is always still owned here, so its address cannot be reused.
#ifndef SHIPYARD_OVERLAP_RESOURCE_OWNERS_H
#define SHIPYARD_OVERLAP_RESOURCE_OWNERS_H
#include <stdint.h>
#include <string.h>
#include <memory>
#include <vector>

namespace Shipyard {
namespace Overlap {
template <class Resource> class ResourceOwners {
public:
    ResourceOwners() { memset(recent_, 0, sizeof(recent_)); }
    void push_back(const std::shared_ptr<Resource>& resource) {
        const Resource* pointer = resource.get();
        if (!pointer) return;
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        const size_t slot = ((address >> 4) ^ (address >> 12)) & 255;
        if (recent_[slot] == pointer) return;
        owners_.push_back(resource);
        recent_[slot] = pointer;
    }
    // Storage stays reserved for the next packet that reads through this owner.
    void clear() {
        memset(recent_, 0, sizeof(recent_));
        owners_.clear();
    }
    size_t size() const { return owners_.size(); }
    bool empty() const { return owners_.empty(); }
    const std::shared_ptr<Resource>& operator[](size_t index) const { return owners_[index]; }

private:
    std::vector<std::shared_ptr<Resource> > owners_;
    const Resource* recent_[256];
};

// Render-side owners are reused across packets; the scope releases them after replay.
template <class Owners> class ResourceReaderScope {
public:
    explicit ResourceReaderScope(Owners& storage) : owners_(storage) {}
    ~ResourceReaderScope() { owners_.clear(); }

private:
    ResourceReaderScope(const ResourceReaderScope&);
    ResourceReaderScope& operator=(const ResourceReaderScope&);
    Owners& owners_;
};
}
}

#endif
