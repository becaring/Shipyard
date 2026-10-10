#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Fast {
namespace ShipyardXenos {
// Framebuffer pixel coordinates and sample-zero depth, in caller order.
struct DepthSample { float x, y; uint16_t depth; };
void GetPixelDepthSamples(int framebuffer, DepthSample* samples, size_t count);
} // namespace ShipyardXenos
} // namespace Fast
