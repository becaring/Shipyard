#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace Shipyard { namespace Resources {
struct PathHash {
    size_t operator()(const std::string& path) const {
        uint32_t value=2166136261u;
        // XDK's sampled string hash skips distinguishing offsets in asset paths.
        for (size_t i=0;i<path.size();++i)
            value=(value*16777619u)^static_cast<unsigned char>(path[i]);
        return value;
    }
};
} }
