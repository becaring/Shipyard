#pragma once
#include <stdarg.h>

// XDK va_list is a cursor into the caller's argument storage. Copying the cursor
// preserves independent traversal; va_end resets only its own cursor.
#if defined(_XBOX) && !defined(va_copy)
#define va_copy(destination, source) ((destination) = (source))
#endif
