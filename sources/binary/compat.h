#pragma once

#include <cstddef>
#include <cmath>
#include <limits>
#include <memory>
#if defined(_XBOX) && defined(_MSC_VER)
#include <float.h>
#endif

// These scoped enum adapters must not acquire implicit integer conversion.
namespace Shipyard { namespace Binary {
template<class Derived> class ScopedEnum {
  public:
    bool operator==(const Derived& other) const { return mValue == other.GetValue(); }
    bool operator!=(const Derived& other) const { return mValue != other.GetValue(); }
    bool operator<(const Derived& other) const { return mValue < other.GetValue(); }
    bool operator<=(const Derived& other) const { return mValue <= other.GetValue(); }
    bool operator>(const Derived& other) const { return mValue > other.GetValue(); }
    bool operator>=(const Derived& other) const { return mValue >= other.GetValue(); }
    int GetValue() const { return mValue; }
  protected:
    ScopedEnum() : mValue(0) {}
    explicit ScopedEnum(int value) : mValue(value) {}
  private:
    int mValue;
};

inline std::unique_ptr<char[]> MakeZeroedChars(size_t length) {
    // make_unique<T[]> value-initializes every element, including unused bytes.
    return std::unique_ptr<char[]>(new char[length]());
}

inline float QuietNaN() { return std::numeric_limits<float>::quiet_NaN(); }

inline bool IsNaN(float value) {
#if defined(_XBOX) && defined(_MSC_VER)
    return _isnan(static_cast<double>(value)) != 0;
#else
    return std::isnan(value);
#endif
}

inline bool IsNaN(double value) {
#if defined(_XBOX) && defined(_MSC_VER)
    return _isnan(value) != 0;
#else
    return std::isnan(value);
#endif
}
} } // namespace Shipyard::Binary
