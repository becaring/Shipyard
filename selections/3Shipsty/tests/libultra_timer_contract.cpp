#include "libultra/timer.h"
#include <windows.h>
#include <cstdio>

using Shipyard::Libultra::AddTimer;
using Shipyard::Libultra::StopTimers;

struct State {
    HANDLE entered;
    HANDLE release;
    HANDLE callback_done;
    volatile LONG add_result;
};
struct SelfStopState { HANDLE done; volatile LONG result; };

uint32_t once(uint32_t, void*) { return 0; }

uint32_t reenter(uint32_t, void* context) {
    State* state = static_cast<State*>(context);
    SetEvent(state->entered);
    if (WaitForSingleObject(state->release, 2000) != WAIT_OBJECT_0) return 0;
    InterlockedExchange(&state->add_result, AddTimer(1, once, NULL) ? 1 : 0);
    SetEvent(state->callback_done);
    return 0;
}

DWORD WINAPI shutdown(void*) { StopTimers(); return 0; }

uint32_t self_stop(uint32_t, void* context) {
    SelfStopState* state = static_cast<SelfStopState*>(context);
    InterlockedExchange(&state->result, StopTimers() ? 1 : 0);
    SetEvent(state->done);
    return 0;
}

int main() {
    State state = {CreateEvent(NULL, TRUE, FALSE, NULL),
                   CreateEvent(NULL, TRUE, FALSE, NULL),
                   CreateEvent(NULL, TRUE, FALSE, NULL), -1};
    if (!state.entered || !state.release || !state.callback_done) return 1;
    if (!AddTimer(1, reenter, &state) ||
        WaitForSingleObject(state.entered, 2000) != WAIT_OBJECT_0) return 2;
    HANDLE stopper = CreateThread(NULL, 0, shutdown, NULL, 0, NULL);
    if (!stopper) return 3;
    Sleep(20);
    SetEvent(state.release);
    if (WaitForSingleObject(stopper, 2000) != WAIT_OBJECT_0) ExitProcess(4);
    if (WaitForSingleObject(state.callback_done, 0) != WAIT_OBJECT_0 || state.add_result != 0) return 5;
    if (!AddTimer(1, once, NULL)) return 6;
    StopTimers();
    SelfStopState self = {CreateEvent(NULL, TRUE, FALSE, NULL), -1};
    if (!self.done || !AddTimer(1, self_stop, &self) ||
        WaitForSingleObject(self.done, 2000) != WAIT_OBJECT_0 || self.result != 0) return 7;
    if (!StopTimers()) return 8;
    CloseHandle(self.done);
    CloseHandle(stopper);
    CloseHandle(state.entered);
    CloseHandle(state.release);
    CloseHandle(state.callback_done);
    std::puts("timer shutdown rejects callback re-entry without holding the registry gate");
    return 0;
}
