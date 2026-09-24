#ifndef SHIPYARD_ENTROPY_NATIVE_H
#define SHIPYARD_ENTROPY_NATIVE_H

#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#include <wincrypt.h>
#endif

namespace Shipyard { namespace Entropy {

// The caller owns XNet startup and cleanup. Failure is reported to the caller;
// no fallback algorithm, seed or process-lifetime state is installed here.
inline bool FillBytes(unsigned char* buffer, unsigned int length) {
#ifdef _XBOX
    return XNetRandom(buffer, length) == 0;
#else
    HCRYPTPROV provider;
    if (!CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_AES,
                             CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) return false;
    const BOOL filled = CryptGenRandom(provider, length, buffer);
    const BOOL released = CryptReleaseContext(provider, 0);
    return filled && released;
#endif
}

} }
#endif
