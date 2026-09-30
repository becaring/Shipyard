#include "storage/device.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <deque>
#include <utility>

namespace {
bool displayReady, cancelSelection, immediateCompletion, failPresent;
bool signinCancel, repeatSigninCancel, changedProfile, temporaryProfileChange;
bool closingProfileChange, signinAtCompletion, stuckUi, observedCancel, profileUnavailable;
bool uiVisible;
unsigned devices, presents, requests, requestFrames, closeFrames, mounts, eventCloses;
unsigned flushes, containerCloses, cancelledOps;
XUID profile = 1;
XOVERLAPPED* pending;
DWORD* selectedDevice;
std::deque<std::pair<DWORD, ULONG_PTR> > notifications;
const char* expectedMount;
const char* expectedFile;
const wchar_t* expectedName;
unsigned long long expectedSize;

void Reset() {
    assert(!pending);
    displayReady = cancelSelection = immediateCompletion = failPresent = false;
    signinCancel = repeatSigninCancel = changedProfile = temporaryProfileChange = false;
    closingProfileChange = signinAtCompletion = stuckUi = observedCancel = profileUnavailable = false;
    uiVisible = false;
    devices = presents = requests = requestFrames = closeFrames = mounts = eventCloses = 0;
    flushes = containerCloses = cancelledOps = 0;
    profile = 1;
    notifications.clear();
}

void Initialize() { if (!displayReady) { displayReady = true; ++devices; } }

HRESULT Present() {
    assert(displayReady);
    ++presents;
    if (failPresent) return -1;
    if (!pending && closeFrames && !stuckUi && !--closeFrames) {
        uiVisible = false;
        notifications.push_back(std::make_pair(DWORD(XN_SYS_UI), ULONG_PTR(0)));
        if (closingProfileChange) profile = 2;
    }
    if (pending) {
        ++requestFrames;
        if (temporaryProfileChange && requestFrames == 1) profile = 2;
        if (temporaryProfileChange && requestFrames == 2) profile = 1;
        if (signinCancel && (requests == 1 || repeatSigninCancel) &&
            requestFrames == (signinAtCompletion ? 3u : 1u))
            notifications.push_back(std::make_pair(DWORD(XN_SYS_SIGNINCHANGED), ULONG_PTR(0)));
        if (requestFrames == 3) {
            const bool interrupted = signinCancel && (requests == 1 || repeatSigninCancel);
            if (changedProfile) profile = 2;
            pending->status = (cancelSelection || interrupted) ? ERROR_CANCELLED : ERROR_SUCCESS;
            pending->dwExtendedError = pending->status;
            if (pending->status == ERROR_SUCCESS) *selectedDevice = 7;
            pending = NULL;
            closeFrames = 2;
        }
    }
    return S_OK;
}

Shipyard::Storage::MountResult Run(unsigned long long bytes, const char* mount,
                                    const char* file, const wchar_t* displayName) {
    expectedMount = mount;
    expectedFile = file;
    expectedName = displayName;
    expectedSize = bytes;
    const Shipyard::Storage::MountPolicy policy = { 0, bytes, mount, file, displayName };
    const Shipyard::Storage::StartupDisplay display = { Initialize, Present };
    return Shipyard::Storage::SelectAndMount(policy, display);
}

void Succeeded(const Shipyard::Storage::MountResult& result) {
    assert(result.status == ERROR_SUCCESS && result.stage == Shipyard::Storage::MountedStage);
    assert(result.deviceId == 7 && mounts == 1 && eventCloses == requests);
}

void Failed(const Shipyard::Storage::MountResult& result) {
    assert(result.status == ERROR_CANCELLED && result.stage == Shipyard::Storage::SelectionStage);
    assert(mounts == 0 && eventCloses == requests && !pending);
}
}

