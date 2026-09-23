#include "libzip/native_boundary.h"
#include "filesystem/filesystem.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <algorithm>
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#include <wincrypt.h>
#endif
extern "C" int _zip_win32_error_to_errno(DWORD error);

extern "C" char *shipyard_zip_resolve_path(const char *name, zip_error_t *error) {
    // XDK file APIs take narrow device paths. Reject encoding we cannot promise
    // to round-trip; ZIP entry names retain libzip's full UTF-8 implementation.
    const unsigned char *scan = reinterpret_cast<const unsigned char *>(name);
    if (!name) { zip_error_set(error, ZIP_ER_INVAL, 0); return NULL; }
    for (; *scan; ++scan) {
        if (*scan >= 0x80) { zip_error_set(error, ZIP_ER_INVAL, 0); return NULL; }
    }
    try {
        std::error_code ec;
        std::string path = xbox_compat::filesystem::absolute(name, ec).lexically_normal().string();
        if (ec) { zip_error_set(error, ZIP_ER_OPEN, _zip_win32_error_to_errno(static_cast<DWORD>(ec.value()))); return NULL; }
        std::replace(path.begin(), path.end(), '/', '\\');
        char *copy = static_cast<char *>(malloc(path.size()+1));
        if (!copy) { zip_error_set(error, ZIP_ER_MEMORY, 0); return NULL; }
        memcpy(copy, path.c_str(), path.size()+1);
        return copy;
    } catch (...) { zip_error_set(error, ZIP_ER_MEMORY, 0); return NULL; }
}

extern "C" int shipyard_zip_secure_random(unsigned char *buffer, unsigned short length) {
    if (!buffer && length) return 0;
#ifdef _XBOX
    // XNet's caller owns network initialization. No hidden process-lifetime
    // network startup or cleanup here; failure propagates to libzip.
    return XNetRandom(buffer, length) == 0;
#else
    HCRYPTPROV provider;
    if (!CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) return 0;
    const BOOL success = CryptGenRandom(provider, length, buffer);
    const BOOL released = CryptReleaseContext(provider, 0);
    return success && released;
#endif
}
