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
    assert(cache.Lookup(1, "alt/a", 7) == owned.lock());
    assert(!cache.Lookup(1, "a", 7));
    assert(!cache.Lookup(1, "alt/a", 8));
    owned.lock()->dirty = true;
    assert(!cache.Lookup(1, "alt/a", 7));
    owned.lock()->dirty = false;
    cache.Store(5, "alt/b", 7, std::shared_ptr<Texture>(new Texture()));
    assert(!cache.Lookup(1, "alt/a", 7));
    cache.Clear();
    assert(owned.expired());
    assert(!cache.Lookup(5, "alt/b", 7));
}
