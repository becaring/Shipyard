#include "presentation/xenos_swap_chain.h"
#include <assert.h>
#include <string>

namespace {
const unsigned Buffers = Shipyard::XenosSwapChain::FrontBuffers;
struct Device : IDirect3DDevice9 {
    IDirect3DSurface9 backBuffer, other;
    IDirect3DSurface9* bound;
    IDirect3DTexture9 textures[Buffers];
    UINT created, failCreateAt, width, height;
    D3DFORMAT format;
    DWORD enqueued;
    unsigned queries;
    bool async;
    IDirect3DBaseTexture9* resolved;
    DWORD resolveFlags;
    std::string calls;
    Device() : bound(&backBuffer), created(0), failCreateAt(99), width(0), height(0), format(0),
               enqueued(0), queries(0), async(false), resolved(NULL), resolveFlags(0) {}
    HRESULT GetRenderTarget(DWORD index, IDirect3DSurface9** surface) {
        assert(index == 0);
        *surface = bound;
        ++bound->references;
        return S_OK;
    }
    HRESULT SetRenderTarget(DWORD index, IDirect3DSurface9* surface) {
        assert(index == 0);
        bound = surface;
        calls += "target ";
        return S_OK;
    }
    HRESULT CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT f, D3DPOOL pool,
                          IDirect3DTexture9** texture, void* shared) {
        assert(levels == 1 && usage == 0 && pool == D3DPOOL_DEFAULT && !shared && !async);
        if (created == failCreateAt) return E_FAIL;
        width = w;
        height = h;
        format = f;
        textures[created].references = 1;
        *texture = &textures[created++];
        return S_OK;
    }
    HRESULT SetSwapMode(BOOL value) {
        async = value != FALSE;
        calls += "async ";
        return S_OK;
    }
    // Each query after the first observes one more vblank retiring a swap.
    HRESULT QuerySwapStatus(D3DSWAP_STATUS* status) {
        if (queries++ && enqueued) --enqueued;
        status->Flags = 0;
        status->Swap = 0;
        status->EnqueuedCount = enqueued;
        return S_OK;
    }
    HRESULT Resolve(DWORD flags, const D3DRECT* rect, IDirect3DBaseTexture9* texture, const D3DPOINT* point,
                    UINT level, UINT slice, const D3DVECTOR4* clear, float, DWORD, const D3DRESOLVE_PARAMETERS* parameters) {
        assert(!rect && !point && !level && !slice && !clear && !parameters);
        assert(bound == &backBuffer);
        // The resolve may only overwrite a buffer that is neither shown nor pending.
        assert(enqueued + 1 < Buffers);
        resolveFlags = flags;
        resolved = texture;
        calls += "resolve ";
        return S_OK;
    }
    DWORD InsertFence() {
        calls += "fence ";
        return 1;
    }
    HRESULT Swap(IDirect3DBaseTexture9* texture, const D3DVIDEO_SCALER_PARAMETERS* parameters) {
        assert(texture == resolved && !parameters);
        calls += "swap ";
        ++enqueued;
        return S_OK;
    }
};

D3DPRESENT_PARAMETERS Parameters() {
    D3DPRESENT_PARAMETERS parameters = {1280, 720, D3DFMT_LE_X8R8G8B8, TRUE};
    return parameters;
}
}

