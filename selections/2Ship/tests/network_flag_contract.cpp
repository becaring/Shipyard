#include "network/flag.h"
#include <cassert>
#include <cstring>
#include <new>
#include <cstdio>

int main() {
    using Shipyard::Network::Flag;
    alignas(Flag) unsigned char storage[sizeof(Flag)];
    std::memset(storage, 0x7f, sizeof(storage));
    Flag* local = new (storage) Flag();
    assert(!*local);
    *local = true;
    assert(*local);
    Flag copy(*local);
    assert(copy);
    *local = false;
    assert(!*local && copy);
    std::puts("network flag default and assignment states are defined");
}
