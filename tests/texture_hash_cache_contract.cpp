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
}
