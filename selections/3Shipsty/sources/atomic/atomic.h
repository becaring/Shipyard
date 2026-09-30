#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif
#include <stdint.h>

#ifdef _XBOX
#include <ppcintrinsics.h>
extern "C" void _ReadWriteBarrier();
#pragma intrinsic(_ReadWriteBarrier)
#endif

namespace Shipyard {
namespace Atomic {

enum memory_order {
    memory_order_relaxed,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
};

enum ordering_policy { conservative_ordering, native_relaxed_counter };

inline void FullFence() {
#ifdef _XBOX
    __sync();
#else
    MemoryBarrier();
#endif
}

template <class T> struct Operations;

#ifdef _XBOX
template <class T> inline T NativeRead(const volatile T* value) {
    _ReadWriteBarrier();
    T result = *value;
    _ReadWriteBarrier();
    return result;
}
template <class T> inline T AcquireRead(const volatile T* value) {
    T result = NativeRead(value);
    __lwsync();
    _ReadWriteBarrier();
    return result;
}
#endif

template <> struct Operations<uint64_t> {
    typedef LONG64 storage;
    static storage cas(volatile storage* value, storage desired, storage expected) {
        return InterlockedCompareExchange64(value, desired, expected);
    }
    static storage exchange(volatile storage* value, storage desired) {
        return InterlockedExchange64(value, desired);
    }
    static storage add(volatile storage* value, storage amount) {
        return InterlockedExchangeAdd64(value, amount);
    }
};
template <> struct Operations<int> {
    typedef LONG storage;
    static storage cas(volatile storage* value, storage desired, storage expected) {
        return InterlockedCompareExchange(value, desired, expected);
    }
    static storage exchange(volatile storage* value, storage desired) {
        return InterlockedExchange(value, desired);
    }
    static storage add(volatile storage* value, storage amount) {
        return InterlockedExchangeAdd(value, amount);
    }
};
template <> struct Operations<unsigned> : Operations<int> {};
template <> struct Operations<bool> : Operations<int> {};

template <class T, ordering_policy Policy> struct Cell {
    __declspec(align(8)) volatile T value;
};
template <class T> struct Cell<T, native_relaxed_counter> {
    volatile T value;
};

// Conservative cells surround operations with full fences. Native relaxed
// counters omit those extra fences only for the explicitly selected operations.
template <class T, ordering_policy Policy = conservative_ordering> class atomic {
    typedef Operations<T> ops;
    typedef typename ops::storage storage;

public:
    explicit atomic(T initial = T()) {
        value_.value = static_cast<storage>(initial);
    }
    T load(memory_order order = memory_order_seq_cst) const {
#ifdef _XBOX
        if (order == memory_order_acquire)
            return static_cast<T>(AcquireRead(&value_.value));
        if (Policy == native_relaxed_counter && order == memory_order_relaxed)
            return static_cast<T>(NativeRead(&value_.value));
#else
        if (order == memory_order_acquire ||
            (Policy == native_relaxed_counter && order == memory_order_relaxed))
            return static_cast<T>(ops::cas(&value_.value, 0, 0));
#endif
        FullFence();
        storage result = ops::cas(&value_.value, 0, 0);
        FullFence();
        return static_cast<T>(result);
    }
    void store(T desired, memory_order = memory_order_seq_cst) {
        if (Policy != native_relaxed_counter)
            FullFence();
        ops::exchange(&value_.value, static_cast<storage>(desired));
        if (Policy != native_relaxed_counter)
            FullFence();
    }
    T exchange(T desired, memory_order = memory_order_seq_cst) {
        FullFence();
        storage previous = ops::exchange(&value_.value, static_cast<storage>(desired));
        FullFence();
        return static_cast<T>(previous);
    }
    T fetch_add(T amount, memory_order = memory_order_seq_cst) {
        FullFence();
        storage previous = ops::add(&value_.value, static_cast<storage>(amount));
        FullFence();
        return static_cast<T>(previous);
    }
    T fetch_sub(T amount, memory_order = memory_order_seq_cst) {
        storage delta = static_cast<storage>(uint64_t(0) - static_cast<uint64_t>(amount));
        FullFence();
        storage previous = ops::add(&value_.value, delta);
        FullFence();
        return static_cast<T>(previous);
    }
    bool compare_exchange_strong(T& expected, T desired) {
        FullFence();
        storage previous =
            ops::cas(&value_.value, static_cast<storage>(desired), static_cast<storage>(expected));
        FullFence();
        if (previous == static_cast<storage>(expected))
            return true;
        expected = static_cast<T>(previous);
        return false;
    }
    bool compare_exchange_weak(T& expected, T desired, memory_order order) {
        if (Policy != native_relaxed_counter || order != memory_order_relaxed)
            FullFence();
        storage previous =
            ops::cas(&value_.value, static_cast<storage>(desired), static_cast<storage>(expected));
        if (Policy != native_relaxed_counter || order != memory_order_relaxed)
            FullFence();
        if (previous == static_cast<storage>(expected))
            return true;
        expected = static_cast<T>(previous);
        return false;
    }
    atomic& operator=(T desired) {
        store(desired);
        return *this;
    }

private:
    atomic(const atomic&);
    atomic& operator=(const atomic&);
    mutable Cell<storage, Policy> value_;
};

} // namespace Atomic
} // namespace Shipyard
