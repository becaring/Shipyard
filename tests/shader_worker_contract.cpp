#include "io/shader_worker.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

std::atomic<int> fail_next(0);
std::atomic<int> writes(0);
std::atomic<int> completions(0);
std::atomic<int> failed_completions(0);
std::atomic<int> throw_write(0);

void* operator new(std::size_t size) {
    if (fail_next.exchange(0)) throw std::bad_alloc();
    void* value = std::malloc(size);
    if (!value) throw std::bad_alloc();
    return value;
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

bool write_record(const std::string& record, bool) {
    if (throw_write.exchange(0)) throw std::bad_alloc();
    if (record != "record") return false;
    ++writes;
    fail_next = 1;
    return true;
}
bool wrong_write(const std::string&, bool) { return false; }
void log_message(const char*, ...) {}
void fatal(const char*, HRESULT) { std::abort(); }
void complete(bool success) { if (success) ++completions; else ++failed_completions; }

int main() {
    using namespace Shipyard::IO;
    char name[] = "shader-cache";
    ShaderWorkerConfig config = {name, 0, write_record, log_message, fatal};
    if (!ConfigureShaderWorker(&config)) return 1;
    std::strcpy(name, "changed");
    config.write = wrong_write;
    const std::string record = "record";
    fail_next = 1;
    if (QueueShaderRecord(record, true, complete)) return 2;
    fail_next = 0;
    if (!QueueShaderRecord(record, true, complete)) return 3;
    WaitShaderWrites();
    fail_next = 0;
    PollShaderWrites();
    throw_write = 1;
    if (!QueueShaderRecord(record, true, complete)) return 4;
    WaitShaderWrites();
    PollShaderWrites();
    ShutdownShaderWrites();
    if (writes != 1 || completions != 1 || failed_completions != 1) return 5;
    std::puts("shader worker owns configuration and drains allocation failures");
}
