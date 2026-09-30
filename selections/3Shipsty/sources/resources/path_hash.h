#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace Shipyard { namespace Resources {
struct PathHash {
    size_t operator()(const std::string& path) const {
        return (*this)(path.data(), path.size());
    }
    size_t operator()(const char* path, size_t length) const {
        return Append(2166136261u, path, length);
    }
    static uint32_t Append(uint32_t value, const char* path, size_t length) {
        // XDK's sampled string hash skips distinguishing offsets in asset paths.
        for (size_t i=0;i<length;++i)
            value=(value*16777619u)^static_cast<unsigned char>(path[i]);
        return value;
    }
};
} }
