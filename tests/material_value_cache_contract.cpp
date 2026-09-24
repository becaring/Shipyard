#include "fast3d/material_value_cache.h"
#include <assert.h>
#include <stdio.h>

struct Key {
    unsigned mode;
    float extent;
    bool operator==(const Key& other) const {
        return mode == other.mode && extent == other.extent;
    }
};

struct Value { unsigned number; };

struct Builder {
    unsigned* calls;
    unsigned number;
    void Rebuild(Value& value) {
        ++*calls;
        value.number = number;
    }
};

int main() {
    Shipyard::Fast3D::MaterialValueCache<Key, Value> cache = {};
    unsigned calls = 0;
    Key a = { 1, 2.0f }, b = { 1, 3.0f }, c = { 2, 3.0f };
    Builder builder = { &calls, 17 };
    assert(cache.Load(a, builder).number == 17 && calls == 1);
    builder.number = 99;
    assert(cache.Load(a, builder).number == 17 && calls == 1);
    assert(cache.Load(b, builder).number == 99 && calls == 2);
    builder.number = 42;
    assert(cache.Load(c, builder).number == 42 && calls == 3);
    assert(cache.Load(a, builder).number == 42 && calls == 4);
    puts("PASS material value cache complete-key miss, warm hit, cold rebuild");
}
