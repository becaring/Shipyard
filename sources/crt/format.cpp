#include "format.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_XBOX)
typedef char ShipyardSizeTIsUint32[(sizeof(size_t) == 4 && sizeof(unsigned int) == 4) ? 1 : -1];
typedef char ShipyardPtrdiffIsInt32[(sizeof(ptrdiff_t) == 4 && sizeof(int) == 4) ? 1 : -1];
#endif
typedef char ShipyardVaListIsPointer[(sizeof(va_list) == sizeof(char*)) ? 1 : -1];

namespace {
// XDK's CRT predates the C99 z modifier. Its size_t/ptrdiff_t use the native
// unsigned-int/int variadic width, so only parsed integer directives lose z.
int RewriteFormat(const char* format, char* output, bool* changed) {
    size_t written = 0;
    *changed = false;
    while (*format) {
        if (*format != '%') {
            if (output) output[written] = *format;
            ++written;
            ++format;
            continue;
        }
        if (output) output[written] = *format;
        ++written;
        ++format;
        if (*format == '%') {
            if (output) output[written] = *format;
            ++written;
            ++format;
            continue;
        }
        while (*format && strchr("-+ #0", *format)) {
            if (output) output[written] = *format;
            ++written;
            ++format;
        }
        if (*format == '*') {
            if (output) output[written] = *format;
            ++written;
            ++format;
        } else {
            while (*format >= '0' && *format <= '9') {
                if (output) output[written] = *format;
                ++written;
                ++format;
            }
        }
        if (*format == '.') {
            if (output) output[written] = *format;
            ++written;
            ++format;
            if (*format == '*') {
                if (output) output[written] = *format;
                ++written;
                ++format;
            } else {
                while (*format >= '0' && *format <= '9') {
                    if (output) output[written] = *format;
                    ++written;
                    ++format;
                }
            }
        }
        if (*format == 'z') {
            ++format;
            if (!*format || !strchr("diouxXn", *format)) {
                errno = EINVAL;
                return -1;
            }
            *changed = true;
        }
        if (*format) {
            if (output) output[written] = *format;
            ++written;
            ++format;
        }
    }
    if (output) output[written] = '\0';
    return 0;
}
}

extern "C" int __cdecl SHIPYARD_CRT_VSNPRINTF(char* destination, size_t capacity,
                                                  const char* format, va_list arguments) {
    if (!format || (!destination && capacity)) {
        errno = EINVAL;
        return -1;
    }
    if (capacity) destination[0] = '\0';

    bool changed = false;
    if (strchr(format, 'z') && RewriteFormat(format, 0, &changed) != 0) return -1;
    char stackFormat[512];
    char* heapFormat = 0;
    const char* nativeFormat = format;
    if (changed) {
        const size_t length = strlen(format);
        char* rewritten = stackFormat;
        if (length >= sizeof(stackFormat)) {
            heapFormat = static_cast<char*>(malloc(length + 1));
            if (!heapFormat) {
                errno = ENOMEM;
                return -1;
            }
            rewritten = heapFormat;
        }
        bool ignored;
        RewriteFormat(format, rewritten, &ignored);
        nativeFormat = rewritten;
    }

    // XDK va_list is a pointer cursor. Each CRT pass receives an independent
    // copy; neither may consume the caller's cursor.
    va_list measured = arguments;
    const int required = _vscprintf(nativeFormat, measured);
    va_end(measured);
    int rendered = 0;
    if (required >= 0 && capacity) {
        va_list writing = arguments;
        rendered = _vsnprintf_s(destination, capacity, _TRUNCATE, nativeFormat, writing);
        va_end(writing);
    }
    if (heapFormat) free(heapFormat);
    if (required < 0) return required;
    if (rendered < 0 && static_cast<size_t>(required) < capacity) return rendered;
    return required;
}

extern "C" int __cdecl SHIPYARD_CRT_SNPRINTF(char* destination, size_t capacity,
                                                 const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = SHIPYARD_CRT_VSNPRINTF(destination, capacity, format, arguments);
    va_end(arguments);
    return result;
}
