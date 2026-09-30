#include "storage/atomic_file.h"
#include "filesystem/filesystem.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    using xbox_compat::filesystem::path;
    xbox_compat::filesystem::InitWorkingDirectory(path(argv[1]));
    const char payload[] = "same logical root";
    std::error_code error;
    if (!xbox_compat::filesystem::write_file(path("ordinary.bin"), payload, sizeof(payload), error)) return 2;
    Shipyard::Storage::WriteAtomicFile(path("atomic.bin"), payload, sizeof(payload));
    std::vector<unsigned char> ordinary, atomic;
    if (!xbox_compat::filesystem::read_file(path("ordinary.bin"), ordinary, error) ||
        !xbox_compat::filesystem::read_file(path("atomic.bin"), atomic, error)) return 3;
    if (ordinary != atomic) return 4;
    try {
        Shipyard::Storage::WriteAtomicFile(path("bad?name"), payload, sizeof(payload));
        return 5;
    } catch (const xbox_compat::filesystem::filesystem_error& invalid) {
        if (invalid.code().value() != ERROR_INVALID_NAME) return 6;
    }
    std::puts("atomic and ordinary file writes share logical resolution and validation");
    return 0;
}
