#include "zapd/extract.h"
#include "zapd/compat.h"
#include <stdio.h>
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif

extern "C" int zapd_report(int argc, char* argv[], volatile size_t* extractCount, volatile size_t* totalExtract);
extern std::vector<uint8_t>* gShipyardZapdRom;
void ShipyardZapdArenaBegin();
void ShipyardZapdArenaEnd();

namespace {
Shipyard::Zapd::LogSink gLog = NULL;
}

double gShipyardZapdSeconds[ZapdStageCount];

double ShipyardZapdClock() {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (double)now.QuadPart / (double)frequency.QuadPart;
}

void ShipyardZapdLog(const std::string& message) {
    if (gLog != NULL)
        gLog(message.c_str());
}

namespace Shipyard {
namespace Zapd {
int Extract(const Request& request) {
    // The same argument list the desktop extractors pass; ZAPD writes nothing to the placeholders.
    const char* argv[] = {
        "ZAPD", "ed",
        "-i", request.xmlDirectory.c_str(),
        "-b", "rom",
        "-fl", request.fileListDirectory.c_str(),
        "-gsf", "0",
        "-rconf", request.configPath.c_str(),
        "-se", "OTR",
        "--otrfile", request.outputPath.c_str(),
        "--portVer", request.portVersion.c_str(),
        "-o", "placeholder",
        "-osf", "placeholder",
    };
    for (int stage = 0; stage < ZapdStageCount; ++stage)
        gShipyardZapdSeconds[stage] = 0;
    const double start = ShipyardZapdClock();
    ShipyardZapdArenaBegin();
    gLog = request.log;
    gShipyardZapdRom = request.rom;
    int result = zapd_report(sizeof(argv) / sizeof(argv[0]), const_cast<char**>(argv), request.done, request.total);
    gShipyardZapdRom = NULL;
    gLog = NULL;
    ShipyardZapdArenaEnd();
    if (request.stages != NULL) {
        const double* s = gShipyardZapdSeconds;
        // Export includes the archive writes it makes; other is everything untimed.
        double timed = 0;
        for (int stage = 0; stage < ZapdStageCount; ++stage)
            timed += stage == ZapdStageArchive ? 0 : s[stage];
        char text[256];
        _snprintf(text, sizeof(text),
                  "config=%.1f rom=%.1f xml=%.1f parse=%.1f export=%.1f archive=%.1f free=%.1f finish=%.1f other=%.1f",
                  s[ZapdStageConfig], s[ZapdStageRom], s[ZapdStageXml], s[ZapdStageParse], s[ZapdStageExport],
                  s[ZapdStageArchive], s[ZapdStageFree], s[ZapdStageFinish], ShipyardZapdClock() - start - timed);
        text[sizeof(text) - 1] = 0;
        *request.stages = text;
    }
    return result;
}
} // namespace Zapd
} // namespace Shipyard
