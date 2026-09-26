#include "exception_filter.h"
#include <xtl.h>

namespace Shipyard {
namespace Diagnostics {
namespace {

UnhandledExceptionCallback gCallback = NULL;
void* gContext = NULL;

LONG WINAPI HandleUnhandledException(PEXCEPTION_POINTERS exception) {
    if (gCallback && exception && exception->ExceptionRecord) {
        gCallback(exception->ExceptionRecord->ExceptionCode,
                  reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress),
                  gContext);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void InstallUnhandledExceptionFilter(UnhandledExceptionCallback callback, void* context) {
    gContext = context;
    gCallback = callback;
    SetUnhandledExceptionFilter(HandleUnhandledException);
}

} // namespace Diagnostics
} // namespace Shipyard
