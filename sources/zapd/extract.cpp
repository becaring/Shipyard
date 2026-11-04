#include "zapd/extract.h"

extern "C" int zapd_report(int argc, char* argv[], volatile size_t* extractCount, volatile size_t* totalExtract);
extern std::vector<uint8_t>* gShipyardZapdRom;

namespace {
Shipyard::Zapd::LogSink gLog = NULL;
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
    gLog = request.log;
    gShipyardZapdRom = request.rom;
    int result = zapd_report(sizeof(argv) / sizeof(argv[0]), const_cast<char**>(argv), request.done, request.total);
    gShipyardZapdRom = NULL;
    gLog = NULL;
    return result;
}
} // namespace Zapd
} // namespace Shipyard
