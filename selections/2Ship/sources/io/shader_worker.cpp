#include "shader_worker.h"
#include "async/native.h"
#include <memory>
#include <new>
#include <stdexcept>

namespace Shipyard {
namespace IO {
namespace {
struct Write {
    std::string bytes;
    void (*complete)(bool);
    bool truncate;
    bool success;
    Write* next;
};
class SectionLock {
public:
    explicit SectionLock(CRITICAL_SECTION& section) : section_(section) {
        EnterCriticalSection(&section_);
    }
    ~SectionLock() {
        LeaveCriticalSection(&section_);
    }

private:
    SectionLock(const SectionLock&);
    SectionLock& operator=(const SectionLock&);
    CRITICAL_SECTION& section_;
};
ShaderWorkerConfig config;
std::string configName;
volatile LONG configured;
Async::mutex lifecycleGate;
CRITICAL_SECTION mutex;
HANDLE worker;
HANDLE workEvent;
HANDLE idleEvent;
DWORD workerId;
bool stopping;
size_t queuedBytes;
size_t queuedCount;
Write* pendingHead;
Write* pendingTail;
Write* completedHead;
Write* completedTail;

DWORD WINAPI Run(void*) {
    for (;;) {
        WaitForSingleObject(workEvent, INFINITE);
        for (;;) {
            Write* write;
            {
                SectionLock lock(mutex);
                write = pendingHead;
                if (!write) {
                    if (stopping)
                        return 0;
                    break;
                }
                pendingHead = write->next;
                if (!pendingHead)
                    pendingTail = NULL;
            }
            bool ok = false;
            try {
                ok = config.write(write->bytes, write->truncate);
            } catch (...) {
                ok = false;
            }
            if (!ok) {
                try {
                    config.log("shader worker write failed name=%s bytes=%u", config.name,
                               (unsigned)write->bytes.size());
                } catch (...) {
                }
            }
            const size_t writtenBytes = write->bytes.size();
            std::string().swap(write->bytes);
            {
                SectionLock lock(mutex);
                queuedBytes -= writtenBytes;
                --queuedCount;
                write->success = ok;
                write->next = NULL;
                if (completedTail)
                    completedTail->next = write;
                else
                    completedHead = write;
                completedTail = write;
                if (!queuedCount)
                    SetEvent(idleEvent);
            }
        }
    }
}
void Start() {
    Async::lock_guard<Async::mutex> lock(lifecycleGate);
    if (worker)
        return;
    InitializeCriticalSection(&mutex);
    workEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    idleEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    if (!workEvent || !idleEvent)
        config.fatal("shader worker events", HRESULT_FROM_WIN32(GetLastError()));
    worker = CreateThread(NULL, 128 * 1024, Run, NULL, CREATE_SUSPENDED, &workerId);
    if (!worker)
        config.fatal("shader worker create", HRESULT_FROM_WIN32(GetLastError()));
    if (XSetThreadProcessor(worker, config.processor) == (DWORD)-1 ||
        !SetThreadPriority(worker, THREAD_PRIORITY_BELOW_NORMAL) ||
        ResumeThread(worker) == (DWORD)-1)
        config.fatal("shader worker start", HRESULT_FROM_WIN32(GetLastError()));
}
} // namespace

bool ConfigureShaderWorker(const ShaderWorkerConfig* options) {
    if (!options || !options->name || !options->write || !options->log || !options->fatal)
        return false;
    Async::lock_guard<Async::mutex> lock(lifecycleGate);
    if (InterlockedCompareExchange(&configured, 0, 0))
        return configName == options->name && config.processor == options->processor &&
               config.write == options->write && config.log == options->log &&
               config.fatal == options->fatal;
    try {
        configName = options->name;
    } catch (const std::bad_alloc&) {
        return false;
    }
    config = *options;
    config.name = configName.c_str();
    InterlockedExchange(&configured, 1);
    return true;
}
bool QueueShaderRecord(const std::string& record, bool truncate, void (*complete)(bool)) {
    if (!InterlockedCompareExchange(&configured, 0, 0) || record.size() > 8 * 1024 * 1024)
        return false;
    Start();
    SectionLock lock(mutex);
    if (!truncate && !stopping && pendingTail) {
        Write* last = pendingTail;
        if (last->complete == complete && record.size() <= 8 * 1024 * 1024 - queuedBytes) {
            try {
                last->bytes += record;
            } catch (const std::bad_alloc&) {
                return false;
            } catch (const std::length_error&) {
                return false;
            }
            queuedBytes += record.size();
            return true;
        }
    }
    const bool room =
        !stopping && queuedCount < 16 && record.size() <= 8 * 1024 * 1024 - queuedBytes;
    if (!room)
        return false;
    std::unique_ptr<Write> write;
    try {
        write.reset(new Write());
        write->bytes = record;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    write->complete = complete;
    write->truncate = truncate;
    write->success = false;
    write->next = NULL;
    if (pendingTail)
        pendingTail->next = write.get();
    else
        pendingHead = write.get();
    pendingTail = write.release();
    queuedBytes += record.size();
    ++queuedCount;
    ResetEvent(idleEvent);
    SetEvent(workEvent);
    return true;
}
void WaitShaderWrites() {
    if (worker && GetCurrentThreadId() != workerId &&
        WaitForSingleObject(idleEvent, INFINITE) != WAIT_OBJECT_0)
        config.fatal("shader write completion", E_FAIL);
}
void PollShaderWrites() {
    if (!worker)
        return;
    Write* results;
    {
        SectionLock lock(mutex);
        results = completedHead;
        completedHead = NULL;
        completedTail = NULL;
    }
    while (results) {
        Write* next = results->next;
        if (results->complete) {
            try {
                results->complete(results->success);
            } catch (...) {
                try {
                    config.log("shader worker completion failed name=%s", config.name);
                } catch (...) {
                }
            }
        }
        delete results;
        results = next;
    }
}
void ShutdownShaderWrites() {
    if (!worker)
        return;
    WaitShaderWrites();
    {
        SectionLock lock(mutex);
        stopping = true;
    }
    SetEvent(workEvent);
    WaitForSingleObject(worker, INFINITE);
    PollShaderWrites();
    CloseHandle(worker);
    CloseHandle(workEvent);
    CloseHandle(idleEvent);
    worker = NULL;
    workEvent = NULL;
    idleEvent = NULL;
    workerId = 0;
    stopping = false;
    DeleteCriticalSection(&mutex);
}
} // namespace IO
} // namespace Shipyard
