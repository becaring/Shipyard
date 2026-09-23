#pragma once

#include <memory>

namespace Shipyard { namespace Fast3D {

template<class T> class DispatchOwner {
public:
    DispatchOwner(DispatchOwner*& slot, const std::weak_ptr<T>& selected)
        : slot_(slot), previous_(slot), owner_(selected.lock()), valid_(true) {
        slot_ = this;
    }
    ~DispatchOwner() { slot_ = previous_; }

    // Keep XDK's weak-lock cleanup out of the valid borrow path.
    static __declspec(noinline) T* TemporaryBorrow(const std::weak_ptr<T>& selected) {
        return selected.lock().get();
    }
    __forceinline T* Get(const std::weak_ptr<T>& selected) const {
        if (valid_) return owner_.get();
        return TemporaryBorrow(selected);
    }
    static void Invalidate(DispatchOwner* scope) {
        // An outer frame must not regain an owner replaced in a nested call.
        while (scope) {
            scope->valid_ = false;
            scope->owner_.reset();
            scope = scope->previous_;
        }
    }
private:
    DispatchOwner(const DispatchOwner&);
    DispatchOwner& operator=(const DispatchOwner&);
    DispatchOwner*& slot_;
    DispatchOwner* previous_;
    std::shared_ptr<T> owner_;
    bool valid_;
};

} }
