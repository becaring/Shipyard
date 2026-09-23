#pragma once

#include <string>

bool Shipyard_LowerShader(const std::string& hlsl, std::string& lowered,
                         std::string& error);