int main() {
    D3DPRESENT_PARAMETERS parameters = Parameters();
    {
        Shipyard::XenosSwapChain chain;
        Device device;
        assert(chain.Present() == E_UNEXPECTED);
        assert(chain.Create(NULL, parameters) == E_INVALIDARG);
        D3DPRESENT_PARAMETERS automatic = parameters;
        automatic.DisableAutoFrontBuffer = FALSE;
        assert(chain.Create(&device, automatic) == E_INVALIDARG && device.created == 0);

        device.failCreateAt = 2;
        assert(chain.Create(&device, parameters) == E_FAIL);
        assert(!device.async && device.textures[0].references == 0 && device.textures[1].references == 0);
        assert(device.backBuffer.references == 1 && chain.Present() == E_UNEXPECTED);
    }
    {
        Shipyard::XenosSwapChain chain;
        Device device;
        assert(chain.Create(&device, parameters) == S_OK);
        assert(Buffers == 4 && device.created == Buffers && device.async && device.calls == "async ");
        assert(device.width == 1280 && device.height == 720 && device.format == D3DFMT_LE_X8R8G8B8);
        assert(chain.Create(&device, parameters) == E_UNEXPECTED && device.created == Buffers);

        // Rotation and order: resolve into the free buffer, kick it, then swap.
        for (unsigned frame = 0; frame < Buffers + 1; ++frame) {
            device.calls.clear();
            device.enqueued = 0;
            device.queries = 0;
            assert(chain.Present() == S_OK);
            assert(device.calls == "resolve fence swap ");
            assert(device.resolveFlags == D3DRESOLVE_RENDERTARGET0);
            assert(device.resolved == &device.textures[frame % Buffers]);
            assert(device.queries == 1);
        }

        // Two finished frames may wait for their vblanks without stalling the CPU.
        device.enqueued = 2;
        device.queries = 0;
        assert(chain.Present() == S_OK && device.queries == 1);

        // Three waiting frames: this thread waits until one is shown, never the GPU.
        device.enqueued = 3;
        device.queries = 0;
        device.calls.clear();
        assert(chain.Present() == S_OK && device.queries == 2 && device.calls == "resolve fence swap ");

        // The back buffer is restored if another target is bound; the query
        // reference is always returned.
        device.bound = &device.other;
        device.enqueued = 0;
        device.queries = 0;
        device.calls.clear();
        assert(chain.Present() == S_OK && device.calls == "target resolve fence swap ");
        assert(device.other.references == 1 && device.backBuffer.references == 2);

        chain.Release();
        assert(device.backBuffer.references == 1);
        for (unsigned i = 0; i < Buffers; ++i) assert(device.textures[i].references == 0);
        assert(chain.Present() == E_UNEXPECTED);
    }
    {
        typedef Shipyard::XenosSwapChain Chain;
        Chain chain;
        Device device;
        assert(chain.Create(&device, parameters) == S_OK && chain.QueueLimit() == Chain::LongQueue);
        // Startup settles with the long queue, then frames start with at most two swaps pending.
        for (unsigned frame = 0; frame < Chain::SettleFrames; ++frame) {
            assert(chain.QueueLimit() == Chain::LongQueue);
            device.enqueued = 1;
            device.queries = 0;
            assert(chain.Present() == S_OK);
        }
        assert(chain.QueueLimit() == Chain::ShortQueue);
        device.enqueued = 2;
        device.queries = 0;
        assert(chain.Present() == S_OK && device.queries == 2);

        // A frame that finds nothing pending needed the third swap. Needed again right after
        // the settle, it holds twice as long.
        device.enqueued = 0;
        device.queries = 0;
        assert(chain.Present() == S_OK && chain.QueueLimit() == Chain::LongQueue);
        for (unsigned frame = 1; frame < 2 * Chain::SettleFrames; ++frame) {
            assert(chain.QueueLimit() == Chain::LongQueue);
            device.enqueued = 2;
            device.queries = 0;
            assert(chain.Present() == S_OK && device.queries == 1);
        }
        assert(chain.QueueLimit() == Chain::ShortQueue);

        // Needed again only long after, the hold returns to the settle length.
        for (unsigned frame = 0; frame < Chain::SettleFrames; ++frame) {
            device.enqueued = 1;
            device.queries = 0;
            assert(chain.Present() == S_OK && chain.QueueLimit() == Chain::ShortQueue);
        }
        device.enqueued = 0;
        device.queries = 0;
        assert(chain.Present() == S_OK && chain.QueueLimit() == Chain::LongQueue);
        for (unsigned frame = 1; frame + 1 < Chain::SettleFrames; ++frame) {
            device.enqueued = 1;
            device.queries = 0;
            assert(chain.Present() == S_OK && chain.QueueLimit() == Chain::LongQueue);
        }
        device.enqueued = 1;
        device.queries = 0;
        assert(chain.Present() == S_OK && chain.QueueLimit() == Chain::ShortQueue);
    }
    assert(&Shipyard::XenosPresentationChain() == &Shipyard::XenosPresentationChain());
    return 0;
}
