#pragma once

#include <stddef.h>

typedef long HRESULT;
typedef unsigned long DWORD;

#define FAILED(value) ((HRESULT)(value) < 0)

const HRESULT S_OK = 0;
const HRESULT E_FAIL = static_cast<HRESULT>(0x80004005UL);
const HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057UL);
const DWORD D3DRS_PRESENTINTERVAL = 1;
const DWORD D3DRS_BUFFER2FRAMES = 2;
const DWORD D3DPRESENT_INTERVAL_IMMEDIATE = 0;
const DWORD D3DPRESENT_INTERVAL_ONE = 11;
const DWORD D3DPRESENT_INTERVAL_TWO = 12;
const DWORD D3DPRESENT_INTERVAL_THREE = 13;
const DWORD D3DPRESENT_INTERVAL_FOUR = 14;

struct D3DDISPLAYMODE { DWORD RefreshRate; };

struct IDirect3DDevice9 {
    virtual HRESULT GetDisplayMode(unsigned, D3DDISPLAYMODE*) = 0;
    virtual HRESULT SetRenderState(DWORD, DWORD) = 0;
    virtual HRESULT Present(const void*, const void*, void*, const void*) = 0;
    virtual ~IDirect3DDevice9() {}
};

void Sleep(DWORD);
