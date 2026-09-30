#pragma once

#include <string>

// Lowers the pinned Fast3D HLSL shape to the Xenos shader ABI. On failure,
// error describes the rejected contract and lowered remains unchanged.
bool Shipyard_LowerShader(const std::string& hlsl, std::string& lowered, std::string& error, unsigned abi = 3);
