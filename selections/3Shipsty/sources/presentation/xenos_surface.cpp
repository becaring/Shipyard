#include "xenos_surface.h"
#include "xenos_pacing.h"
#include "xenos_swap_chain.h"

namespace Shipyard {
XenosPresentationSurface::XenosPresentationSurface() : device(NULL) {}

HRESULT XenosPresentationSurface::Attach(IDirect3DDevice9* value) {
    if (!value) return E_INVALIDARG;
    D3DDISPLAYMODE mode;
    const HRESULT result = value->GetDisplayMode(0, &mode);
    if (FAILED(result)) return result;
    pacing.SetRefresh(mode.RefreshRate);
    device = value;
    return S_OK;
}

uint32_t XenosPresentationSurface::Refresh() const { return pacing.Refresh(); }
void XenosPresentationSurface::SetRate(uint32_t rate) { pacing.SetRate(rate); }

HRESULT XenosPresentationSurface::Prepare(double (*now)(), bool* ready) {
    if (!device || !ready) return E_INVALIDARG;
    const uint32_t intervals = pacing.Next();
    *ready = intervals != 0;
    if (!intervals) return S_OK;
    return ApplyXenosPresentationInterval(device, intervals, pacing.Refresh(), now);
}

HRESULT XenosPresentationSurface::Present() {
    return device ? XenosPresentationChain().Present() : E_INVALIDARG;
}

HRESULT XenosPresentationSurface::SetMaxFrameLatency(int frames) {
    return device ? device->SetRenderState(D3DRS_BUFFER2FRAMES, frames >= 2) : E_INVALIDARG;
}
}
