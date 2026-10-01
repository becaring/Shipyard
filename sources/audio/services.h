#ifndef SHIPYARD_AUDIO_SERVICES_H
#define SHIPYARD_AUDIO_SERVICES_H

#include "async/native.h"

// Configure before audio initialization; uses Xbox processor-mask bits.
extern "C" void ShipyardAudioSetProcessor(unsigned processorMask);

void ShipyardAudioLog(const char* format, ...);
__declspec(noreturn) void ShipyardAudioFatal(const char* operation, HRESULT result);

#endif
