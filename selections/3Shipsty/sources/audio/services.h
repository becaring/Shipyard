#ifndef SHIPYARD_AUDIO_SERVICES_H
#define SHIPYARD_AUDIO_SERVICES_H

#include "async/native.h"

void ShipyardAudioLog(const char* format, ...);
__declspec(noreturn) void ShipyardAudioFatal(const char* operation, HRESULT result);

#endif
