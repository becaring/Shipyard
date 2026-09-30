#include "io/shader_worker.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>

std::atomic<int> fail_next(0);
std::atomic<int> writes(0);
std::atomic<int> completions(0);
std::atomic<int> failed_completions(0);
std::atomic<int> throw_write(0);
std::atomic<int> fail_after_write(1);
std::atomic<std::size_t> outstanding(0);

struct alignas(std::max_align_t) Allocation { std::size_t size; };

void* operator new(std::size_t size) {
    if (fail_next.exchange(0)) throw std::bad_alloc();
    Allocation* value = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (!value) throw std::bad_alloc();
    value->size = size;
    outstanding += size;
    return value + 1;
}
void operator delete(void* value) noexcept {
    if (!value) return;
    Allocation* block = static_cast<Allocation*>(value) - 1;
    outstanding -= block->size;
    std::free(block);
}
void operator delete(void* value, std::size_t) noexcept { operator delete(value); }

bool write_record(const std::string& record, bool) {
    if (throw_write.exchange(0)) throw std::bad_alloc();
    if (record != "record" && (record.size() != 1024*1024 || record[0] != 'x')) return false;
    ++writes;
    if (fail_after_write.exchange(0)) fail_next = 1;
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
    std::string large(1024*1024, 'x');
    const std::size_t before = outstanding;
    if (!QueueShaderRecord(large, true, complete)) return 5;
    WaitShaderWrites();
    if (outstanding - before >= 128*1024) return 6;
    PollShaderWrites();
    ShutdownShaderWrites();
    if (writes != 2 || completions != 2 || failed_completions != 1) return 7;
    std::puts("shader worker owns configuration and drains allocation failures");
}
