// Reuse must preserve collisions, resource ownership, dirty fallback and batch lifetime.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include "fast3d/texture_replay.h"
#include "fast3d/material_memo.h"

struct Texture {
    bool dirty;
    unsigned identity;
    explicit Texture(unsigned value) : dirty(false), identity(value) {}
    bool IsDirty() const { return dirty; }
};
struct Metadata { std::shared_ptr<Texture> resource; unsigned width; };
struct Key {
    unsigned input;
    bool operator==(const Key& other) const { return input == other.input; }
};
struct Builder {
    unsigned input, calls;
    void Rebuild(unsigned& result) { ++calls; result = input * 17; }
};

int main() {
    typedef Shipyard::Fast3D::TextureReplayCache<Metadata> Textures;
    Textures cache;
    int owner, second;
    Metadata metadata = {std::make_shared<Texture>(7), 128};
    std::weak_ptr<Texture> lifetime = metadata.resource;
    cache.Begin(3);
    std::string path = "textures/reusable";
    cache.Store(&owner, 37, path.c_str(), 5, metadata);
    path.assign(4000, 'x');
    metadata.resource.reset();
    for (unsigned replay = 0; replay < 3; ++replay) {
        const Textures::Entry* hit = cache.Lookup(&owner, 37);
        assert(hit && hit->path == "textures/reusable" && hit->flags == 5);
        assert(hit->metadata.width == 128 && hit->metadata.resource->identity == 7);
    }
    assert(!cache.Lookup(&second, 37));
    auto resource = lifetime.lock();
    resource->dirty = true;
    assert(!cache.Lookup(&owner, 37));
    resource->dirty = false;
    assert(cache.Lookup(&owner, 37));
    resource.reset();
    cache.End();
    assert(lifetime.expired() && !cache.Lookup(&owner, 37));
    cache.Begin(1);
    metadata.resource = std::make_shared<Texture>(9);
    cache.Store(&owner, 37, "disabled", 0, metadata);
    assert(!cache.Lookup(&owner, 37));
    cache.Begin(3);
    unsigned cases = 0;
    for (unsigned id = 0; id < 5000; ++id) {
        metadata.resource = std::make_shared<Texture>(id);
        metadata.width = id;
        cache.Store(&owner, id, "collisions", id ^ 7, metadata);
        const Textures::Entry* hit = cache.Lookup(&owner, id);
        assert(hit && hit->metadata.resource->identity == id && hit->metadata.width == id);
        if (id) {
            hit = cache.Lookup(&owner, id - 1);
            assert(!hit || hit->metadata.resource->identity == id - 1);
        }
        ++cases;
    }
    cache.Clear();
    assert(!cache.Lookup(&owner, 4999));
    cache.End();

    Shipyard::Fast3D::MaterialMemo<Key, unsigned, 256> values = {};
    static_assert(sizeof(values) == sizeof(void*), "Static TLS retains only a pointer");
    Builder builder = {0, 0};
    for (unsigned replay = 0; replay < 3; ++replay)
        for (unsigned id = 0; id < 120; ++id) {
            builder.input = id;
            assert(values.Load(Key{id}, id, builder) == id * 17);
        }
    assert(builder.calls == 120);
    builder.input = 999;
    assert(values.Load(Key{999}, 0, builder) == 999 * 17);
    builder.input = 0;
    assert(values.Load(Key{0}, 0, builder) == 0);
    values.Clear();
    unsigned before = builder.calls;
    values.Load(Key{0}, 0, builder);
    assert(builder.calls == before + 1);
    values.Release();
    assert(!values.entries);
    values.Load(Key{0}, 0, builder);
    values.Release();
    std::printf("%u texture reuse cases; material replay, collisions and lifetimes passed\n", cases);
}
