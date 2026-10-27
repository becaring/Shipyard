// Packet resource owners: repeats retain once, every distinct owner survives until clear.
#include "overlap/resource_owners.h"
#include <cassert>
#include <cstdio>
#include <vector>

struct Resource {
    int id;
    explicit Resource(int value) : id(value) {}
};

typedef Shipyard::Overlap::ResourceOwners<Resource> Owners;

static bool owns(const Owners& owners, const Resource* resource) {
    for (size_t i = 0; i != owners.size(); ++i)
        if (owners[i].get() == resource) return true;
    return false;
}

int main() {
    Owners owners;
    std::shared_ptr<Resource> first(new Resource(1));
    for (int i = 0; i != 100; ++i) owners.push_back(first);
    assert(owners.size() == 1 && first.use_count() == 2);
    owners.push_back(std::shared_ptr<Resource>());
    assert(owners.size() == 1);

    // Many distinct owners: each is kept; collisions may keep extra copies, never fewer.
    std::vector<std::shared_ptr<Resource> > many;
    for (int i = 0; i != 2000; ++i) many.push_back(std::shared_ptr<Resource>(new Resource(i)));
    for (size_t i = 0; i != many.size(); ++i) owners.push_back(many[i]);
    for (size_t i = 0; i != many.size(); ++i) owners.push_back(many[i]);
    assert(owners.size() >= many.size() + 1 && owners.size() <= 2 * many.size() + 1);
    for (size_t i = 0; i != many.size(); ++i) assert(owns(owners, many[i].get()));
    assert(owns(owners, first.get()));

    // A packet copy (FrameChannel reset) keeps the same answers.
    Owners copy = owners;
    assert(copy.size() == owners.size());
    const size_t before = copy.size();
    copy.push_back(many.back());
    assert(copy.size() == before);

    // The last owner keeps a resource alive after its producer drops it.
    Resource* raw;
    {
        Owners packet;
        {
            std::shared_ptr<Resource> loaded(new Resource(7));
            raw = loaded.get();
            packet.push_back(loaded);
        }
        assert(packet.size() == 1 && packet[0].get() == raw && packet[0]->id == 7);
        packet.clear();
        assert(packet.empty());
    }

    copy.clear();

    // The render scope releases reused storage after replay; repeats count again afterwards.
    {
        Shipyard::Overlap::ResourceReaderScope<Owners> scope(owners);
        assert(!owners.empty());
    }
    assert(owners.empty() && first.use_count() == 1);
    owners.push_back(first);
    owners.push_back(first);
    assert(owners.size() == 1);
    std::printf("resource owners: repeat suppression passed\n");
    return 0;
}