HANDLE CreateEvent(void*, int manual, int, void*) { assert(manual == TRUE); return 2; }
int CloseHandle(HANDLE handle) { if (handle == 2) { assert(!pending); ++eventCloses; } return TRUE; }
DWORD GetLastError() { return 5; }
DWORD GetTickCount() { return presents * 16; }
void Sleep(DWORD) {}
DWORD XInputGetState(DWORD user, XINPUT_STATE* input) {
    assert(user == 0);
    input->Gamepad.wButtons = observedCancel ? XINPUT_GAMEPAD_B : 0;
    return ERROR_SUCCESS;
}
HANDLE XNotifyCreateListener(DWORD mask) { assert(mask == XNOTIFY_SYSTEM); return 1; }
int XNotifyGetNext(HANDLE listener, DWORD, DWORD* id, ULONG_PTR* value) {
    assert(listener == 1);
    if (notifications.empty()) return FALSE;
    *id = notifications.front().first;
    *value = notifications.front().second;
    notifications.pop_front();
    return TRUE;
}
DWORD XUserGetSigninInfo(DWORD user, DWORD flags, XUSER_SIGNIN_INFO* info) {
    assert(user == 0 && flags == XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY);
    info->UserSigninState = profileUnavailable ? eXUserSigninState_NotSignedIn : eXUserSigninState_SignedInLocally;
    info->xuid = profile;
    return ERROR_SUCCESS;
}
DWORD XShowDeviceSelectorUI(DWORD user, DWORD type, DWORD, ULARGE_INTEGER requested,
                           DWORD* device, XOVERLAPPED* operation) {
    assert(user == 0 && type == XCONTENTTYPE_SAVEDGAME && requested.QuadPart == expectedSize);
    assert(displayReady && !uiVisible);
    ++requests;
    requestFrames = 0;
    selectedDevice = device;
    operation->status = ERROR_IO_PENDING;
    if (immediateCompletion) { operation->status = ERROR_SUCCESS; *device = 7; }
    else {
        pending = operation;
        uiVisible = true;
        notifications.push_back(std::make_pair(DWORD(XN_SYS_UI), ULONG_PTR(1)));
    }
    return ERROR_IO_PENDING;
}
int XHasOverlappedIoCompleted(XOVERLAPPED* operation) { return operation->status != ERROR_IO_PENDING; }
DWORD XGetOverlappedResult(XOVERLAPPED* operation, void*, int wait) {
    assert(wait == FALSE && XHasOverlappedIoCompleted(operation));
    return operation->status;
}
DWORD XCancelOverlapped(XOVERLAPPED* operation) {
    assert(pending == operation);
    operation->status = ERROR_CANCELLED;
    pending = NULL;
    ++cancelledOps;
    return ERROR_SUCCESS;
}
DWORD XContentCreate(DWORD user, const char* mount, XCONTENT_DATA* data, DWORD flags,
                     void*, void*, void*) {
    assert(user == 0 && flags == XCONTENTFLAG_OPENALWAYS && data->DeviceID == 7);
    assert(data->dwContentType == XCONTENTTYPE_SAVEDGAME);
    assert(std::strcmp(mount, expectedMount) == 0);
    assert(std::strcmp(data->szFileName, expectedFile) == 0);
    assert(std::wcscmp(data->szDisplayName, expectedName) == 0);
    ++mounts;
    return ERROR_SUCCESS;
}
DWORD XContentFlush(const char* mount, void*) { assert(std::strcmp(mount, expectedMount) == 0); ++flushes; return ERROR_SUCCESS; }
DWORD XContentClose(const char* mount, void*) { assert(std::strcmp(mount, expectedMount) == 0); ++containerCloses; return ERROR_SUCCESS; }

int main() {
    Reset();
    Succeeded(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(devices == 1 && presents == 3 && requests == 1);
    assert(Shipyard::Storage::Flush(expectedMount) == ERROR_SUCCESS && flushes == 1);
    assert(Shipyard::Storage::Close(expectedMount) == ERROR_SUCCESS && containerCloses == 1);
    Reset();
    Succeeded(Run(16 * 1024 * 1024, "shipyard", "3Shipsty", L"Ship of Harkinian"));
    Reset(); cancelSelection = true;
    Failed(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(requests == 1);
    Reset(); immediateCompletion = true;
    Succeeded(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(presents == 0);
    Reset(); failPresent = true;
    Failed(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(cancelledOps == 1 && requests == 1);
    Reset(); signinCancel = true;
    Succeeded(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(requests == 2 && presents == 8);
    Reset(); signinCancel = repeatSigninCancel = true;
    Failed(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(requests == 2);
    Reset(); signinCancel = changedProfile = true;
    Failed(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(requests == 1);
    Reset(); signinCancel = temporaryProfileChange = true;
    Failed(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(requests == 1);
    Reset(); signinCancel = closingProfileChange = true;
    Failed(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(requests == 1);
    Reset(); signinCancel = stuckUi = true;
    Failed(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(requests == 1 && presents < 320);
    Reset(); signinCancel = observedCancel = true;
    Failed(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(requests == 1);
    Reset(); signinCancel = signinAtCompletion = true;
    Failed(Run(1024 * 1024, "smashsave", "Smash360", L"BattleShip"));
    assert(requests == 1);
    std::puts("PASS canonical XAM selector and XContent mount: both policies, sticky profile, bounded retry, UI lifetime, cancellation, flush/close");
}
