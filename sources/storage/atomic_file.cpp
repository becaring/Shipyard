#include "atomic_file.h"
#include <limits.h>
#include <string.h>
namespace Shipyard { namespace Storage {
void WriteAtomicFile(const xbox_compat::filesystem::path& destination, const void* bytes, size_t size) {
    if (size > UINT_MAX) throw std::length_error("atomic file exceeds native write size");
    if (!bytes && size) throw std::invalid_argument("atomic file data is null");
    const std::string temporary = destination.string() + ".tmp";
    HANDLE file = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) throw Async::native_error("create atomic temporary", GetLastError());
    DWORD written = 0;
    DWORD error = WriteFile(file, bytes, (DWORD)size, &written, NULL) ? ERROR_SUCCESS : GetLastError();
    if (error == ERROR_SUCCESS && written != size) error = ERROR_WRITE_FAULT;
    if (error == ERROR_SUCCESS && !FlushFileBuffers(file)) error = GetLastError();
    if (!CloseHandle(file) && error == ERROR_SUCCESS) error = GetLastError();
    if (error == ERROR_SUCCESS && !MoveFileExA(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) error = GetLastError();
    if (error != ERROR_SUCCESS) { DeleteFileA(temporary.c_str()); throw Async::native_error("write atomic file", error); }
}
void WriteAtomicFile(const xbox_compat::filesystem::path& destination, const std::string& bytes) {
    WriteAtomicFile(destination, bytes.data(), bytes.size());
}
void WriteAtomicRelativeFile(const char* root, const char* relative, const void* bytes, size_t size) {
    if (!root || !*root || (root[strlen(root)-1] != '/' && root[strlen(root)-1] != '\\') ||
        !relative || !*relative || strchr(relative, ':') ||
        strstr(relative, "..") || relative[0] == '/' || relative[0] == '\\')
        throw std::invalid_argument("invalid relative atomic file path");
    std::string destination(root);
    destination += relative;
    for (size_t i = 0; i < destination.size(); ++i)
        if (destination[i] == '/') destination[i] = '\\';
    WriteAtomicFile(xbox_compat::filesystem::path(destination), bytes, size);
}
}}
