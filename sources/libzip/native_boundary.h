#ifndef SHIPYARD_LIBZIP_NATIVE_BOUNDARY_H
#define SHIPYARD_LIBZIP_NATIVE_BOUNDARY_H
#include <zip.h>
#ifdef __cplusplus
extern "C" {
#endif
char *shipyard_zip_resolve_path(const char *path, zip_error_t *error);
int shipyard_zip_secure_random(unsigned char *buffer, unsigned short length);
#ifdef __cplusplus
}
#endif
#endif
