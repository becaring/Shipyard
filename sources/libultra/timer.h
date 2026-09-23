#pragma once
#include <stdint.h>
namespace Shipyard { namespace Libultra {
typedef uint32_t (*TimerCallback)(uint32_t interval, void* context);
bool AddTimer(uint32_t interval, TimerCallback callback, void* context);
// Stop and join workers before destroying callback owners.
void StopTimers();
} }
