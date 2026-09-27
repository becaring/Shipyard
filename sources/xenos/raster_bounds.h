#pragma once

namespace Shipyard {
namespace Xenos {
// Target binding and SDK tiling boundaries may replace viewport/scissor state.
// Fast3D can resume the same batch without issuing either setter again.
class ScopedRasterBounds {
    IDirect3DDevice9* device;
    D3DVIEWPORT9 viewport;
    RECT scissor;
    DWORD scissorEnabled;
    ScopedRasterBounds(const ScopedRasterBounds&);
    ScopedRasterBounds& operator=(const ScopedRasterBounds&);
public:
    explicit ScopedRasterBounds(IDirect3DDevice9* value) : device(value) {
        Shipyard_Check(device->GetViewport(&viewport), "capture framebuffer viewport");
        Shipyard_Check(device->GetScissorRect(&scissor), "capture framebuffer scissor");
        Shipyard_Check(device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissorEnabled),
                       "capture framebuffer scissor enable");
    }
    ~ScopedRasterBounds() {
        Shipyard_Check(device->SetViewport(&viewport), "restore framebuffer viewport");
        Shipyard_Check(device->SetScissorRect(&scissor), "restore framebuffer scissor");
        Shipyard_Check(device->SetRenderState(D3DRS_SCISSORTESTENABLE, scissorEnabled),
                       "restore framebuffer scissor enable");
    }
};
}
}
