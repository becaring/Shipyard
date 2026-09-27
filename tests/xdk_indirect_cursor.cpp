// XDK 16.00.11886 /O2 regression fixture. Project ReadWords before compiling.
// The unprojected loop hoists *command and advances it only once. The local
// cursor projection must advance four commands and copy eight distinct words.
#include <string.h>

union Command {
    struct { unsigned int w0, w1; } words;
    long long alignment;
};

volatile float cursorOutput[8];

extern "C" __declspec(noinline) void ReadWords(Command** command) {
    float coords[8];
    for (int i = 0; i < 8; i += 2) {
        ++(*command);
        memcpy(&coords[i], &(*command)->words.w0, sizeof(float));
        memcpy(&coords[i + 1], &(*command)->words.w1, sizeof(float));
    }
    for (int i = 0; i < 8; ++i) cursorOutput[i] = coords[i];
}
