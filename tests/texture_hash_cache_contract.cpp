#include "resources/texture_hash_cache.h"
#include <cassert>
#include <memory>

struct Texture {
    bool dirty;
    explicit Texture(bool value = false) : dirty(value) {}
    bool IsDirty() const { return dirty; }
};

int main() {
    Shipyard::Resources::TextureHashCache<Texture, 4> cache;
    std::shared_ptr<Texture> first(new Texture());
    std::weak_ptr<Texture> owned = first;
    cache.Store(1, "alt/a", 7, first);
    first.reset();
    assert(!owned.expired());
    const std::shared_ptr<Texture>* borrowed = cache.LookupBorrowed(1, "alt/a", 7);
    assert(borrowed && *borrowed == owned.lock());
    const long ownerCount = borrowed->use_count();
    assert(cache.LookupBorrowed(1, "alt/a", 7)->use_count() == ownerCount);
    assert(!cache.LookupBorrowed(1, "a", 7));
    assert(!cache.LookupBorrowed(1, "alt/a", 8));
    assert(cache.Lookup(1, "alt/a", 7) == owned.lock());
    assert(!cache.Lookup(1, "a", 7));
    assert(!cache.Lookup(1, "alt/a", 8));
    owned.lock()->dirty = true;
    assert(!cache.Lookup(1, "alt/a", 7));
    assert(!cache.LookupBorrowed(1, "alt/a", 7));
    owned.lock()->dirty = false;
    cache.Store(5, "alt/b", 7, std::shared_ptr<Texture>(new Texture()));
    // Four different keys that collide in the old direct-mapped slot must
    // survive repeated interpolation renders within the same cache batch.
    assert(cache.Lookup(1, "alt/a", 7) == owned.lock());
    assert(cache.Lookup(5, "alt/b", 7));
    cache.Store(9, "alt/c", 7, std::shared_ptr<Texture>(new Texture()));
    cache.Store(13, "alt/d", 7, std::shared_ptr<Texture>(new Texture()));
    assert(cache.Lookup(1, "alt/a", 7));
    assert(cache.Lookup(5, "alt/b", 7));
    assert(cache.Lookup(9, "alt/c", 7));
    assert(cache.Lookup(13, "alt/d", 7));
    cache.Store(17, "alt/e", 7, std::shared_ptr<Texture>(new Texture()));
    assert(!cache.Lookup(1, "alt/a", 7));
    assert(cache.Lookup(17, "alt/e", 7));
    cache.Clear();
    assert(owned.expired());
    assert(!cache.Lookup(5, "alt/b", 7));

    // Signed texture paths use aligned addresses as keys. A cache that only
    // indexes their low address bits repeatedly evicts them during replay.
    Shipyard::Resources::TextureHashCache<Texture> aligned;
    for (unsigned i = 0; i < 32; ++i) {
        std::string path("tex/");
        path.push_back(static_cast<char>('A' + i));
        aligned.Store(0x10000000u + i * 16u, path.c_str(), 7,
                      std::shared_ptr<Texture>(new Texture()));
    }
    for (unsigned i = 0; i < 32; ++i) {
        std::string path("tex/");
        path.push_back(static_cast<char>('A' + i));
        assert(aligned.Lookup(0x10000000u + i * 16u, path.c_str(), 7));
    }

    // An archive certificate can skip hash-to-path resolution only while the
    // same archive manager, map epoch, resource generation and clean owner live.
    Shipyard::Resources::TextureHashCache<Texture, 4> versioned;
    int archiveA = 0, archiveB = 0;
    std::shared_ptr<Texture> live(new Texture());
    versioned.StoreArchive(31, "alt/first", 9, &archiveA, 4, live);
    const auto* hit = versioned.LookupArchive(31, &archiveA, 4, 9);
    assert(hit && hit->path == "alt/first" && hit->texture == live);
    assert(!versioned.LookupArchive(31, &archiveB, 4, 9));
    assert(!versioned.LookupArchive(31, &archiveA, 5, 9));
    assert(!versioned.LookupArchive(31, &archiveA, 4, 10));
    assert(versioned.LookupAndCertifyArchive(31, "alt/first", 9, &archiveA, 5));
    assert(versioned.LookupArchive(31, &archiveA, 5, 9));
    assert(!versioned.LookupAndCertifyArchive(31, "alt/second", 9, &archiveA, 6));
    versioned.StoreArchive(31, "alt/second", 9, &archiveA, 6,
                           std::shared_ptr<Texture>(new Texture()));
    assert(versioned.LookupArchive(31, &archiveA, 6, 9)->path == "alt/second");
    assert(!versioned.LookupArchive(31, &archiveA, 5, 9));
    versioned.LookupArchive(31, &archiveA, 6, 9)->texture->dirty = true;
    assert(!versioned.LookupArchive(31, &archiveA, 6, 9));
    versioned.Clear();
    assert(!versioned.LookupArchive(31, &archiveA, 6, 9));

    // A direct-path command with a numerically equal key cannot acquire an
    // archive certificate accidentally.
    versioned.Store(31, "alt/second", 9, live);
    assert(!versioned.LookupArchive(31, &archiveA, 6, 9));

    // Compare a path-resolving baseline with the epoch route through archive
    // remaps, generation changes, dirty textures, collisions and manager swap.
    Shipyard::Resources::TextureHashCache<Texture, 4> baseline, trial;
    std::shared_ptr<Texture> variants[5][2];
    unsigned selected[5] = { 0, 0, 0, 0, 0 };
    for (unsigned key = 0; key < 5; ++key)
        for (unsigned variant = 0; variant < 2; ++variant)
            variants[key][variant].reset(new Texture());
    unsigned epoch = 1, generation = 1;
    const void* manager = &archiveA;
    for (unsigned call = 0; call < 3000; ++call) {
        const unsigned key = (call * 7) % 5;
        const uint64_t hash = 1 + 4 * key;
        if (call % 31 == 0) {
            selected[key] ^= 1;
            ++epoch;
        }
        if (call % 67 == 0)
            ++generation;
        if (call % 113 == 0)
            manager = manager == &archiveA ? &archiveB : &archiveA;
        if (call % 173 == 0) {
            baseline.Clear();
            trial.Clear();
        }
        const std::string path = std::string(selected[key] ? "alt/" : "base/") +
                                 static_cast<char>('a' + key);
        std::shared_ptr<Texture> authoritative = variants[key][selected[key]];
        authoritative->dirty = call % 29 == 0;
        const std::shared_ptr<Texture>* oldHit = baseline.LookupBorrowed(hash, path.c_str(), generation);
        std::shared_ptr<Texture> oldResult = oldHit ? *oldHit : authoritative;
        if (!oldHit && !oldResult->IsDirty())
            baseline.Store(hash, path.c_str(), generation, oldResult);

        const auto* certified = trial.LookupArchive(hash, manager, epoch, generation);
        if (!certified)
            certified = trial.LookupAndCertifyArchive(hash, path.c_str(), generation, manager, epoch);
        std::shared_ptr<Texture> newResult = certified ? certified->texture : authoritative;
        if (!certified && !newResult->IsDirty())
            trial.StoreArchive(hash, path.c_str(), generation, manager, epoch, newResult);
        assert(oldResult == newResult);
    }
}
