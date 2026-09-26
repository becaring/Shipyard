#ifndef SHIPYARD_DIAGNOSTICS_EXCEPTION_FILTER_H
#define SHIPYARD_DIAGNOSTICS_EXCEPTION_FILTER_H

#include <stdint.h>

namespace Shipyard {
namespace Diagnostics {

typedef void (*UnhandledExceptionCallback)(uint32_t code, uintptr_t address, void* context);

// Install once during application startup. The callback runs in exception context;
// it should only collect already available diagnostics and must not throw.
void InstallUnhandledExceptionFilter(UnhandledExceptionCallback callback, void* context);

} // namespace Diagnostics
} // namespace Shipyard

#endif
