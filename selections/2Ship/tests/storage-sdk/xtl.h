#pragma once
#include <stddef.h>
#include <stdint.h>

typedef unsigned long DWORD;
typedef unsigned long ULONG;
typedef uintptr_t ULONG_PTR;
typedef long HRESULT;
typedef int HANDLE;
typedef uint64_t XUID;

#define FALSE 0
#define TRUE 1
#define S_OK 0
#define FAILED(value) ((value) < 0)
#define INVALID_HANDLE_VALUE (-1)
#define ERROR_SUCCESS 0
#define ERROR_IO_PENDING 997
#define ERROR_CANCELLED 1223
#define ERROR_INVALID_PARAMETER 87
#define XINPUT_GAMEPAD_B 0x2000

struct XINPUT_GAMEPAD { unsigned short wButtons; };
struct XINPUT_STATE { XINPUT_GAMEPAD Gamepad; };
struct ULARGE_INTEGER { uint64_t QuadPart; };
struct XOVERLAPPED { DWORD status; HANDLE hEvent; DWORD dwExtendedError; };

HANDLE CreateEvent(void*, int, int, void*);
int CloseHandle(HANDLE);
DWORD GetLastError();
DWORD GetTickCount();
void Sleep(DWORD);
DWORD XInputGetState(DWORD, XINPUT_STATE*);
