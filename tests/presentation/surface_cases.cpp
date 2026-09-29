#include "presentation/xenos_surface.h"
#include <assert.h>
#include <stdexcept>

namespace {
double clockValue;
unsigned sleeps;
double Now() { return clockValue; }

struct Device : IDirect3DDevice9 {
    DWORD refresh;
    DWORD state, value;
    unsigned modes;
    HRESULT modeResult, stateResult;
    Device() : refresh(60), state(0), value(0), modes(0),
               modeResult(S_OK), stateResult(S_OK) {}
    HRESULT GetDisplayMode(unsigned index, D3DDISPLAYMODE* mode) {
        assert(index == 0);
        ++modes;
        mode->RefreshRate = refresh;
        return modeResult;
    }
    HRESULT SetRenderState(DWORD kind, DWORD next) {
        state = kind;
        value = next;
        return stateResult;
    }
};
}

void Sleep(DWORD) { ++sleeps; clockValue += 0.005; }

int main() {
    Shipyard::XenosPresentationSurface surface;
    Device device;
    bool ready = false;
    assert(surface.Attach(NULL) == E_INVALIDARG);
    assert(surface.Prepare(Now, &ready) == E_INVALIDARG);
    assert(surface.Present() == E_INVALIDARG);
    assert(surface.SetMaxFrameLatency(2) == E_INVALIDARG);

    device.modeResult = E_FAIL;
    assert(surface.Attach(&device) == E_FAIL);
    device.modeResult = S_OK;
    device.refresh = 75;
    assert(surface.Attach(&device) == S_OK);
    assert(device.modes == 2 && surface.Refresh() == 75);
    assert(surface.Prepare(Now, NULL) == E_INVALIDARG);
    assert(surface.Prepare(Now, &ready) == S_OK && ready);
    assert(device.state == D3DRS_PRESENTINTERVAL && device.value == D3DPRESENT_INTERVAL_ONE);

    surface.SetRate(150);
    assert(surface.Prepare(Now, &ready) == S_OK && !ready);
    assert(surface.Prepare(Now, &ready) == S_OK && ready);
    assert(device.value == D3DPRESENT_INTERVAL_ONE);
    surface.SetRate(10);
    clockValue = 0;
    sleeps = 0;
    assert(surface.Prepare(Now, &ready) == S_OK && ready);
    assert(sleeps > 0 && device.value == D3DPRESENT_INTERVAL_FOUR);

    surface.SetUncapped(true);
    sleeps = 0;
    for (unsigned i=0; i<20; ++i) {
        assert(surface.Prepare(Now, &ready) == S_OK && ready);
        assert(device.value == D3DPRESENT_INTERVAL_IMMEDIATE && sleeps == 0);
    }
    surface.SetUncapped(false);
    surface.SetRate(75);
    assert(surface.Prepare(Now, &ready) == S_OK && ready);
    assert(device.value == D3DPRESENT_INTERVAL_ONE);
    device.stateResult = E_FAIL;
    assert(surface.Prepare(Now, &ready) == E_FAIL && ready);
    device.stateResult = S_OK;
    assert(surface.SetMaxFrameLatency(1) == S_OK);
    assert(device.state == D3DRS_BUFFER2FRAMES && device.value == 0);
    assert(surface.SetMaxFrameLatency(2) == S_OK && device.value == 1);
    device.stateResult = E_FAIL;
    assert(surface.SetMaxFrameLatency(2) == E_FAIL);
    // Frames go through the device's swap chain, which device setup creates;
    // swap_chain_cases.cpp covers it. The surface never calls device Present.
    assert(surface.Present() == E_UNEXPECTED);

    bool threw = false;
    try { surface.SetRate(0); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    return 0;
}
