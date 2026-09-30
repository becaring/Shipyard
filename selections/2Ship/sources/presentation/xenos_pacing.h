#pragma once
#include <xtl.h>
#include "pacing.h"

namespace Shipyard {
inline HRESULT ApplyXenosPresentationInterval(IDirect3DDevice9* device, uint32_t intervals,
                                               uint32_t refresh, double (*now)()) {
    if (!device || !intervals || !refresh || !now) return E_INVALIDARG;
    static const DWORD values[] = {
        D3DPRESENT_INTERVAL_ONE, D3DPRESENT_INTERVAL_TWO,
        D3DPRESENT_INTERVAL_THREE, D3DPRESENT_INTERVAL_FOUR
    };
    if (intervals > 4) {
        const double until = now() + double(intervals - 4) / refresh;
        while (now() < until) Sleep(1);
        intervals = 4;
    }
    return device->SetRenderState(D3DRS_PRESENTINTERVAL, values[intervals - 1]);
}
}
