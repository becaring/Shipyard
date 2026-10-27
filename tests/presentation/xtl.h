#pragma once

#include <stddef.h>

typedef long HRESULT;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef int BOOL;
typedef DWORD D3DFORMAT;
typedef DWORD D3DPOOL;

#define FAILED(value) ((HRESULT)(value) < 0)
#define SUCCEEDED(value) ((HRESULT)(value) >= 0)
#define TRUE 1
#define FALSE 0

const HRESULT S_OK = 0;
const HRESULT E_FAIL = static_cast<HRESULT>(0x80004005UL);
const HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057UL);
const HRESULT E_UNEXPECTED = static_cast<HRESULT>(0x8000FFFFUL);
const DWORD D3DRS_PRESENTINTERVAL = 1;
const DWORD D3DRS_BUFFER2FRAMES = 2;
const DWORD D3DPRESENT_INTERVAL_IMMEDIATE = 0;
const DWORD D3DPRESENT_INTERVAL_ONE = 11;
const DWORD D3DPRESENT_INTERVAL_TWO = 12;
const DWORD D3DPRESENT_INTERVAL_THREE = 13;
const DWORD D3DPRESENT_INTERVAL_FOUR = 14;
const DWORD D3DRESOLVE_RENDERTARGET0 = 0x10;
const D3DFORMAT D3DFMT_LE_X8R8G8B8 = 0x42;
const D3DPOOL D3DPOOL_DEFAULT = 0;

struct D3DDISPLAYMODE { DWORD RefreshRate; };
struct D3DPRESENT_PARAMETERS {
    UINT BackBufferWidth, BackBufferHeight;
    D3DFORMAT FrontBufferFormat;
    BOOL DisableAutoFrontBuffer;
};
struct D3DSWAP_STATUS { DWORD Flags, Swap, EnqueuedCount; };
struct D3DRECT;
struct D3DPOINT;
struct D3DVECTOR4;
struct D3DRESOLVE_PARAMETERS;
struct D3DVIDEO_SCALER_PARAMETERS;

struct IDirect3DResource9 {
    unsigned long references;
    IDirect3DResource9() : references(1) {}
    unsigned long Release() { return --references; }
};
struct IDirect3DSurface9 : IDirect3DResource9 {};
struct IDirect3DBaseTexture9 : IDirect3DResource9 {};
struct IDirect3DTexture9 : IDirect3DBaseTexture9 {};

// Presentation contracts override only what they exercise.
struct IDirect3DDevice9 {
    virtual HRESULT GetDisplayMode(unsigned, D3DDISPLAYMODE*) { return E_FAIL; }
    virtual HRESULT SetRenderState(DWORD, DWORD) { return E_FAIL; }
    virtual HRESULT GetRenderTarget(DWORD, IDirect3DSurface9**) { return E_FAIL; }
    virtual HRESULT SetRenderTarget(DWORD, IDirect3DSurface9*) { return E_FAIL; }
    virtual HRESULT CreateTexture(UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, void*) {
        return E_FAIL;
    }
    virtual HRESULT SetSwapMode(BOOL) { return E_FAIL; }
    virtual HRESULT QuerySwapStatus(D3DSWAP_STATUS*) { return E_FAIL; }
    virtual HRESULT Resolve(DWORD, const D3DRECT*, IDirect3DBaseTexture9*, const D3DPOINT*, UINT, UINT,
                            const D3DVECTOR4*, float, DWORD, const D3DRESOLVE_PARAMETERS*) {
        return E_FAIL;
    }
    virtual DWORD InsertFence() { return 0; }
    virtual HRESULT Swap(IDirect3DBaseTexture9*, const D3DVIDEO_SCALER_PARAMETERS*) { return E_FAIL; }
    virtual ~IDirect3DDevice9() {}
};

void Sleep(DWORD);

union LARGE_INTEGER { long long QuadPart; };
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* value) { value->QuadPart = 0; return TRUE; }
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* value) { value->QuadPart = 1000; return TRUE; }
