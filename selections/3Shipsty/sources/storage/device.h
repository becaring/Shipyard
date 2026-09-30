#pragma once

#include <xtl.h>
#include <xam.h>

namespace Shipyard {
namespace Storage {

struct MountPolicy {
    unsigned userIndex;
    unsigned long long requestedBytes; // Minimum requested storage size in bytes.
    const char* mountName;
    const char* containerFileName;
    const wchar_t* displayName;
};

struct StartupDisplay {
    void (*initialize)();
    HRESULT (*present)();
};

enum MountStage { SelectionStage, ContainerStage, MountedStage };

struct MountResult {
    DWORD status;
    MountStage stage;
    DWORD extendedStatus;
    DWORD deviceId;
};

MountResult SelectAndMount(const MountPolicy& policy, const StartupDisplay& display);
DWORD Flush(const char* mountName);
DWORD Close(const char* mountName);

} // namespace Storage
} // namespace Shipyard
