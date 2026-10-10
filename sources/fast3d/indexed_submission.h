#pragma once

#include <stddef.h>
#include <stdint.h>

// Lightweight indexed draw interface for interpreter adapters.
#if defined(_XBOX)
#define SHIPYARD_XENOS_INDEXED_TRIANGLES 1
namespace Fast {
namespace ShipyardXenos {
void DrawIndexedTriangles(const float* vertices, size_t floatCount,
                          size_t vertexCount, const uint16_t* indices,
                          size_t triangleCount);
}
}
#endif
