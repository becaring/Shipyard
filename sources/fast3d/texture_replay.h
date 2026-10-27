// Texture resolutions belong to one immutable command batch. Entries own both
// the resource and path; dirty resources and explicit invalidations still miss.
// Certify before each lookup: entries hold only under the lookup generation and
// archive epoch current when they were resolved.
#pragma once
#include <stdint.h>
#include <string>

namespace Shipyard {
namespace Fast3D {


template<class Metadata, unsigned Capacity = 512> struct TextureReplayCache {
    struct Entry {
        const void* owner;
        uint64_t hash;
        unsigned flags;
        Metadata metadata;
        std::string path;
        Entry() : owner(NULL), hash(0), flags(0) {}
    };
    Entry entries[Capacity];
    bool active;
    mutable unsigned hits, misses;
    unsigned stores, clears;
    unsigned generation, epoch;
    TextureReplayCache() : active(false), hits(0), misses(0), stores(0), clears(0), generation(0), epoch(0) {}
    static unsigned Bucket(uint64_t hash) {
        unsigned folded = static_cast<unsigned>(hash) ^ static_cast<unsigned>(hash >> 32);
        folded ^= folded >> 11;
        folded ^= folded >> 5;
        return folded & (Capacity - 1);
    }
    void Clear() {
        ++clears;
        for (unsigned i = 0; i < Capacity; ++i) {
            entries[i].owner = NULL;
            entries[i].metadata.resource.reset();
        }
    }
    void Begin(unsigned replays) { Clear(); active = replays > 1; hits = misses = stores = clears = 0; }
    void End() { active = false; Clear(); }
    void Certify(unsigned lookupGeneration, unsigned archiveEpoch) {
        if (lookupGeneration == generation && archiveEpoch == epoch) return;
        Clear();
        generation = lookupGeneration;
        epoch = archiveEpoch;
    }
    const Entry* Lookup(const void* owner, uint64_t hash) const {
        if (!active) return NULL;
        const unsigned first = Bucket(hash);
        for (unsigned probe = 0; probe < 8; ++probe) {
            const Entry& entry = entries[(first + probe) & (Capacity - 1)];
            if (entry.owner == owner && entry.hash == hash && entry.metadata.resource &&
                !entry.metadata.resource->IsDirty()) { ++hits; return &entry; }
        }
        ++misses;
        return NULL;
    }
    void Store(const void* owner, uint64_t hash, const char* path, unsigned flags, const Metadata& metadata) {
        if (!active || !metadata.resource || metadata.resource->IsDirty()) return;
        ++stores;
        const unsigned first = Bucket(hash);
        Entry* target = &entries[first];
        for (unsigned probe = 0; probe < 8; ++probe) {
            Entry& entry = entries[(first + probe) & (Capacity - 1)];
            if (!entry.owner || (entry.owner == owner && entry.hash == hash)) { target = &entry; break; }
        }
        target->owner = NULL;
        target->path = path;
        target->metadata = metadata;
        target->flags = flags;
        target->hash = hash;
        target->owner = owner;
    }
};

} // namespace Fast3D
} // namespace Shipyard
