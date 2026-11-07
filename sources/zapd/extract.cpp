#include "zapd/extract.h"
#include "zapd/compat.h"
#include "archive/stored_zip_reader.h"
#include <stdio.h>
#include <algorithm>
#include <map>
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif

extern "C" int zapd_report(int argc, char* argv[], volatile size_t* extractCount, volatile size_t* totalExtract);
extern std::vector<uint8_t>* gShipyardZapdRom;
void ShipyardZapdArenaBegin();
void ShipyardZapdArenaEnter();
void ShipyardZapdArenaLeave();
void ShipyardZapdArenaEnd();

namespace {
Shipyard::Zapd::LogSink gLog = NULL;
int gHelpers;
volatile long gRetired; // helpers that stopped early for memory
CRITICAL_SECTION gLock;
struct LockSetup {
    LockSetup() { InitializeCriticalSection(&gLock); }
} gLockSetup;
double gTotalSeconds[ZapdStageCount];
std::map<std::string, double> gTypeSeconds[2];
__declspec(thread) std::map<std::string, double>* tTypeSeconds; // parse and export, made on first use
Shipyard::Archive::StoredZipReader gBundle;
std::string gBundleRoot; // the folder the bundle stands in for, as BundleKey spells it

std::string BundleKey(const std::string& path) {
    std::string key = ShipyardZapdNative(path);
    for (size_t i = 0; i < key.size(); ++i)
        if (key[i] == '\\')
            key[i] = '/';
    while (!key.empty() && key[key.size() - 1] == '/')
        key.erase(key.size() - 1);
    return key;
}

// The path's place inside the bundle, or false when the path is outside the bundled folder.
bool BundleName(const std::string& path, std::string& name) {
    if (!gBundle.IsOpen())
        return false;
    const std::string key = BundleKey(path);
    if (key.size() < gBundleRoot.size() || _strnicmp(key.c_str(), gBundleRoot.c_str(), gBundleRoot.size()) != 0)
        return false;
    if (key.size() == gBundleRoot.size()) {
        name.clear();
        return true;
    }
    if (key[gBundleRoot.size()] != '/')
        return false;
    name = key.substr(gBundleRoot.size() + 1);
    return true;
}
}

bool ShipyardZapdBundled(const std::string& path, std::string* bytes) {
    std::string name;
    return BundleName(path, name) && !name.empty() && gBundle.Read(name, bytes);
}

void ShipyardZapdBundledList(const std::string& folder, std::vector<std::string>& files) {
    std::string name;
    if (!BundleName(folder, name))
        return;
    std::vector<std::string> names;
    gBundle.List(name, names);
    const std::string base = BundleKey(folder) + "/";
    for (size_t i = 0; i < names.size(); ++i)
        files.push_back(base + names[i].substr(name.empty() ? 0 : name.size() + 1));
}

__declspec(thread) double gShipyardZapdSeconds[ZapdStageCount];

void ShipyardZapdAddTypeSeconds(int stage, const std::string& type, double seconds) {
    if (tTypeSeconds == NULL)
        tTypeSeconds = new std::map<std::string, double>[2];
    tTypeSeconds[stage == ZapdStageExport][type] += seconds;
}

ShipyardZapdArchiveLock::ShipyardZapdArchiveLock() {
    EnterCriticalSection(&gLock);
}

ShipyardZapdArchiveLock::~ShipyardZapdArchiveLock() {
    LeaveCriticalSection(&gLock);
}

long ShipyardZapdIncrement(volatile long* value) {
    return InterlockedIncrement(value);
}

// One file can hold tens of megabytes until it is exported, so helpers stop well before memory runs out.
bool ShipyardZapdRetireHelper() {
    const SIZE_T kFloor = 96u << 20;
    MEMORYSTATUS status;
    GlobalMemoryStatus(&status);
    if (status.dwAvailPhys >= kFloor)
        return false;
    InterlockedIncrement(&gRetired);
    return true;
}

namespace {
// Adds this thread's stage and type seconds to the run's totals and frees its own.
void MergeSeconds() {
    {
        ShipyardZapdArchiveLock lock;
        for (int stage = 0; stage < ZapdStageCount; ++stage)
            gTotalSeconds[stage] += gShipyardZapdSeconds[stage];
        for (int kind = 0; kind < 2 && tTypeSeconds != NULL; ++kind)
            for (std::map<std::string, double>::const_iterator it = tTypeSeconds[kind].begin();
                 it != tTypeSeconds[kind].end(); ++it)
                gTypeSeconds[kind][it->first] += it->second;
    }
    for (int stage = 0; stage < ZapdStageCount; ++stage)
        gShipyardZapdSeconds[stage] = 0;
    delete[] tTypeSeconds;
    tTypeSeconds = NULL;
}

struct Helper {
    void (*work)(void*, int);
    void* context;
    int worker;
};

DWORD WINAPI RunHelper(void* parameter) {
    const Helper& helper = *static_cast<const Helper*>(parameter);
    ShipyardZapdArenaEnter();
    helper.work(helper.context, helper.worker);
    MergeSeconds();
    ShipyardZapdArenaLeave();
    return 0;
}
} // namespace

