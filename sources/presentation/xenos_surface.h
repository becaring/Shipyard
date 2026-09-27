#pragma once

#include <xtl.h>
#include "pacing.h"

namespace Shipyard {
class XenosPresentationSurface {
  public:
    XenosPresentationSurface();
    HRESULT Attach(IDirect3DDevice9* device);
    uint32_t Refresh() const;
    void SetRate(uint32_t rate);
    void SetUncapped(bool enabled);
    HRESULT Prepare(double (*now)(), bool* ready);
    HRESULT Present();
    HRESULT SetMaxFrameLatency(int frames);

  private:
    IDirect3DDevice9* device;
    PresentationPacing pacing;
    bool uncapped;
};
}
