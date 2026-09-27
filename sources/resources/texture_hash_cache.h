#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Shipyard {
namespace Resources {

// Short lived, owning cache for texture commands in one interpolation batch.
// The archive path is checked on every lookup, so a changed hash table cannot
// silently reuse a texture from an earlier archive mapping.
template <typename Texture, size_t Capacity = 256> class TextureHashCache {
  public:
    enum { Ways = 4 };
    struct Entry {
        uint64_t hash;
        unsigned generation;
        uint64_t lastUse;
        std::string path;
        std::shared_ptr<Texture> texture;
        bool valid;
        Entry() : hash(0), generation(0), lastUse(0), valid(false) {}
    };

    TextureHashCache() : mClock(0) {}

    std::shared_ptr<Texture> Lookup(uint64_t hash, const char* path, unsigned generation) {
        const std::shared_ptr<Texture>* hit = LookupBorrowed(hash, path, generation);
        return hit ? *hit : std::shared_ptr<Texture>();
    }

    // Borrow only within the synchronous command. Store or Clear can replace
    // this slot; copy the shared_ptr before keeping the texture beyond it.
    const std::shared_ptr<Texture>* LookupBorrowed(uint64_t hash, const char* path,
                                                    unsigned generation) {
        const size_t first = SetStart(hash);
        for (size_t way = 0; way < Ways; ++way) {
            Entry& entry = mEntries[first + way];
            if (entry.valid && entry.hash == hash && entry.generation == generation &&
                entry.path == path && entry.texture && !entry.texture->IsDirty()) {
                entry.lastUse = ++mClock;
                return &entry.texture;
            }
        }
        return NULL;
    }

    void Store(uint64_t hash, const char* path, unsigned generation,
               const std::shared_ptr<Texture>& texture) {
        const size_t first = SetStart(hash);
        Entry* target = NULL;
        for (size_t way = 0; way < Ways; ++way) {
            Entry& entry = mEntries[first + way];
            if (entry.valid && entry.hash == hash && entry.path == path) {
                target = &entry;
                break;
            }
            if (!entry.valid && !target)
                target = &entry;
        }
        if (!target) {
            target = &mEntries[first];
            for (size_t way = 1; way < Ways; ++way) {
                Entry& entry = mEntries[first + way];
                if (entry.lastUse < target->lastUse)
                    target = &entry;
            }
        }
        Entry& entry = *target;
        entry.valid = false;
        entry.texture = texture;
        entry.path = path;
        entry.hash = hash;
        entry.generation = generation;
        entry.lastUse = ++mClock;
        entry.valid = true;
    }

    void Clear() {
        for (size_t i = 0; i < Capacity; ++i) {
            mEntries[i].valid = false;
            mEntries[i].texture.reset();
        }
        mClock = 0;
    }

  private:
    static size_t SetStart(uint64_t hash) {
        static_assert(Capacity >= Ways && (Capacity & (Capacity - 1)) == 0,
                      "TextureHashCache capacity must be a power of two and at least four");
        // The signed-path opcode keys by an aligned address. Mix address bits
        // above the alignment into the set index alongside archive hash bits.
        uint32_t folded = static_cast<uint32_t>(hash) ^ static_cast<uint32_t>(hash >> 32);
        folded ^= folded >> 4;
        folded ^= folded >> 9;
        folded ^= folded >> 16;
        return static_cast<size_t>(folded & ((Capacity / Ways) - 1)) * Ways;
    }
    Entry mEntries[Capacity];
    uint64_t mClock;
};

} // namespace Resources
} // namespace Shipyard
