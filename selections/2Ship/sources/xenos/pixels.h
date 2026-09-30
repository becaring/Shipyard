#pragma once
#include <stdint.h>
namespace Shipyard { namespace Xenos {
inline uint16_t PackReadbackPixel(uint32_t pixel) {
    const unsigned r=(( ((pixel>>16)&255)+4)*31)/255;
    const unsigned g=(( ((pixel>>8)&255)+4)*31)/255;
    const unsigned b=(( (pixel&255)+4)*31)/255;
    return static_cast<uint16_t>((r<<11)|(g<<6)|(b<<1)|((pixel>>24)?1:0));
}
} }
