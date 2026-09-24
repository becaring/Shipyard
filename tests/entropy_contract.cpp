#include "entropy/native.h"
#include <stdio.h>

int main() {
    unsigned char bytes[32] = {};
    if (!Shipyard::Entropy::FillBytes(bytes, sizeof(bytes))) return 1;
    puts("PASS native byte fill");
    return 0;
}
