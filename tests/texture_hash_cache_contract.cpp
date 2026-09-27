#include "resources/texture_hash_cache.h"
#include <cassert>
#include <map>
#include <memory>

struct Texture {
    bool dirty;
    explicit Texture(bool value = false) : dirty(value) {}
    bool IsDirty() const { return dirty; }
};

struct FakeArchive {
    unsigned epoch;
    unsigned probes;
    std::map<uint64_t, std::string> names;
    FakeArchive() : epoch(1), probes(0) {}
    const char* Resolve(uint64_t hash) {
        ++probes;
        const std::map<uint64_t, std::string>::const_iterator found = names.find(hash);
        return found == names.end() ? NULL : found->second.c_str();
    }
    void Write(uint64_t hash, const char* path) { ++epoch; names[hash] = path; }
    void Reset() { ++epoch; names.clear(); }
};

struct FakeManager {
    unsigned generation;
    unsigned loads;
    bool alternate;
    std::shared_ptr<FakeArchive> archive;
    std::map<std::string, std::shared_ptr<Texture> > resources;
    explicit FakeManager(const std::shared_ptr<FakeArchive>& value)
        : generation(1), loads(0), alternate(false), archive(value) {}
    std::shared_ptr<Texture> Load(const char* path) {
        ++loads;
        const std::string key = alternate ? std::string("alt/") + path : path;
        const std::map<std::string, std::shared_ptr<Texture> >::const_iterator found = resources.find(key);
        return found == resources.end() ? std::shared_ptr<Texture>() : found->second;
    }
};

struct RouteResult {
    std::shared_ptr<Texture> texture;
    std::string path;
};

static RouteResult ReferenceRoute(uint64_t hash, const std::shared_ptr<FakeManager>& manager) {
    RouteResult result;
    const char* path = manager->archive->Resolve(hash);
    if (path) {
        result.path = path;
        result.texture = manager->Load(path);
    }
    return result;
}

static RouteResult FastRoute(uint64_t hash, const std::shared_ptr<FakeManager>& manager,
                             Shipyard::Resources::TextureHashCache<Texture, 4>& cache) {
    RouteResult result;
    const std::shared_ptr<FakeArchive> archive = manager->archive;
    const unsigned generation = manager->generation;
    const unsigned epoch = archive->epoch;
    const Shipyard::Resources::TextureHashCache<Texture, 4>::ArchiveHashHit hit =
        cache.LookupArchiveHash(hash, generation, epoch, manager, archive);
    if (hit.texture && manager->generation == generation && archive->epoch == epoch) {
        result.texture = hit.texture;
        result.path = *hit.path;
        return result;
    }
    const char* path = archive->Resolve(hash);
    if (!path)
        return result;
    result.path = path;
    result.texture = manager->Load(path);
    if (result.texture && !result.texture->IsDirty() &&
        manager->generation == generation && archive->epoch == epoch)
        cache.StoreArchiveHash(hash, path, generation, epoch, result.texture, manager, archive);
    return result;
}

