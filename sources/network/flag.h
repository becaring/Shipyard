#pragma once
#include "async/native.h"

namespace Shipyard {
namespace Network {
// Application and worker threads share these state flags.
class Flag {
public:
    Flag() : value_(0) {
    }
    Flag(bool value) : value_(value ? 1 : 0) {
    }
    Flag(const Flag& value) : value_(value ? 1 : 0) {
    }
    Flag& operator=(bool value) {
        InterlockedExchange(&value_, value ? 1 : 0);
        return *this;
    }
    Flag& operator=(const Flag& value) {
        return operator=(bool(value));
    }
    operator bool() const {
        return InterlockedCompareExchange(&value_, 0, 0) != 0;
    }

private:
    mutable volatile LONG value_;
};
} // namespace Network
} // namespace Shipyard
