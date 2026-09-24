#pragma once
#include <xtl.h>
#include <stdint.h>
#include <cstddef>
#include <deque>
#include <string>

namespace Shipyard { namespace Diagnostics {

typedef void (*Emit)(const char*, ...);
typedef void (*Format)(const void*, Emit);

// All bytes are copied before publication. The callback and its context must
// remain alive until shutdown returns; the formatter itself belongs to the title.
struct Record {
    int level;
    std::string bytes;
    Format format;
    LARGE_INTEGER queued_at;
    uint32_t capture_ticks;
    DWORD producer;
    Record() : level(2), format(NULL), capture_ticks(0), producer(0) {
        queued_at.QuadPart = 0;
    }
};

struct Config {
    void* context;
    void (*consume)(const Record&, void*);
    void (*flush)(void*);
    void (*sink_error)(void*);
    void (*begin_record)(Record&, void*);
    void (*accepted)(Record&, uint32_t, void*);
    void (*worker_started)(void*);
    void (*worker_stopped)(void*);
    size_t byte_limit;
    unsigned line_limit;
    size_t wake_bytes;
    DWORD interval_ms;
    bool force_synchronous;
};

struct Stats {
    unsigned accepted;
    unsigned processed;
    unsigned backpressure;
    size_t peak_bytes;
};

class Queue {
public:
    explicit Queue(const Config& config);
    ~Queue();
    bool submit(const Record& record, uint32_t capture_start = 0);
    void flush();
    void shutdown();
    Stats stats();
private:
    Queue(const Queue&);
    Queue& operator=(const Queue&);
    static DWORD WINAPI run(void* context);
    void run_worker();
    void start_locked();
    void consume_record(const Record& record);
    void flush_sink();
    Config config_;
    CRITICAL_SECTION gate_;
    CRITICAL_SECTION sink_gate_;
    HANDLE wake_, drained_, space_, worker_;
    DWORD worker_id_;
    bool started_, stopping_, closed_;
    std::deque<Record> pending_;
    size_t queued_bytes_;
    Stats stats_;
};

} }
