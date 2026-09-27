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
    enum Route { SignedPath, ArchiveHash };
    struct Entry {
        uint64_t hash;
        unsigned generation;
        unsigned archiveEpoch;
        uint64_t lastUse;
        std::string path;
        std::shared_ptr<const std::string> archivePath;
        std::shared_ptr<Texture> texture;
        std::weak_ptr<void> managerOwner;
        std::weak_ptr<void> archiveOwner;
        Route route;
        bool valid;
        Entry() : hash(0), generation(0), archiveEpoch(0), lastUse(0),
                  route(SignedPath), valid(false) {}
    };

    TextureHashCache() : mClock(0) {}

    std::shared_ptr<Texture> Lookup(uint64_t hash, const char* path, unsigned generation) {
        const size_t first = SetStart(hash);
        for (size_t way = 0; way < Ways; ++way) {
            Entry& entry = mEntries[first + way];
            if (entry.valid && entry.route == SignedPath && entry.hash == hash &&
                entry.generation == generation &&
                entry.path == path && entry.texture && !entry.texture->IsDirty()) {
                entry.lastUse = ++mClock;
                return entry.texture;
            }
        }
        return std::shared_ptr<Texture>();
    }

    template <typename Manager, typename Archive>
    bool LookupArchiveHash(uint64_t hash, unsigned generation, unsigned archiveEpoch,
                           const std::shared_ptr<Manager>& manager,
                           const std::shared_ptr<Archive>& archive,
                           std::shared_ptr<Texture>& textureOut,
                           std::shared_ptr<const std::string>& pathOut) {
        textureOut.reset();
        pathOut.reset();
        if (!manager || !archive)
            return false;
        const size_t first = SetStart(hash);
        for (size_t way = 0; way < Ways; ++way) {
            Entry& entry = mEntries[first + way];
            if (!entry.valid || entry.route != ArchiveHash || entry.hash != hash ||
                entry.generation != generation || entry.archiveEpoch != archiveEpoch ||
                !entry.archivePath || !entry.texture || entry.texture->IsDirty())
                continue;
            // Compare control blocks directly. The current shared owners keep
            // matching weak owners alive, so taking two strong locks here only
            // adds atomic reference traffic to every warm texture command.
            if (entry.managerOwner.owner_before(manager) ||
                manager.owner_before(entry.managerOwner) ||
                entry.archiveOwner.owner_before(archive) ||
                archive.owner_before(entry.archiveOwner))
                continue;
            entry.lastUse = ++mClock;
            textureOut = entry.texture;
            pathOut = entry.archivePath;
            return true;
        }
        return false;
    }

    void Store(uint64_t hash, const char* path, unsigned generation,
               const std::shared_ptr<Texture>& texture) {
        const size_t first = SetStart(hash);
        Entry* target = NULL;
        for (size_t way = 0; way < Ways; ++way) {
            Entry& entry = mEntries[first + way];
            if (entry.valid && entry.route == SignedPath && entry.hash == hash && entry.path == path) {
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
        entry.archivePath.reset();
        entry.managerOwner.reset();
        entry.archiveOwner.reset();
        entry.texture = texture;
        entry.path = path;
        entry.hash = hash;
        entry.generation = generation;
        entry.route = SignedPath;
        entry.lastUse = ++mClock;
        entry.valid = true;
    }

    template <typename Manager, typename Archive>
    void StoreArchiveHash(uint64_t hash, const char* path, unsigned generation,
                          unsigned archiveEpoch, const std::shared_ptr<Texture>& texture,
                          const std::shared_ptr<Manager>& manager,
                          const std::shared_ptr<Archive>& archive) {
        if (!path || !texture || texture->IsDirty() || !manager || !archive)
            return;
        const size_t first = SetStart(hash);
        Entry* target = NULL;
        for (size_t way = 0; way < Ways; ++way) {
            Entry& entry = mEntries[first + way];
            if (entry.valid && entry.route == ArchiveHash && entry.hash == hash) {
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
        entry.path.clear();
        entry.archivePath.reset(new std::string(path));
        entry.managerOwner = std::shared_ptr<void>(manager);
        entry.archiveOwner = std::shared_ptr<void>(archive);
        entry.texture = texture;
        entry.hash = hash;
        entry.generation = generation;
        entry.archiveEpoch = archiveEpoch;
        entry.route = ArchiveHash;
        entry.lastUse = ++mClock;
        entry.valid = true;
    }

    void Clear() {
        for (size_t i = 0; i < Capacity; ++i) {
            mEntries[i].valid = false;
            mEntries[i].texture.reset();
            mEntries[i].archivePath.reset();
            mEntries[i].managerOwner.reset();
            mEntries[i].archiveOwner.reset();
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
