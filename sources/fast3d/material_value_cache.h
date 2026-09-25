#pragma once

namespace Shipyard {
namespace Fast3D {

// One value at a time. Consumers define the complete key, arithmetic and
// lifetime; the builder runs only on a cold or changed-key lookup. A failed
// builder leaves the old entry intact.
template <class Key, class Value> struct MaterialValueCache {
    bool valid;
    Key key;
    Value value;

    template <class Builder> const Value& Load(const Key& requested, Builder& builder) {
        if (!valid || !(key == requested)) {
            Value next = Value();
            builder.Rebuild(next);
            key = requested;
            value = next;
            valid = true;
        }
        return value;
    }

    void Invalidate() {
        valid = false;
    }
};

} // namespace Fast3D
} // namespace Shipyard