void ShipyardZapdRunParallel(void (*work)(void* context, int worker), void* context) {
    // Cores 1 and 2 beside the caller's hardware thread 3; core 0 keeps the title's main thread.
    static const DWORD kProcessors[] = { 4, 5, 2 };
    const int kMost = sizeof(kProcessors) / sizeof(kProcessors[0]);
    Helper helpers[kMost];
    HANDLE threads[kMost];
    int started = 0;
    for (int i = 0; i < gHelpers && i < kMost; ++i) {
        Helper& helper = helpers[started];
        helper.work = work;
        helper.context = context;
        helper.worker = started + 1;
        // A desktop-sized stack, as the caller's extraction thread has.
        HANDLE thread = CreateThread(NULL, 1024 * 1024, &RunHelper, &helper, CREATE_SUSPENDED, NULL);
        if (thread == NULL)
            break;
#ifdef _XBOX
        XSetThreadProcessor(thread, kProcessors[i]);
#endif
        ResumeThread(thread);
        threads[started++] = thread;
    }
    work(context, 0);
    for (int i = 0; i < started; ++i) {
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
    }
}

static bool Slower(const std::pair<std::string, double>& a, const std::pair<std::string, double>& b) {
    return a.second > b.second;
}

// Appends the slowest types of one stage, such as " parse: Room 20.1, DList 4.0".
static void AppendTypes(std::string& out, const char* label, std::map<std::string, double>& types) {
    std::vector<std::pair<std::string, double> > sorted(types.begin(), types.end());
    std::sort(sorted.begin(), sorted.end(), Slower);
    out += std::string(" ") + label + ":";
    for (size_t i = 0; i < sorted.size() && i < 8; ++i) {
        char item[96];
        _snprintf(item, sizeof(item), " %s %.1f", sorted[i].first.c_str(), sorted[i].second);
        item[sizeof(item) - 1] = 0;
        out += item;
    }
    types.clear();
}

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
        gShipyardZapdSeconds[stage] = gTotalSeconds[stage] = 0;
    if (!request.assetBundle.empty()) {
        if (!gBundle.Open(ShipyardZapdNative(request.assetBundle).c_str())) {
            if (request.log != NULL)
                request.log(("cannot read asset bundle " + request.assetBundle).c_str());
            return 1;
        }
        const size_t dot = request.assetBundle.find_last_of('.');
        gBundleRoot = BundleKey(request.assetBundle.substr(0, dot));
    }
    ShipyardZapdArenaBegin();
    gLog = request.log;
    gHelpers = request.helpers;
    gRetired = 0;
    gShipyardZapdRom = request.rom;
    int result = zapd_report(sizeof(argv) / sizeof(argv[0]), const_cast<char**>(argv), request.done, request.total);
    gShipyardZapdRom = NULL;
    gHelpers = 0;
    gLog = NULL;
    MergeSeconds();
    std::string types;
    AppendTypes(types, "parse", gTypeSeconds[0]);
    AppendTypes(types, "export", gTypeSeconds[1]);
    ShipyardZapdArenaEnd();
    gBundle.Close();
    std::string().swap(gBundleRoot);
    if (request.stages != NULL) {
        // Seconds are summed over the extraction threads; export includes the archive writes it makes.
        const double* s = gTotalSeconds;
        char text[256];
        _snprintf(text, sizeof(text),
                  "threads=%d retired=%ld config=%.1f rom=%.1f xml=%.1f parse=%.1f export=%.1f archive=%.1f free=%.1f finish=%.1f",
                  1 + std::min(request.helpers, 3), gRetired, s[ZapdStageConfig], s[ZapdStageRom], s[ZapdStageXml], s[ZapdStageParse],
                  s[ZapdStageExport], s[ZapdStageArchive], s[ZapdStageFree], s[ZapdStageFinish]);
        text[sizeof(text) - 1] = 0;
        *request.stages = text + types;
    }
    return result;
}
} // namespace Zapd
} // namespace Shipyard