static void AssertSameRoute(uint64_t hash, const std::shared_ptr<FakeManager>& manager,
                            Shipyard::Resources::TextureHashCache<Texture, 4>& cache) {
    const RouteResult expected = ReferenceRoute(hash, manager);
    const RouteResult actual = FastRoute(hash, manager, cache);
    assert(actual.path == expected.path);
    assert(actual.texture == expected.texture);
}

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

    // A warm archive-hash hit skips map and loader; its path remains owned
    // after the archive changes or its cache entry is replaced.
    Shipyard::Resources::TextureHashCache<Texture, 4> routed;
    std::shared_ptr<FakeArchive> archive(new FakeArchive());
    std::shared_ptr<FakeManager> manager(new FakeManager(archive));
    std::shared_ptr<Texture> a(new Texture()), b(new Texture()), alt(new Texture());
    archive->Write(1, "a");
    manager->resources["a"] = a;
    manager->resources["b"] = b;
    manager->resources["alt/b"] = alt;
    AssertSameRoute(1, manager, routed);
    const unsigned probes = archive->probes, loads = manager->loads;
    const RouteResult warm = FastRoute(1, manager, routed);
    assert(warm.texture == a && warm.path == "a");
    assert(archive->probes == probes && manager->loads == loads);
    archive->Write(1, "b");
    AssertSameRoute(1, manager, routed);
    assert(warm.path == "a" && FastRoute(1, manager, routed).texture == b);
    archive->Reset();
    AssertSameRoute(1, manager, routed);
    assert(!FastRoute(1, manager, routed).texture);
    archive->Write(1, "b");
    AssertSameRoute(1, manager, routed);
    manager->alternate = true;
    ++manager->generation;
    AssertSameRoute(1, manager, routed);
    assert(FastRoute(1, manager, routed).texture == alt);
    alt->dirty = true;
    manager->resources["alt/b"] = std::shared_ptr<Texture>(new Texture());
    AssertSameRoute(1, manager, routed);
    assert(FastRoute(1, manager, routed).texture == manager->resources["alt/b"]);
    manager->resources.erase("alt/b");
    ++manager->generation; // unload invalidates the owning cache
    AssertSameRoute(1, manager, routed);
    assert(!FastRoute(1, manager, routed).texture);
    manager->alternate = false;
    ++manager->generation;
    manager->resources.erase("b");
    AssertSameRoute(1, manager, routed); // null result is never installed
    assert(!FastRoute(1, manager, routed).texture);

    // Replacing either owner rejects an entry even with identical epoch and
    // generation. The cache holds owners weakly, so destruction is possible.
    manager->resources["b"] = b;
    AssertSameRoute(1, manager, routed);
    std::weak_ptr<FakeManager> oldManager = manager;
    std::shared_ptr<FakeManager> replacement(new FakeManager(archive));
    replacement->generation = manager->generation;
    replacement->resources["b"] = std::shared_ptr<Texture>(new Texture());
    AssertSameRoute(1, replacement, routed);
    assert(FastRoute(1, replacement, routed).texture == replacement->resources["b"]);
    manager.reset();
    assert(oldManager.expired());
    std::shared_ptr<FakeArchive> otherArchive(new FakeArchive());
    otherArchive->epoch = archive->epoch;
    otherArchive->names[1] = "other";
    replacement->archive = otherArchive;
    replacement->resources["other"] = std::shared_ptr<Texture>(new Texture());
    AssertSameRoute(1, replacement, routed);
    assert(FastRoute(1, replacement, routed).path == "other");

    // Signed-path and archive-hash routes cannot cross-hit at equal hashes.
    routed.Store(1, "other", replacement->generation, b);
    assert(routed.Lookup(1, "other", replacement->generation) == b);
    assert(FastRoute(1, replacement, routed).texture == replacement->resources["other"]);
    routed.Clear();
    const unsigned colliding[] = {1, 5, 9, 13};
    for (unsigned i = 0; i < 4; ++i)
        routed.StoreArchiveHash(colliding[i], "other", replacement->generation,
                                otherArchive->epoch, replacement->resources["other"],
                                replacement, otherArchive);
    for (unsigned i = 0; i < 4; ++i)
        assert(routed.LookupArchiveHash(colliding[i], replacement->generation,
                                       otherArchive->epoch, replacement, otherArchive).texture);
    const Shipyard::Resources::TextureHashCache<Texture, 4>::ArchiveHashHit retained =
        routed.LookupArchiveHash(1, replacement->generation, otherArchive->epoch,
                                 replacement, otherArchive);
    const char* retainedName = retained.path->c_str();
    routed.Clear();
    assert(retained.texture == replacement->resources["other"]);
    assert(std::string(retainedName) == "other");
    routed.StoreArchiveHash(1, "new path", replacement->generation, otherArchive->epoch,
                            replacement->resources["other"], replacement, otherArchive);
    assert(std::string(retainedName) == "other");
}
