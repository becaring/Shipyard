#pragma once
#include "native.h"

namespace Shipyard { namespace Entropy {
// Called once by the application before consumers request entropy, with a
// matching Shutdown after those consumers stop. No entropy fallback is used.
inline bool Initialize() {
#ifdef _XBOX
    return XNetStartup(0) == 0;
#else
    return true;
#endif
}
inline void Shutdown() {
#ifdef _XBOX
    XNetCleanup();
#endif
}
} }
