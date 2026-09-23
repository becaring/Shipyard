#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Convert only native-structure regions of standard little-endian engine data.
int ShipyardSpeechFixupData(void* memory,size_t size,const char* name,const char* directory);
void ShipyardSpeechPcmToNative(short* target,const unsigned char* source,size_t samples);
#ifdef __cplusplus
}
#endif
