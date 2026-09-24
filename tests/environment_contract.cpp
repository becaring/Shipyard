#include "environment/launch_environment.h"
#include <stdio.h>
#include <string.h>
#include <string>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main(int argc, char** argv) {
    CHECK(argc == 6);
    using namespace Shipyard::Environment;
    std::string text;
    CHECK(ReadBoundedFile(argv[2], text) == FileAbsent);
    CHECK(ReadBoundedFile(argv[3], text) == FileIoError);
    CHECK(ReadBoundedFile(argv[4], text) == FileInvalid);
    CHECK(ReadBoundedFile(argv[5], text) == FileLoaded);
    CHECK(text.size() == 1024 * 1024);
    CHECK(ReadBoundedFile(argv[1], text) == FileLoaded);
    CHECK(text == "# comment\r\nSSB64_MAX_FRAMES=123\r\nVALUE= a=b #literal \nEMPTY=");

    Store values;
    CHECK(!values.Initialized());
    CHECK(values.Get("SSB64_MAX_FRAMES") == NULL);
    CHECK(values.PublishText(text) == PublishLoaded);
    const char* first = values.Get("SSB64_MAX_FRAMES");
    CHECK(first != NULL && strcmp(first, "123") == 0);
    CHECK(strcmp(values.Get("VALUE"), " a=b #literal ") == 0);
    CHECK(strcmp(values.Get("EMPTY"), "") == 0);
    CHECK(values.Get("ssb64_max_frames") == NULL);
    CHECK(values.Get(NULL) == NULL);
    CHECK(values.PublishText("SSB64_MAX_FRAMES=changed") == PublishAlreadyInitialized);
    CHECK(values.PublishAbsent() == PublishAlreadyInitialized);
    CHECK(values.Get("SSB64_MAX_FRAMES") == first);

    const char* invalid[] = {
        "SSB64_MAX_FRAMES=123\nSSB64_MAX_FRAMES=456",
        "SSB64_MAX_FRAMES=123\nnot an assignment",
        "BAD-NAME=value",
        "1BAD=value",
        " NAME=value",
        "SSB64_MAX_FRAMES=123\nBAD=x\0y",
    };
    const size_t lengths[] = {strlen(invalid[0]), strlen(invalid[1]), strlen(invalid[2]),
                              strlen(invalid[3]), strlen(invalid[4]), 28};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        Store candidate;
        CHECK(candidate.PublishText(std::string(invalid[i], lengths[i])) == PublishInvalid);
        CHECK(!candidate.Initialized());
        CHECK(candidate.Get("SSB64_MAX_FRAMES") == NULL);
        CHECK(candidate.PublishText("GOOD=ok") == PublishLoaded);
        CHECK(strcmp(candidate.Get("GOOD"), "ok") == 0);
    }
    Store absent;
    CHECK(absent.PublishAbsent() == PublishAbsentResult);
    CHECK(absent.Initialized());
    CHECK(absent.Get("anything") == NULL);
    CHECK(absent.PublishText("GOOD=ok") == PublishAlreadyInitialized);
    puts("Shipyard environment contract PASS");
    return 0;
}
