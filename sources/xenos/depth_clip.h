#pragma once

namespace Shipyard {
namespace Xenos {
// Xenos has no independent near-plane disable. When depth is neither tested nor
// written, put Z on the near plane in the vertex shader; retain X/Y/W clipping
// and perspective interpolation. Depth-enabled geometry keeps its original Z.
class DepthClipState {
    bool useDepth, valid;
public:
    DepthClipState() : useDepth(false), valid(false) {}
    void SetDepth(bool test, bool write) {
        const bool next = test || write;
        if (useDepth != next) valid = false;
        useDepth = next;
    }
    void Invalidate() { valid = false; }
    void Apply(IDirect3DDevice9* device) {
        if (valid) return;
        const float value[4] = {useDepth ? 1.0f : 0.0f, 0, 0, 0};
        Shipyard_Check(device->SetVertexShaderConstantF(14, value, 1), "vertex depth clip mode");
        valid = true;
    }
};
}
}
