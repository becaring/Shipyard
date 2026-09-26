#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Shipyard {
namespace Resources {

// Short lived, owning cache for texture commands in one display-list run.
// The archive path is checked on every lookup, so a changed hash table cannot
// silently reuse a texture from an earlier archive mapping.
template <typename Texture, size_t Capacity = 256> class TextureHashCache {
  public:
    struct Entry {
        uint64_t hash;
        unsigned generation;
        std::string path;
        std::shared_ptr<Texture> texture;
        bool valid;
        Entry() : hash(0), generation(0), valid(false) {}
    };

    std::shared_ptr<Texture> Lookup(uint64_t hash, const char* path, unsigned generation) {
        Entry& entry = mEntries[Slot(hash)];
        if (entry.valid && entry.hash == hash && entry.generation == generation &&
            entry.path == path && entry.texture && !entry.texture->IsDirty())
            return entry.texture;
        return std::shared_ptr<Texture>();
    }

    void Store(uint64_t hash, const char* path, unsigned generation,
               const std::shared_ptr<Texture>& texture) {
        Entry& entry = mEntries[Slot(hash)];
        entry.valid = false;
        entry.texture = texture;
        entry.path = path;
        entry.hash = hash;
        entry.generation = generation;
        entry.valid = true;
    }

    void Clear() {
        for (size_t i = 0; i < Capacity; ++i) {
            mEntries[i].valid = false;
            mEntries[i].texture.reset();
        }
    }

  private:
    static size_t Slot(uint64_t hash) {
        static_assert(Capacity && (Capacity & (Capacity - 1)) == 0,
                      "TextureHashCache capacity must be a power of two");
        return static_cast<size_t>((hash ^ (hash >> 32)) & (Capacity - 1));
    }
    Entry mEntries[Capacity];
};

} // namespace Resources
} // namespace Shipyard
