#pragma once
#include <xtl.h>
#include <string>

namespace Shipyard {
namespace IO {
struct ShaderWorkerConfig {
    const char* name;
    DWORD processor;
    bool (*write)(const std::string&, bool);
    void (*log)(const char*, ...);
    // Start-up failures call fatal and require it not to return. The worker
    // cannot safely continue with missing events or an unstarted thread.
    void (*fatal)(const char*, HRESULT);
};
// The first successful call copies the configuration and name; later calls
// must provide equivalent values. Shutdown must not race with queue calls.
bool ConfigureShaderWorker(const ShaderWorkerConfig* config);
bool QueueShaderRecord(const std::string& record, bool truncate, void (*complete)(bool));
void WaitShaderWrites();
void PollShaderWrites();
void ShutdownShaderWrites();
} // namespace IO
} // namespace Shipyard
