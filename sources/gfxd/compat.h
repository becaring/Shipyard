#ifndef SHIPYARD_GFXD_COMPAT_H
#define SHIPYARD_GFXD_COMPAT_H
#include <stdint.h>
#include "crt/format.h"

/* libgfxd formats its explicitly 32-bit signed and unsigned words. */
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIX32 "X"
#endif
