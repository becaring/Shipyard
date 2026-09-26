#pragma once
#include <math.h>

namespace Shipyard {
namespace Fast3D {
// Round halfway cases away from zero without adding 0.5 to a large value.
inline float Round(float value) {
    const float magnitude = fabsf(value);
    const float integral = floorf(magnitude);
    const float result = magnitude - integral < 0.5f ? integral : integral + 1.0f;
    return value < 0.0f ? -result : (value == 0.0f ? value : result);
}
} // namespace Fast3D
} // namespace Shipyard
