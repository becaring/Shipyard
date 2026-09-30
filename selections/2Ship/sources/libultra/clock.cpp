#include "libultra/clock.h"
#include "clock/chrono.h"
namespace Shipyard { namespace Libultra {
uint64_t NowCycles() {
    return NanosecondsToCycles(Clock::steady_clock::now().time_since_epoch().count());
}
} }
