#include "shader_worker.h"
#include <deque>

namespace Shipyard { namespace IO {
namespace {
struct Write {
    std::string bytes;
    void (*complete)(bool);
    bool truncate;
};
struct Result { void (*complete)(bool); bool success; };
const ShaderWorkerConfig* config;
CRITICAL_SECTION mutex;
HANDLE worker,workEvent,idleEvent;
DWORD workerId;
bool stopping;
size_t queuedBytes,queuedCount;
std::deque<Write*> pending;
std::deque<Result> completed;

DWORD WINAPI Run(void*) {
    for(;;) {
        WaitForSingleObject(workEvent,INFINITE);
        for(;;) {
            EnterCriticalSection(&mutex);
            if(pending.empty()) {
                const bool finish=stopping;
                LeaveCriticalSection(&mutex);
                if(finish)return 0;
                break;
            }
            Write* write=pending.front();pending.pop_front();
            LeaveCriticalSection(&mutex);
            const bool ok=config->write(write->bytes,write->truncate);
            if(!ok)config->log("shader worker write failed name=%s bytes=%u",
                               config->name,(unsigned)write->bytes.size());
            const Result result={write->complete,ok};
            EnterCriticalSection(&mutex);
            queuedBytes-=write->bytes.size();--queuedCount;
            completed.push_back(result);
            if(!queuedCount)SetEvent(idleEvent);
            LeaveCriticalSection(&mutex);
            delete write;
        }
    }
}
void Start() {
    if(worker)return;
    InitializeCriticalSection(&mutex);
    workEvent=CreateEvent(NULL,FALSE,FALSE,NULL);
    idleEvent=CreateEvent(NULL,TRUE,TRUE,NULL);
    if(!workEvent || !idleEvent)config->fatal("shader worker events",HRESULT_FROM_WIN32(GetLastError()));
    worker=CreateThread(NULL,128*1024,Run,NULL,CREATE_SUSPENDED,&workerId);
    if(!worker)config->fatal("shader worker create",HRESULT_FROM_WIN32(GetLastError()));
    if(XSetThreadProcessor(worker,config->processor)==(DWORD)-1 ||
       !SetThreadPriority(worker,THREAD_PRIORITY_BELOW_NORMAL) ||
       ResumeThread(worker)==(DWORD)-1)
        config->fatal("shader worker start",HRESULT_FROM_WIN32(GetLastError()));
}
}

bool ConfigureShaderWorker(const ShaderWorkerConfig* options) {
    if(!options || !options->name || !options->write ||
       !options->log || !options->fatal)return false;
    if(config && config!=options)return false;
    config=options;
    return true;
}
bool QueueShaderRecord(const std::string& record,bool truncate,void (*complete)(bool)) {
    if(!config)return false;
    Start();
    EnterCriticalSection(&mutex);
    if(!truncate && !stopping && !pending.empty()) {
        Write* last=pending.back();
        if(last->complete==complete && record.size()<=8*1024*1024-queuedBytes) {
            last->bytes+=record;queuedBytes+=record.size();LeaveCriticalSection(&mutex);return true;
        }
    }
    const bool room=!stopping && queuedCount<16 && record.size()<=8*1024*1024-queuedBytes;
    if(room) {
        Write* write=new Write();write->bytes=record;write->complete=complete;write->truncate=truncate;
        pending.push_back(write);queuedBytes+=record.size();++queuedCount;
        ResetEvent(idleEvent);SetEvent(workEvent);
    }
    LeaveCriticalSection(&mutex);
    return room;
}
void WaitShaderWrites() {
    if(worker && GetCurrentThreadId()!=workerId && WaitForSingleObject(idleEvent,INFINITE)!=WAIT_OBJECT_0)
        config->fatal("shader write completion",E_FAIL);
}
void PollShaderWrites() {
    if(!worker)return;
    std::deque<Result> results;
    {EnterCriticalSection(&mutex);results.swap(completed);LeaveCriticalSection(&mutex);}
    for(size_t i=0;i<results.size();++i)if(results[i].complete)results[i].complete(results[i].success);
}
void ShutdownShaderWrites() {
    if(!worker)return;
    WaitShaderWrites();
    EnterCriticalSection(&mutex);stopping=true;LeaveCriticalSection(&mutex);
    SetEvent(workEvent);WaitForSingleObject(worker,INFINITE);
    PollShaderWrites();
    CloseHandle(worker);CloseHandle(workEvent);CloseHandle(idleEvent);
    worker=workEvent=idleEvent=NULL;workerId=0;stopping=false;
    DeleteCriticalSection(&mutex);
}
} }
