#ifndef SHIPYARD_CRT_FORMAT_H
#define SHIPYARD_CRT_FORMAT_H

#include <stddef.h>
#include <stdarg.h>

#ifndef SHIPYARD_CRT_SNPRINTF
#define SHIPYARD_CRT_SNPRINTF snprintf
#endif
#ifndef SHIPYARD_CRT_VSNPRINTF
#define SHIPYARD_CRT_VSNPRINTF Shipyard_Vsnprintf
#endif

#ifdef __cplusplus
extern "C" {
#endif
int SHIPYARD_CRT_VSNPRINTF(char* destination, size_t capacity, const char* format,
                           va_list arguments);
int SHIPYARD_CRT_SNPRINTF(char* destination, size_t capacity, const char* format, ...);
#ifdef __cplusplus
}
#if defined(_XBOX) && !defined(SHIPYARD_CRT_HOST_TEST)
namespace std {
using ::snprintf;
}
#endif
#endif

#endif
