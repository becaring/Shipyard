#pragma once

#include <cstring>
#include <new>

namespace Shipyard {
namespace Fast3D {

// XDK can omit the zero-initialization preceding nested non-POD constructors.
// Clear raw storage before object lifetime begins, never after construction.
template <class T> __declspec(noinline) T* NewValueInitialized() {
    void* storage = ::operator new(sizeof(T));
    std::memset(storage, 0, sizeof(T));
    try {
        return ::new (storage) T();
    } catch (...) {
        ::operator delete(storage);
        throw;
    }
}

} // namespace Fast3D
} // namespace Shipyard
