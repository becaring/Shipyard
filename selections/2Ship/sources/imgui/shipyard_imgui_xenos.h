#pragma once

#include "imgui.h"
#include <xtl.h>
#include <d3d9.h>

IDirect3DDevice9* Shipyard_ImGuiDevice();
void Shipyard_ImGuiBeginNativeDraw();
void Shipyard_ImGuiInvalidateNativeState();
void Shipyard_ImGuiLogShader(const char*);
__declspec(noreturn) void Shipyard_ImGuiFatal(const char*, HRESULT);

inline void Shipyard_ImGuiCheck(HRESULT result, const char* operation) {
    if (FAILED(result)) Shipyard_ImGuiFatal(operation, result);
}

void Shipyard_ImGuiRendererInit();
void Shipyard_ImGuiRendererNewFrame();
void Shipyard_ImGuiRendererShutdown();
void Shipyard_ImGuiRender(ImDrawData*);
ImTextureID Shipyard_ImGuiUploadRGBA(const unsigned char*, int, int);
void Shipyard_ImGuiDestroyTexture(ImTextureID);
