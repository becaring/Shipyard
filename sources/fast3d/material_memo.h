#pragma once

namespace Shipyard {
namespace Fast3D {


// Retain material calculations across replays without enlarging Xenon's limited TLS section.
// Static TLS holds only the pointer. Storage is allocated once and released by the interpreter.
template<class Key, class Value, unsigned Capacity> struct MaterialMemo {
    struct Entry {
        Key key;
        Value value;
        bool valid;
    };
    Entry* entries;

    Entry& Slot(unsigned hash) {
        if (!entries) entries = new Entry[Capacity]();
        return entries[hash & (Capacity - 1)];
    }
    void Clear() {
        if (!entries) return;
        for (unsigned i = 0; i < Capacity; ++i) entries[i].valid = false;
    }
    void Release() {
        delete[] entries;
        entries = 0;
    }
    template<class Builder> const Value& Load(const Key& key, unsigned hash, Builder& builder) {
        Entry& entry = Slot(hash);
        if (!entry.valid || !(entry.key == key)) {
            Value next = Value();
            builder.Rebuild(next);
            entry.valid = false;
            entry.key = key;
            entry.value = next;
            entry.valid = true;
        }
        return entry.value;
    }
};

static inline unsigned HashMaterialKey(unsigned value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    return value;
}

} // namespace Fast3D
} // namespace Shipyard
