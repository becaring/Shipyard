#pragma once

namespace Shipyard {
namespace Xenos {
// Keep Fast3D requests independently of native readback: outside BeginTiling,
// XDK SetViewport clamps to the smaller physical tile surface. Internal copies,
// clears and presentation must not replace the logical application bounds.
class RasterBounds {
    bool viewportValid, scissorValid;
    int vx, vy, vw, vh, sx, sy, sw, sh;
public:
    RasterBounds() : viewportValid(false), scissorValid(false) {}
    void Reset() { viewportValid = scissorValid = false; }
    void SetViewport(int x, int y, int w, int h) {
        vx = x; vy = y; vw = w; vh = h; viewportValid = true;
    }
    void SetScissor(int x, int y, int w, int h) {
        sx = x; sy = y; sw = w; sh = h; scissorValid = true;
    }
    void ApplyViewport(IDirect3DDevice9* device, unsigned height) const {
        if (!viewportValid) return;
        D3DVIEWPORT9 viewport = {(DWORD)vx, height - vy - vh, (DWORD)vw, (DWORD)vh, 0, 1};
        Shipyard_Check(device->SetViewport(&viewport), "apply requested viewport");
    }
    void ApplyScissor(IDirect3DDevice9* device, unsigned height) const {
        if (!scissorValid) return;
        RECT scissor = {sx, (LONG)height - sy - sh, sx + sw, (LONG)height - sy};
        Shipyard_Check(device->SetScissorRect(&scissor), "apply requested scissor");
        Shipyard_Check(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE),
                       "enable requested scissor");
    }
    void Apply(IDirect3DDevice9* device, unsigned height) const {
        // SetViewport also changes native scissor state.
        ApplyViewport(device, height);
        ApplyScissor(device, height);
    }
};
}
}
