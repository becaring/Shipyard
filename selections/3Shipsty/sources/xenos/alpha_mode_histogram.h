#pragma once

namespace Fast {
namespace ShipyardXenos {

// Mirrors the native blend state: opaque, blended, and texture-edge cutout.
inline unsigned ClassifyNativeAlphaMode(bool enable, bool optAlpha, bool textureEdge,
                                        bool noise, bool invisible) {
    if (!enable)
        return 0;
    return optAlpha && textureEdge && !noise && !invisible ? 2 : 1;
}

struct NativeAlphaHistogram {
    enum { ModeCount = 3 };
    unsigned draws[ModeCount];
    unsigned triangles[ModeCount];
    unsigned vertices[ModeCount];

    void Record(unsigned mode, unsigned triangleCount) {
        if (mode >= ModeCount)
            return;
        ++draws[mode];
        triangles[mode] += triangleCount;
        vertices[mode] += triangleCount * 3;
    }

    void Reset() {
        for (unsigned mode = 0; mode < ModeCount; ++mode) {
            draws[mode] = 0;
            triangles[mode] = 0;
            vertices[mode] = 0;
        }
    }

    void CopyAndReset(unsigned* drawCounts, unsigned* triangleCounts,
                      unsigned* vertexCounts) {
        for (unsigned mode = 0; mode < ModeCount; ++mode) {
            drawCounts[mode] = draws[mode];
            triangleCounts[mode] = triangles[mode];
            vertexCounts[mode] = vertices[mode];
        }
        Reset();
    }
};

}
}
