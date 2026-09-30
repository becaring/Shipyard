#pragma once
#include <xtl.h>

#define INVALID_XUID 0
#define XNOTIFY_SYSTEM 1
#define XN_SYS_UI 2
#define XN_SYS_SIGNINCHANGED 3
#define XUSER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY 4
#define XCONTENTDEVICE_ANY 0
#define XCONTENTTYPE_SAVEDGAME 1
#define XCONTENTFLAG_OPENALWAYS 3

enum XUSER_SIGNIN_STATE { eXUserSigninState_NotSignedIn, eXUserSigninState_SignedInLocally };
struct XUSER_SIGNIN_INFO { XUSER_SIGNIN_STATE UserSigninState; XUID xuid; };
struct XCONTENT_DATA {
    DWORD DeviceID;
    DWORD dwContentType;
    char szFileName[42];
    wchar_t szDisplayName[128];
};

HANDLE XNotifyCreateListener(DWORD);
int XNotifyGetNext(HANDLE, DWORD, DWORD*, ULONG_PTR*);
DWORD XUserGetSigninInfo(DWORD, DWORD, XUSER_SIGNIN_INFO*);
DWORD XShowDeviceSelectorUI(DWORD, DWORD, DWORD, ULARGE_INTEGER, DWORD*, XOVERLAPPED*);
int XHasOverlappedIoCompleted(XOVERLAPPED*);
DWORD XGetOverlappedResult(XOVERLAPPED*, void*, int);
DWORD XCancelOverlapped(XOVERLAPPED*);
DWORD XContentCreate(DWORD, const char*, XCONTENT_DATA*, DWORD, void*, void*, void*);
DWORD XContentFlush(const char*, void*);
DWORD XContentClose(const char*, void*);
