// In-process ZAPD extraction into a stored O2R archive. The adapter supplies
// the asset paths, its normalized big-endian ROM, and optional progress
// counters; extraction runs on the calling thread and any helper threads it
// asks for, and is not reentrant.
#ifndef SHIPYARD_ZAPD_EXTRACT_H
#define SHIPYARD_ZAPD_EXTRACT_H
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace Shipyard {
namespace Zapd {
typedef void (*LogSink)(const char* message);

struct Request {
    std::string xmlDirectory;      // assets/xml/<version>
    std::string fileListDirectory; // ROM file lists for every version
    std::string configPath;        // Config_<version>.xml
    std::string outputPath;        // archive to write, such as oot.o2r
    std::string portVersion;       // major.minor.patch
    std::vector<uint8_t>* rom;     // big-endian ROM; extraction takes its bytes
    volatile size_t* done;         // XML files finished, or null
    volatile size_t* total;        // XML files to process, or null
    LogSink log;                   // null discards exporter diagnostics
    std::string* stages;           // receives seconds per extraction stage, or null
    int helpers;                   // threads to add on hardware threads 4, 5 and 2, at most 3
    Request() : rom(NULL), done(NULL), total(NULL), log(NULL), stages(NULL), helpers(0) {}
};

// Returns ZAPD's exit code: zero once the archive is complete.
int Extract(const Request& request);
} // namespace Zapd
} // namespace Shipyard
#endif
