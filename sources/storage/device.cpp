#include "storage/device.h"
#include <string.h>

namespace Shipyard {
namespace Storage {
namespace {

class SelectorEvents {
    HANDLE listener;
    unsigned user;
    XUID owner;

public:
    unsigned signins;
    bool uiVisible;
    bool cancelObserved;
    bool profileChanged;

    explicit SelectorEvents(unsigned userIndex)
        : listener(XNotifyCreateListener(XNOTIFY_SYSTEM)), user(userIndex), owner(INVALID_XUID),
          signins(0), uiVisible(false), cancelObserved(false), profileChanged(false) {
        if (listener == INVALID_HANDLE_VALUE)
            listener = NULL;
        Poll();
    }

    ~SelectorEvents() {
        if (listener)
            CloseHandle(listener);
    }

    XUID Profile() const {
        XUSER_SIGNIN_INFO info = {0};
        if (XUserGetSigninInfo(user, XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY, &info) !=
                ERROR_SUCCESS ||
            info.UserSigninState == eXUserSigninState_NotSignedIn)
            return INVALID_XUID;
        return info.xuid;
    }

    void BeginAttempt(XUID selectedOwner) {
        owner = selectedOwner;
    }

    void Poll() {
        DWORD id;
        ULONG_PTR value;
        while (listener && XNotifyGetNext(listener, 0, &id, &value)) {
            if (id == XN_SYS_UI)
                uiVisible = value != 0;
            else if (id == XN_SYS_SIGNINCHANGED)
                ++signins;
        }
        XINPUT_STATE input = {0};
        if (XInputGetState(user, &input) == ERROR_SUCCESS &&
            (input.Gamepad.wButtons & XINPUT_GAMEPAD_B))
            cancelObserved = true;
        if (owner != INVALID_XUID && Profile() != owner)
            profileChanged = true;
    }
};

} // namespace

MountResult SelectAndMount(const MountPolicy& policy, const StartupDisplay& display) {
    MountResult outcome = {ERROR_INVALID_PARAMETER, SelectionStage, 0, XCONTENTDEVICE_ANY};
    if (!policy.mountName || !policy.containerFileName || !policy.displayName ||
        !display.initialize || !display.present)
        return outcome;

    SelectorEvents events(policy.userIndex);
    display.initialize();
    events.Poll();
    const XUID owner = events.Profile();
    XCONTENT_DATA content = {0};
    ULARGE_INTEGER requested = {0};
    requested.QuadPart = policy.requestedBytes;
    DWORD result = ERROR_CANCELLED;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        events.Poll();
        if (attempt && (events.uiVisible || events.cancelObserved || events.profileChanged ||
                        events.Profile() != owner))
            break;
        events.BeginAttempt(owner);
        content.DeviceID = XCONTENTDEVICE_ANY;
        XOVERLAPPED selection = {0};
        selection.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!selection.hEvent) {
            outcome.status = GetLastError();
            return outcome;
        }
        const unsigned signinsBefore = events.signins;
        unsigned signinsPending = signinsBefore;
        result = XShowDeviceSelectorUI(policy.userIndex, XCONTENTTYPE_SAVEDGAME, 0, requested,
                                       &content.DeviceID, &selection);
        const DWORD initial = result;
        HRESULT presentation = S_OK;
        if (result == ERROR_IO_PENDING) {
            while (!XHasOverlappedIoCompleted(&selection)) {
                events.Poll();
                signinsPending = events.signins;
                presentation = display.present();
                if (FAILED(presentation)) {
                    XCancelOverlapped(&selection);
                    // XAM retains the device pointer and overlapped until cancellation finishes.
                    while (!XHasOverlappedIoCompleted(&selection))
                        Sleep(1);
                    break;
                }
            }
            result = XGetOverlappedResult(&selection, NULL, FALSE);
            if (FAILED(presentation) && result == ERROR_SUCCESS)
                result = ERROR_CANCELLED;
        }
        events.Poll();
        outcome.extendedStatus = selection.dwExtendedError;
        CloseHandle(selection.hEvent);
        // Retry a cancelled pending selector only after a sign-in interruption;
        // explicit cancel, profile changes, and presentation failure end selection.
        if (attempt || initial != ERROR_IO_PENDING || result != ERROR_CANCELLED ||
            FAILED(presentation) || signinsPending == signinsBefore || events.cancelObserved ||
            events.profileChanged || owner == INVALID_XUID)
            break;

        const DWORD hideStart = GetTickCount();
        while (events.uiVisible && DWORD(GetTickCount() - hideStart) < 5000) {
            presentation = display.present();
            if (FAILED(presentation))
                break;
            events.Poll();
        }
        if (events.uiVisible || FAILED(presentation) || events.cancelObserved ||
            events.profileChanged || events.Profile() != owner)
            break;
    }
    outcome.status = result;
    outcome.deviceId = content.DeviceID;
    if (result != ERROR_SUCCESS || content.DeviceID == XCONTENTDEVICE_ANY) {
        if (result == ERROR_SUCCESS)
            outcome.status = ERROR_CANCELLED;
        return outcome;
    }
    content.dwContentType = XCONTENTTYPE_SAVEDGAME;
    if (strcpy_s(content.szFileName, policy.containerFileName) ||
        wcscpy_s(content.szDisplayName, policy.displayName)) {
        outcome.status = ERROR_INVALID_PARAMETER;
        return outcome;
    }
    outcome.stage = ContainerStage;
    outcome.status = XContentCreate(policy.userIndex, policy.mountName, &content,
                                    XCONTENTFLAG_OPENALWAYS, NULL, NULL, NULL);
    if (outcome.status == ERROR_SUCCESS)
        outcome.stage = MountedStage;
    return outcome;
}

DWORD Flush(const char* mountName) {
    return XContentFlush(mountName, NULL);
}
DWORD Close(const char* mountName) {
    return XContentClose(mountName, NULL);
}

} // namespace Storage
} // namespace Shipyard
