#pragma once

#include <xtl.h>
#ifdef _XBOX
#include <ppcintrinsics.h>
#endif

namespace Shipyard {
// Title-owned front buffers presented with asynchronous swaps, following the
// XDK AsyncSwaps sample (end-of-frame CPU throttle, no GPU vblank stall).
//
// Synchronous Present stalls the GPU at a vblank before it may resolve into
// the single automatic front buffer, so the GPU cannot start the next frame
// until that vblank, and it throttles the CPU so a frame's Present returns
// only after the previous frame is on screen. D3DRS_BUFFER2FRAMES only moves
// that CPU stall to the end of the frame; it never allows a third frame.
// Interpolated presentation puts the whole game update in front of lane 0,
// so lane 0 has to fit the update, its recording and its GPU work into two
// vblanks after lane 1 is shown. When it does not, it slips exactly one
// vblank and the three lanes take four vblanks: 3/4 of refresh, ~45 Hz.
//
// With three front buffers the GPU renders each frame as soon as it is
// kicked, and up to two finished frames may wait for their vblanks, so the
// short lanes bank the time the long lane needs. Every swap still honours
// D3DRS_PRESENTINTERVAL, so frames are shown in submission order, one
// presentation interval each; interpolation cadence is unchanged.
class XenosSwapChain {
  public:
    enum { FrontBuffers = 3 };

    XenosSwapChain() : device(NULL), backBuffer(NULL), next(0) {
        for (UINT i = 0; i < FrontBuffers; ++i) front[i] = NULL;
    }

    // parameters are the ones the device was created with. Async swaps need
    // DisableAutoFrontBuffer; the device's back buffer must still be bound.
    HRESULT Create(IDirect3DDevice9* value, const D3DPRESENT_PARAMETERS& parameters) {
        if (!value || !parameters.DisableAutoFrontBuffer) return E_INVALIDARG;
        if (device) return E_UNEXPECTED;
        HRESULT result = value->GetRenderTarget(0, &backBuffer);
        for (UINT i = 0; SUCCEEDED(result) && i < FrontBuffers; ++i)
            result = value->CreateTexture(parameters.BackBufferWidth, parameters.BackBufferHeight, 1, 0,
                                          parameters.FrontBufferFormat, D3DPOOL_DEFAULT, &front[i], NULL);
        if (FAILED(result)) {
            Release();
            return result;
        }
        // Do not enable D3DRS_BUFFER2FRAMES before this call: the XDK asserts
        // on that order, and the state has no effect under async swaps.
        value->SetSwapMode(TRUE);
        device = value;
        next = 0;
        return S_OK;
    }

    // Call after the frame's commands were kicked (InsertFence), with the
    // finished image in the device back buffer.
    HRESULT Present() {
        if (!device) return E_UNEXPECTED;
        WaitForFrontBuffer();
        IDirect3DSurface9* bound = NULL;
        if (SUCCEEDED(device->GetRenderTarget(0, &bound)) && bound != backBuffer)
            device->SetRenderTarget(0, backBuffer);
        if (bound) bound->Release();
        device->Resolve(D3DRESOLVE_RENDERTARGET0, NULL, front[next], NULL, 0, 0, NULL, 0.0f, 0, NULL);
        device->InsertFence();
        device->Swap(front[next], NULL);
        next = (next + 1) % FrontBuffers;
        return S_OK;
    }

    // The GPU must be idle; the device is released immediately afterwards.
    void Release() {
        for (UINT i = 0; i < FrontBuffers; ++i)
            if (front[i]) {
                front[i]->Release();
                front[i] = NULL;
            }
        if (backBuffer) {
            backBuffer->Release();
            backBuffer = NULL;
        }
        device = NULL;
        next = 0;
    }

  private:
    // EnqueuedCount swaps are pending and one more front buffer is being
    // scanned out. The next buffer is free once fewer than FrontBuffers are
    // busy; the GPU is never stalled, only this thread, and only when two
    // finished frames are already waiting for their vblanks.
    void WaitForFrontBuffer() {
        D3DSWAP_STATUS status;
        device->QuerySwapStatus(&status);
        if (status.EnqueuedCount + 1 < FrontBuffers) return;
        // SDK spin pattern: an interrupt resets the hardware priority, so it
        // is lowered on every pass and restored to the default afterwards.
        do {
#ifdef _XBOX
            __SetHWThreadPriorityLow();
            YieldProcessor();
            YieldProcessor();
            YieldProcessor();
#endif
            device->QuerySwapStatus(&status);
        } while (status.EnqueuedCount + 1 >= FrontBuffers);
#ifdef _XBOX
        __SetHWThreadPriorityMed();
#endif
    }

    IDirect3DDevice9* device;
    IDirect3DSurface9* backBuffer;
    IDirect3DTexture9* front[FrontBuffers];
    UINT next;
};

// The presentation device has one swap chain, shared by device setup, the
// startup UI and every port's window backend.
inline XenosSwapChain& XenosPresentationChain() {
    static XenosSwapChain chain;
    return chain;
}
}
