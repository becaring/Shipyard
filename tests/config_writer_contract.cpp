// Host contract for lus_config.WRITER; the test generates config_writer.inc.
#include <nlohmann/json.hpp>
#include "async/native.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs {
struct path {
    std::string value;
    explicit path(const std::string& text) : value(text) {}
};
}

static Shipyard::Async::mutex recordGate;
static std::vector<std::pair<std::string, std::string> > writes;
static Shipyard::Async::manual_reset_event writing;
static Shipyard::Async::manual_reset_event release(true);
static int failures;

namespace Shipyard { namespace Storage {
void WriteAtomicFile(const fs::path& destination, const std::string& bytes) {
    writing.set();
    release.wait();
    if (destination.value == "fail") throw std::runtime_error("disk full");
    Shipyard::Async::lock_guard<Shipyard::Async::mutex> lock(recordGate);
    writes.push_back(std::make_pair(destination.value, bytes));
}
} }

#define SPDLOG_ERROR(...) (++failures)
#include "config_writer.inc"

static void expect(bool condition, const char* what) {
    if (condition) return;
    std::printf("config writer contract FAILED: %s\n", what);
    std::exit(1);
}

static nlohmann::json flat(int value) {
    nlohmann::json nested = nlohmann::json::object();
    nested["CVars"]["x"] = value;
    return nested.flatten();
}

static int savedX(size_t index) {
    return nlohmann::json::parse(writes[index].second)["CVars"]["x"].get<int>();
}

struct Other {
    ShipyardConfigWriter* writer;
    Shipyard::Async::manual_reset_event done;
    static void Run(void* context) {
        Other& other = *static_cast<Other*>(context);
        other.writer->Queue("b", flat(9), true);
        other.done.set();
    }
};

int main() {
    {
        ShipyardConfigWriter writer;
        release.reset();
        writer.Queue("a", flat(1), true);
        writing.wait();
        // The first save is being written; later ones replace each other.
        writer.Queue("a", flat(2), true);
        writer.Queue("a", flat(3), true);
        release.set();
        writer.Wait();
        expect(writes.size() == 2 && savedX(0) == 1 && savedX(1) == 3, "newer pending save replaces older");

        nlohmann::json nested = nlohmann::json::object();
        nested["CVars"]["x"] = 4;
        writer.Queue("a", nested, false);
        writer.Wait();
        expect(writes.size() == 3 && savedX(2) == 4, "nested document is written as-is");

        // A pending save of another file is written before the new one.
        writes.clear();
        writing.reset();
        release.reset();
        writer.Queue("a", flat(5), true);
        writing.wait();
        writer.Queue("a", flat(6), true);
        Other other;
        other.writer = &writer;
        Shipyard::Async::joining_thread queued(Other::Run, &other);
        expect(!other.done.wait_for_ms(50), "other file waits behind a pending save");
        release.set();
        queued.join();
        writer.Wait();
        expect(writes.size() == 3 && writes[1].first == "a" && savedX(1) == 6 && writes[2].first == "b",
               "pending save of another file survives");

        writer.Queue("fail", flat(7), true);
        writer.Wait();
        expect(failures == 1 && writes.size() == 3, "failed write is logged and skipped");
    }
    {
        writes.clear();
        ShipyardConfigWriter writer;
        writer.Queue("a", flat(8), true);
    }
    expect(writes.size() == 1 && savedX(0) == 8, "destruction publishes the last save");
    std::printf("Config writer contract passed\n");
    return 0;
}
