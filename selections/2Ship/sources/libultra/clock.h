#pragma once
#include <stdint.h>
namespace Shipyard { namespace Libultra {
// N64 cycles run at 46.875 MHz. Divide first to avoid overflowing ns * 3.
inline int64_t NanosecondsToCycles(int64_t ns) { return (ns / 64) * 3 + ((ns % 64) * 3) / 64; }
uint64_t NowCycles();
} }
