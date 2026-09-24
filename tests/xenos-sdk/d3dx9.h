#pragma once
#include <windows.h>
#define D3DX_SDK_VERSION 43
struct ID3DXBuffer {
    virtual void* GetBufferPointer() = 0;
    virtual DWORD GetBufferSize() = 0;
    virtual ULONG Release() = 0;
protected:
    virtual ~ID3DXBuffer() {}
};
HRESULT D3DXCreateBuffer(DWORD size, ID3DXBuffer** buffer);
