#include "entropy/native.h"
#include <errno.h>

// Application startup owns the entropy service lifetime.
extern "C" int __cdecl rand_s(unsigned int* value) {
    if (!value) {
        errno = EINVAL;
        return EINVAL;
    }
    *value = 0;
    unsigned int sample = 0;
    if (!Shipyard::Entropy::FillBytes(reinterpret_cast<unsigned char*>(&sample), sizeof(sample))) {
        errno = EIO;
        return EIO;
    }
    *value = sample;
    return 0;
}
