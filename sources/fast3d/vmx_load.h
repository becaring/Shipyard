#pragma once

#include <vectorintrinsics.h>

namespace Shipyard { namespace Fast3D { namespace Detail {

static __forceinline __vector4 LoadUnaligned(const void* pointer) {
    return __vor(__lvlx(pointer,0),__lvrx(pointer,16));
}

} } }
