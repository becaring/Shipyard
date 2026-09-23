#pragma once
#include <xtl.h>
#include <string>

namespace Shipyard { namespace IO {
struct ShaderWorkerConfig {
    const char* name;
    DWORD processor;
    bool (*write)(const std::string&,bool);
    void (*log)(const char*,...);
    void (*fatal)(const char*,HRESULT);
};
bool ConfigureShaderWorker(const ShaderWorkerConfig* config);
bool QueueShaderRecord(const std::string& record,bool truncate,void (*complete)(bool));
void WaitShaderWrites();
void PollShaderWrites();
void ShutdownShaderWrites();
} }
