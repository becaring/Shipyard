#pragma once
#include <stdint.h>
namespace Shipyard { namespace Libultra {
typedef uint32_t (*TimerCallback)(uint32_t interval, void* context);
bool AddTimer(uint32_t interval, TimerCallback callback, void* context);
// Returns false on a timer callback or while another shutdown is in progress.
// A successful call joins workers before callback owners may be destroyed.
bool StopTimers();
} }
