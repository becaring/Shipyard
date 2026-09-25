#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Convert only native-structure regions of writable little-endian engine data.
// name must be non-null; directory is required only for phondata.
int ShipyardSpeechFixupData(void* memory, size_t size, const char* name, const char* directory);
// Source samples are little-endian; target holds native signed 16-bit samples.
void ShipyardSpeechPcmToNative(short* target, const unsigned char* source, size_t samples);

#ifdef __cplusplus
}
#endif
