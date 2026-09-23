#include "async/detached.h"
#include "async/native.h"
#include <memory>
#include <exception>
#include <limits>
namespace Shipyard { namespace Async {
namespace {
// Initialized before application work starts, avoiding the XDK compiler's
// unsynchronized function-local-static initialization. Retain this one small
// tracker for process lifetime: detached workers may finish during CRT static
// teardown, so a tracker destructor must not invalidate their synchronization.
struct detached_state {
    mutex gate;
    condition_variable changed;
    size_t active;
    detached_state() : active(0) {}
};
detached_state& detached = *new detached_state();
void begin_detached() {
    lock_guard<mutex> lock(detached.gate);
    if (detached.active == (std::numeric_limits<size_t>::max)())
        throw std::overflow_error("Detached work count overflow");
    ++detached.active;
}
void finish_detached() {
    lock_guard<mutex> lock(detached.gate);
    --detached.active;
    if (!detached.active) detached.changed.notify_all();
}
}
static DWORD WINAPI run_detached(void* context){
    std::unique_ptr<std::function<void()> > callable(static_cast<std::function<void()>*>(context));
    try{
        (*callable)();
        // Resource/File captures can access Context while being destroyed.
        // Releasing them is part of completion, not an action after the wait.
        callable.reset();
        finish_detached();
    }catch(...){std::terminate();}
    return 0;
}
void launch_detached(const std::function<void()>& callable){
    std::unique_ptr<std::function<void()> > pending(new std::function<void()>(callable));
    begin_detached();
    // A worker can complete before CreateThread returns: transfer first.
    std::function<void()>* raw=pending.release();
    HANDLE thread=CreateThread(NULL,128*1024,run_detached,raw,0,NULL);
    if(!thread){DWORD error=GetLastError();delete raw;finish_detached();throw native_error("CreateThread",error);}
    // A valid, freshly created handle must close. As with an immediately
    // detached std::thread whose detach fails, do not lose a joinable owner.
    if(!CloseHandle(thread))std::terminate();
}
void wait_for_detached(){
    unique_lock<mutex> lock(detached.gate);
    while(detached.active)detached.changed.wait(lock);
}
} }
