#pragma once

#include <xtl.h>
#ifdef _XBOX
#include <ppcintrinsics.h>
#endif

namespace Shipyard {
// Title-owned front buffers, following the XDK AsyncSwaps sample. Four
// buffers let rendering run two frames ahead of scanout, so one slow
// presentation between quick ones keeps refresh; the submitting CPU waits before
// reusing a busy buffer. Swaps retain the device's presentation interval and
// submission order. Buffering does not guarantee that a workload meets refresh.
class XenosSwapChain {
  public:
    enum { FrontBuffers = 4 };

    XenosSwapChain() : device(NULL), backBuffer(NULL), next(0), resolved(false) {
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

    // A frame tiled in EDRAM never sits whole in the back buffer: it resolves
    // each tile into this front buffer, then calls MarkResolved().
    IDirect3DTexture9* ResolveTarget() {
        if (!device) return NULL;
        WaitForFrontBuffer();
        return front[next];
    }
    void MarkResolved() { resolved = true; }

    // Call after the frame's commands were kicked (InsertFence), with the
    // finished image in the device back buffer or already resolved.
    HRESULT Present() {
        if (!device) return E_UNEXPECTED;
        if (!resolved) {
            WaitForFrontBuffer();
            IDirect3DSurface9* bound = NULL;
            if (SUCCEEDED(device->GetRenderTarget(0, &bound)) && bound != backBuffer)
                device->SetRenderTarget(0, backBuffer);
            if (bound) bound->Release();
            device->Resolve(D3DRESOLVE_RENDERTARGET0, NULL, front[next], NULL, 0, 0, NULL, 0.0f, 0, NULL);
        }
        resolved = false;
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
        resolved = false;
    }

  private:
    // EnqueuedCount excludes the buffer being scanned out. Wait on the CPU
    // until pending swaps plus scanout leave one of our front buffers free.
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
    bool resolved;
};

// The presentation device has one swap chain, shared by device setup, the
// startup UI and every port's window backend.
inline XenosSwapChain& XenosPresentationChain() {
    static XenosSwapChain chain;
    return chain;
}
}
