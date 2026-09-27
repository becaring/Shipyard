#include "filesystem.h"
#include <algorithm>
#include <cstring>

namespace xbox_compat {
namespace filesystem {
namespace {
bool separator(char c) {
    return c == '/' || c == '\\';
}
size_t root_length(const std::string& text) {
    size_t colon = text.find(':');
    if (colon == std::string::npos || !colon)
        return 0;
    for (size_t i = 0; i < colon; ++i) {
        char c = text[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }
    return colon + 1;
}
size_t root_end(const std::string& text) {
    size_t position = root_length(text);
    while (position < text.size() && separator(text[position]))
        ++position;
    return position;
}
void native_error(std::error_code& error, DWORD value) {
    error.assign(static_cast<int>(value), std::system_category());
}
bool missing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
bool validate(const path& input, std::error_code& error) {
    const std::string& text = input.string();
    if (text.find('\0') != std::string::npos ||
        text.find_first_of("*?\"<>|") != std::string::npos ||
        text.find(':', root_length(text)) != std::string::npos) {
        native_error(error, ERROR_INVALID_NAME);
        return false;
    }
    if (text.size() >= 2 && separator(text[0]) && separator(text[1])) {
        native_error(error, ERROR_NOT_SUPPORTED);
        return false;
    }
    error.clear();
    return true;
}
// Protects the logical Xbox cwd and host process cwd operations.
Shipyard::Async::mutex cwd_gate;
#if defined(_XBOX) || defined(SHIPYARD_TEST_LOGICAL_CWD)
path logical_cwd;
#endif
std::string native_path(const path& input, std::error_code& error) {
    if (!validate(input, error))
        return std::string();
    if (input.empty()) {
        native_error(error, ERROR_PATH_NOT_FOUND);
        return std::string();
    }
    path resolved = absolute(input, error);
    if (error)
        return std::string();
#if defined(_XBOX) || defined(SHIPYARD_TEST_LOGICAL_CWD)
    resolved = resolved.lexically_normal();
#endif
    std::string native = resolved.string();
    std::replace(native.begin(), native.end(), '/', '\\');
    return native;
}
void throw_if_error(const char* operation, const path& input, const std::error_code& error) {
    if (error)
        throw filesystem_error(operation, input, error);
}
struct owned_handle {
    HANDLE value;
    explicit owned_handle(HANDLE handle) : value(handle) {
    }
    ~owned_handle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }

private:
    owned_handle(const owned_handle&);
    owned_handle& operator=(const owned_handle&);
};
bool query_data(const std::string& native, WIN32_FILE_ATTRIBUTE_DATA& data,
                std::error_code& error) {
    if (!GetFileAttributesExA(native.c_str(), GetFileExInfoStandard, &data)) {
        native_error(error, GetLastError());
        return false;
    }
#ifndef _XBOX
    if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        // Follow the link for status, size and timestamps, matching filesystem
        // queries. Enumeration separately decides whether to descend into it.
        owned_handle file(CreateFileA(native.c_str(), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL));
        if (file.value == INVALID_HANDLE_VALUE) {
            native_error(error, GetLastError());
            return false;
        }
        BY_HANDLE_FILE_INFORMATION target;
        if (!GetFileInformationByHandle(file.value, &target)) {
            native_error(error, GetLastError());
            return false;
        }
        data.dwFileAttributes = target.dwFileAttributes;
        data.ftCreationTime = target.ftCreationTime;
        data.ftLastAccessTime = target.ftLastAccessTime;
        data.ftLastWriteTime = target.ftLastWriteTime;
        data.nFileSizeHigh = target.nFileSizeHigh;
        data.nFileSizeLow = target.nFileSizeLow;
    }
#endif
    error.clear();
    return true;
}
} // namespace

std::string to_native_path(const path& input, std::error_code& error) {
    return native_path(input, error);
}

path& path::make_preferred() {
    std::replace(text_.begin(), text_.end(), '/', '\\');
    return *this;
}
std::string path::generic_string() const {
    std::string result;
    for (size_t i = 0; i < text_.size(); ++i) {
        char c = separator(text_[i]) ? '/' : text_[i];
        if (c == '/' && !result.empty() && result[result.size() - 1] == '/')
            continue;
        result += c;
    }
    return result;
}
path path::root_name() const {
    return text_.substr(0, root_length(text_));
}
path path::root_directory() const {
    size_t n = root_length(text_);
    return n < text_.size() && separator(text_[n]) ? path(text_.substr(n, 1)) : path();
}
path path::root_path() const {
    return text_.substr(0, root_end(text_));
}
path path::relative_path() const {
    return text_.substr(root_end(text_));
}
bool path::is_absolute() const {
    return !root_name().empty() && !root_directory().empty();
}
path path::filename() const {
    if (text_.empty() || separator(text_[text_.size() - 1]) || text_.size() == root_length(text_))
        return path();
    size_t position = text_.find_last_of("/\\");
    return text_.substr(position == std::string::npos ? root_length(text_) : position + 1);
}
path path::parent_path() const {
    size_t root = root_end(text_);
    if (text_.size() <= root)
        return *this;
    size_t finish = text_.size();
    if (separator(text_[finish - 1])) {
        while (finish > root && separator(text_[finish - 1]))
            --finish;
        return text_.substr(0, finish);
    }
    size_t position = text_.find_last_of("/\\");
    if (position == std::string::npos)
        return root_name();
    while (position > root && separator(text_[position - 1]))
        --position;
    return text_.substr(0, (std::max)(root, position));
}
path path::extension() const {
    std::string name = filename().string();
    if (name == "." || name == "..")
        return path();
    size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? path() : path(name.substr(dot));
}
path path::stem() const {
    std::string name = filename().string();
    return name.substr(0, name.size() - extension().string().size());
}
path& path::replace_extension(const path& ext) {
    text_.erase(text_.size() - extension().string().size());
    if (!ext.empty()) {
        if (ext.string()[0] != '.')
            text_ += '.';
        text_ += ext.string();
    }
    return *this;
}
path& path::operator/=(const path& right) {
    path rhs(right); // Also permits p /= p without aliasing its storage.
    if (rhs.is_absolute() || (!rhs.root_name().empty() && rhs.root_name() != root_name())) {
        text_ = rhs.string();
        return *this;
    }
    if (!rhs.root_directory().empty()) {
        text_ = root_name().string() + rhs.string();
        return *this;
    }
    if (!text_.empty() && !separator(text_[text_.size() - 1]) && text_.size() != root_length(text_))
        text_ += '\\';
    text_ += rhs.relative_path().string();
    return *this;
}
path operator/(path left, const path& right) {
    left /= right;
    return left;
}
bool operator==(const path& left, const path& right) {
    return left.generic_string() == right.generic_string();
}
bool operator!=(const path& left, const path& right) {
    return !(left == right);
}
bool operator<(const path& left, const path& right) {
    return left.generic_string() < right.generic_string();
}
path path::lexically_normal() const {
    if (empty())
        return path();
    std::string input = generic_string();
    size_t cursor = root_end(input);
    std::vector<std::string> parts;
    bool trailing = separator(input[input.size() - 1]);
    while (cursor < input.size()) {
        size_t slash = input.find('/', cursor);
        if (slash == std::string::npos)
            slash = input.size();
        std::string part = input.substr(cursor, slash - cursor);
        if (part == ".") {
            trailing = true;
        } else if (part == ".." && !parts.empty() && parts.back() != "..") {
            parts.pop_back();
            trailing = true;
        } else if (part == ".." && !root_directory().empty()) {
            trailing = true;
        } else if (!part.empty()) {
            parts.push_back(part);
            trailing = slash != input.size();
        }
        cursor = slash + 1;
    }
    std::string result = root_path().generic_string();
    for (size_t i = 0; i < parts.size(); ++i) {
        if (!result.empty() && result[result.size() - 1] != '/' && result[result.size() - 1] != ':')
            result += '/';
        result += parts[i];
    }
    if (result.empty())
        return path(".");
    if (trailing && !parts.empty() && parts.back() != ".." && result[result.size() - 1] != '/')
        result += '/';
    return path(result);
}

filesystem_error::filesystem_error(const char* operation, const path& first,
                                   const std::error_code& error)
    : std::system_error(error, std::string(operation) + " [" + first.string() + "]"),
      first_(first) {
}
filesystem_error::filesystem_error(const char* operation, const path& first, const path& second,
                                   const std::error_code& error)
    : std::system_error(error, std::string(operation) + " [" + first.string() + "] [" +
                                   second.string() + "]"),
      first_(first), second_(second) {
}

path current_path(std::error_code& error) {
    Shipyard::Async::lock_guard<Shipyard::Async::mutex> lock(cwd_gate);
#if defined(_XBOX) || defined(SHIPYARD_TEST_LOGICAL_CWD)
    if (logical_cwd.empty()) {
        native_error(error, ERROR_NOT_READY);
        return path();
    }
    error.clear();
    return logical_cwd;
#else
    DWORD length = GetCurrentDirectoryA(0, NULL);
    if (!length) {
        native_error(error, GetLastError());
        return path();
    }
    std::vector<char> buffer(length);
    DWORD got = GetCurrentDirectoryA(length, &buffer[0]);
    if (!got || got >= length) {
        native_error(error, got ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return path();
    }
    error.clear();
    return path(&buffer[0]);
#endif
}
path current_path() {
    std::error_code error;
    path result = current_path(error);
    throw_if_error("current_path", path(), error);
    return result;
}
path absolute(const path& input, std::error_code& error) {
    if (!validate(input, error))
        return path();
    if (input.empty()) {
        native_error(error, ERROR_INVALID_PARAMETER);
        return path();
    }
    if (input.is_absolute()) {
        error.clear();
        return input;
    }
#if defined(_XBOX) || defined(SHIPYARD_TEST_LOGICAL_CWD)
    if (!input.root_name().empty()) {
        native_error(error, ERROR_NOT_SUPPORTED);
        return path();
    }
    path cwd = current_path(error);
    return error ? path() : cwd / input;
#else
    DWORD size = GetFullPathNameA(input.c_str(), 0, NULL, NULL);
    if (!size) {
        native_error(error, GetLastError());
        return path();
    }
    std::vector<char> buffer(size);
    DWORD got = GetFullPathNameA(input.c_str(), size, &buffer[0], NULL);
    if (!got || got >= size) {
        native_error(error, got ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return path();
    }
    error.clear();
    return path(&buffer[0]);
#endif
}
path absolute(const path& input) {
    std::error_code error;
    path result = absolute(input, error);
    throw_if_error("absolute", input, error);
    return result;
}
bool InitWorkingDirectory(const path& directory, std::error_code& error) {
    if (!directory.is_absolute()) {
        native_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }
    current_path(directory, error);
    return !error;
}
void InitWorkingDirectory(const path& directory) {
    std::error_code error;
    InitWorkingDirectory(directory, error);
    throw_if_error("InitWorkingDirectory", directory, error);
}
void current_path(const path& directory, std::error_code& error) {
    path resolved = absolute(directory, error);
    if (error)
        return;
    if (!is_directory(resolved, error)) {
        if (!error)
            native_error(error, ERROR_DIRECTORY);
        return;
    }
    std::string native = native_path(resolved, error);
    if (error)
        return;
    Shipyard::Async::lock_guard<Shipyard::Async::mutex> lock(cwd_gate);
#if defined(_XBOX) || defined(SHIPYARD_TEST_LOGICAL_CWD)
    logical_cwd = resolved.lexically_normal();
    error.clear();
#else
    if (!SetCurrentDirectoryA(native.c_str()))
        native_error(error, GetLastError());
    else
        error.clear();
#endif
}
void current_path(const path& directory) {
    std::error_code error;
    current_path(directory, error);
    throw_if_error("current_path", directory, error);
}

file_status status(const path& input, std::error_code& error) {
    std::string native = native_path(input, error);
    if (error)
        return file_status(missing(error.value()) ? file_type::not_found : file_type::none);
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!query_data(native, data, error)) {
        DWORD failure = static_cast<DWORD>(error.value());
        return file_status(missing(failure) ? file_type::not_found : file_type::none);
    }
    error.clear();
    return file_status((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? file_type::directory
                                                                          : file_type::regular);
}
file_status status(const path& input) {
    std::error_code error;
    file_status result = status(input, error);
    if (result.type() == file_type::none)
        throw_if_error("status", input, error);
    return result;
}
bool exists(const path& input, std::error_code& error) {
    file_status result = status(input, error);
    if (result.type() != file_type::none)
        error.clear();
    return result.type() != file_type::none && result.type() != file_type::not_found;
}
bool exists(const path& input) {
    std::error_code error;
    bool result = exists(input, error);
    throw_if_error("exists", input, error);
    return result;
}
bool is_directory(const path& input, std::error_code& error) {
    return status(input, error).type() == file_type::directory;
}
bool is_directory(const path& input) {
    return status(input).type() == file_type::directory;
}
bool is_regular_file(const path& input, std::error_code& error) {
    return status(input, error).type() == file_type::regular;
}
bool is_regular_file(const path& input) {
    return status(input).type() == file_type::regular;
}
uint64_t file_size(const path& input, std::error_code& error) {
    if (!is_regular_file(input, error)) {
        if (!error)
            native_error(error, ERROR_ACCESS_DENIED);
        return static_cast<uint64_t>(-1);
    }
    std::string native = native_path(input, error);
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!query_data(native, data, error))
        return static_cast<uint64_t>(-1);
    error.clear();
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}
uint64_t file_size(const path& input) {
    std::error_code error;
    uint64_t result = file_size(input, error);
    throw_if_error("file_size", input, error);
    return result;
}
file_time_type last_write_time(const path& input, std::error_code& error) {
    std::string native = native_path(input, error);
    if (error)
        return file_time_type::min();
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!query_data(native, data, error))
        return file_time_type::min();
    error.clear();
    return file_time_type((static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                          data.ftLastWriteTime.dwLowDateTime);
}
file_time_type last_write_time(const path& input) {
    std::error_code error;
    file_time_type result = last_write_time(input, error);
    throw_if_error("last_write_time", input, error);
    return result;
}
bool create_directory(const path& input, std::error_code& error) {
    std::string native = native_path(input, error);
    if (error)
        return false;
    if (CreateDirectoryA(native.c_str(), NULL)) {
        error.clear();
        return true;
    }
    DWORD failure = GetLastError();
    if (failure == ERROR_ALREADY_EXISTS && is_directory(input, error)) {
        error.clear();
        return false;
    }
    native_error(error, failure);
    return false;
}
bool create_directory(const path& input) {
    std::error_code error;
    bool result = create_directory(input, error);
    throw_if_error("create_directory", input, error);
    return result;
}
bool create_directories(const path& input, std::error_code& error) {
    std::string native = native_path(input, error);
    if (error)
        return false;
    file_status found = status(input, error);
    if (found.type() == file_type::directory) {
        error.clear();
        return false;
    }
    if (found.type() == file_type::regular) {
        native_error(error, ERROR_ALREADY_EXISTS);
        return false;
    }
    if (found.type() == file_type::none)
        return false;
    error.clear();
    path parent = input.parent_path();
    if (!parent.empty() && parent != input) {
        create_directories(parent, error);
        if (error)
            return false;
    }
    if (CreateDirectoryA(native.c_str(), NULL)) {
        error.clear();
        return true;
    }
    DWORD failure = GetLastError();
    if (failure == ERROR_ALREADY_EXISTS && is_directory(input, error)) {
        error.clear();
        return false;
    }
    native_error(error, failure);
    return false;
}
bool create_directories(const path& input) {
    std::error_code error;
    bool result = create_directories(input, error);
    throw_if_error("create_directories", input, error);
    return result;
}
bool copy_file(const path& from, const path& to, std::error_code& error) {
    return copy_file(from, to, copy_options::none, error);
}
bool copy_file(const path& from, const path& to, copy_options::type options, std::error_code& error) {
    if (options != copy_options::none && options != copy_options::overwrite_existing) {
        native_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }
    std::string source = native_path(from, error);
    if (error)
        return false;
    std::string target = native_path(to, error);
    if (error)
        return false;
    // The native fail-if-exists check is atomic with competing writers.
    if (!is_regular_file(from, error)) {
        if (!error)
            native_error(error, ERROR_ACCESS_DENIED);
        return false;
    }
    if (CopyFileA(source.c_str(), target.c_str(), options == copy_options::none ? TRUE : FALSE)) {
        error.clear();
        return true;
    }
    native_error(error, GetLastError());
    return false;
}
bool copy_file(const path& from, const path& to) {
    return copy_file(from, to, copy_options::none);
}
bool copy_file(const path& from, const path& to, copy_options::type options) {
    std::error_code error;
    bool result = copy_file(from, to, options, error);
    if (error)
        throw filesystem_error("copy_file", from, to, error);
    return result;
}
bool remove(const path& input, std::error_code& error) {
    std::string native = native_path(input, error);
    if (error) {
        if (missing(error.value()))
            error.clear();
        return false;
    }
    // Remove the entry itself, including dangling links; never its target.
    DWORD attributes = GetFileAttributesA(native.c_str());
    if (attributes == static_cast<DWORD>(-1)) {
        DWORD failure = GetLastError();
        if (missing(failure))
            error.clear();
        else
            native_error(error, failure);
        return false;
    }
    BOOL removed = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryA(native.c_str())
                                                           : DeleteFileA(native.c_str());
    if (removed) {
        error.clear();
        return true;
    }
    DWORD failure = GetLastError();
    if (missing(failure))
        error.clear();
    else
        native_error(error, failure);
    return false;
}
bool remove(const path& input) {
    std::error_code error;
    bool result = remove(input, error);
    throw_if_error("remove", input, error);
    return result;
}
void rename(const path& from, const path& to, std::error_code& error) {
    std::string source = native_path(from, error);
    if (error)
        return;
    std::string target = native_path(to, error);
    if (error)
        return;
    if (MoveFileExA(source.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        error.clear();
    else
        native_error(error, GetLastError());
}
void rename(const path& from, const path& to) {
    std::error_code error;
    rename(from, to, error);
    if (error)
        throw filesystem_error("rename", from, to, error);
}

bool read_file(const path& input, std::vector<unsigned char>& output, std::error_code& error) {
    output.clear();
    std::string native = native_path(input, error);
    if (error)
        return false;
    owned_handle file(CreateFileA(native.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    if (file.value == INVALID_HANDLE_VALUE) {
        native_error(error, GetLastError());
        return false;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file.value, &size)) {
        native_error(error, GetLastError());
        return false;
    }
    if (size.QuadPart < 0 || size.QuadPart > 128 * 1024 * 1024) {
        native_error(error, ERROR_BUFFER_OVERFLOW);
        return false;
    }
    output.resize(static_cast<size_t>(size.QuadPart));
    size_t position = 0;
    while (position < output.size()) {
        DWORD got = 0;
        DWORD count = static_cast<DWORD>(
            (std::min)(output.size() - position, static_cast<size_t>(1024 * 1024)));
        if (!ReadFile(file.value, &output[position], count, &got, NULL)) {
            native_error(error, GetLastError());
            output.clear();
            return false;
        }
        if (!got) {
            native_error(error, ERROR_HANDLE_EOF);
            output.clear();
            return false;
        }
        position += got;
    }
    error.clear();
    return true;
}
bool write_file(const path& output, const void* bytes, size_t size, std::error_code& error) {
    if (!bytes && size) {
        native_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }
    std::string native = native_path(output, error);
    if (error)
        return false;
    owned_handle file(CreateFileA(native.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, NULL));
    if (file.value == INVALID_HANDLE_VALUE) {
        native_error(error, GetLastError());
        return false;
    }
    const unsigned char* data = static_cast<const unsigned char*>(bytes);
    size_t position = 0;
    while (position < size) {
        DWORD written = 0;
        DWORD count =
            static_cast<DWORD>((std::min)(size - position, static_cast<size_t>(1024 * 1024)));
        if (!WriteFile(file.value, data + position, count, &written, NULL)) {
            native_error(error, GetLastError());
            return false;
        }
        if (!written) {
            native_error(error, ERROR_WRITE_FAULT);
            return false;
        }
        position += written;
    }
    if (!FlushFileBuffers(file.value)) {
        native_error(error, GetLastError());
        return false;
    }
    error.clear();
    return true;
}

struct directory_iterator::state {
    HANDLE search; // Owned by this shared single-pass iterator state.
    filesystem::path root;
    directory_entry entry;
    state() : search(INVALID_HANDLE_VALUE) {
    }
    ~state() {
        close();
    }
    void close() {
        if (search != INVALID_HANDLE_VALUE) {
            FindClose(search);
        }
        search = INVALID_HANDLE_VALUE;
    }
    bool accept(const WIN32_FIND_DATAA& data) {
        if (!std::strcmp(data.cFileName, ".") || !std::strcmp(data.cFileName, ".."))
            return false;
        entry = directory_entry(root / data.cFileName);
        return true;
    }
    void advance(std::error_code& error) {
        WIN32_FIND_DATAA data;
        while (FindNextFileA(search, &data)) {
            if (accept(data)) {
                error.clear();
                return;
            }
        }
        DWORD failure = GetLastError();
        close();
        if (failure == ERROR_NO_MORE_FILES)
            error.clear();
        else
            native_error(error, failure);
    }
};
directory_iterator::directory_iterator(const path& root, std::error_code& error) {
    std::string native = native_path(root, error);
    if (error)
        return;
    if (!is_directory(root, error)) {
        if (!error)
            native_error(error, ERROR_DIRECTORY);
        return;
    }
    std::shared_ptr<state> newState(new state());
    newState->root = root;
    if (!native.empty() && !separator(native[native.size() - 1]))
        native += '\\';
    native += '*';
    WIN32_FIND_DATAA data;
    newState->search = FindFirstFileA(native.c_str(), &data);
    if (newState->search == INVALID_HANDLE_VALUE) {
        DWORD failure = GetLastError();
        if (failure == ERROR_FILE_NOT_FOUND || failure == ERROR_NO_MORE_FILES) {
            if (!is_directory(root, error) && !error)
                native_error(error, ERROR_DIRECTORY);
        } else
            native_error(error, failure);
        return;
    }
    if (!newState->accept(data))
        newState->advance(error);
    else
        error.clear();
    state_ = newState;
}
directory_iterator::directory_iterator(const path& root) {
    std::error_code error;
    directory_iterator created(root, error);
    throw_if_error("directory_iterator", root, error);
    state_ = created.state_;
}
bool directory_iterator::at_end() const {
    return !state_ || state_->search == INVALID_HANDLE_VALUE;
}
const directory_entry& directory_iterator::operator*() const {
    if (at_end())
        throw std::logic_error("dereferencing end directory iterator");
    return state_->entry;
}
directory_iterator& directory_iterator::increment(std::error_code& error) {
    if (at_end()) {
        native_error(error, ERROR_INVALID_PARAMETER);
        return *this;
    }
    state_->advance(error);
    return *this;
}
directory_iterator& directory_iterator::operator++() {
    path root = at_end() ? path() : state_->root;
    std::error_code error;
    increment(error);
    throw_if_error("directory_iterator increment", root, error);
    return *this;
}
bool directory_iterator::operator==(const directory_iterator& other) const {
    return at_end() ? other.at_end() : state_ == other.state_;
}
directory_iterator begin(directory_iterator iterator) {
    return iterator;
}
directory_iterator end(const directory_iterator&) {
    return directory_iterator();
}

struct recursive_directory_iterator::state {
    std::vector<directory_iterator> stack;
    directory_options::type options;
    state() : options(directory_options::none) {
    }
};
recursive_directory_iterator::recursive_directory_iterator(const path& root,
                                                           std::error_code& error) {
    recursive_directory_iterator created(root, directory_options::none, error);
    state_ = created.state_;
}
recursive_directory_iterator::recursive_directory_iterator(const path& root,
                                                           directory_options::type options,
                                                           std::error_code& error) {
    if (options != directory_options::none &&
        options != directory_options::follow_directory_symlink) {
        native_error(error, ERROR_INVALID_PARAMETER);
        return;
    }
    directory_iterator first(root, error);
    if (!error && first != directory_iterator()) {
        state_.reset(new state());
        state_->options = options;
        state_->stack.push_back(first);
    }
}
recursive_directory_iterator::recursive_directory_iterator(const path& root) {
    std::error_code error;
    recursive_directory_iterator created(root, error);
    throw_if_error("recursive_directory_iterator", root, error);
    state_ = created.state_;
}
recursive_directory_iterator::recursive_directory_iterator(const path& root,
                                                           directory_options::type options) {
    std::error_code error;
    recursive_directory_iterator created(root, options, error);
    throw_if_error("recursive_directory_iterator", root, error);
    state_ = created.state_;
}
bool recursive_directory_iterator::at_end() const {
    return !state_ || state_->stack.empty();
}
const directory_entry& recursive_directory_iterator::operator*() const {
    if (at_end())
        throw std::logic_error("dereferencing end recursive iterator");
    return *state_->stack.back();
}
recursive_directory_iterator& recursive_directory_iterator::increment(std::error_code& error) {
    if (at_end()) {
        native_error(error, ERROR_INVALID_PARAMETER);
        return *this;
    }
    directory_entry current = **this;
    bool descend = false;
#ifndef _XBOX
    std::string native = native_path(current.path(), error);
    if (error) {
        state_->stack.clear();
        return *this;
    }
    DWORD attributes = GetFileAttributesA(native.c_str());
    if (attributes == static_cast<DWORD>(-1)) {
        native_error(error, GetLastError());
        state_->stack.clear();
        return *this;
    }
    bool link = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if (!link || state_->options == directory_options::follow_directory_symlink) {
        descend = current.is_directory(error);
        // A dangling directory link has no directory to descend into.
        if (link && missing(error.value()))
            error.clear();
    } else
        error.clear();
#else
    // There are no directory symlinks on the XDK filesystem; requesting follow
    // still enumerates all actual directories and never discards feature code.
    descend = current.is_directory(error);
#endif
    if (error) {
        state_->stack.clear();
        return *this;
    }
    if (descend) {
        directory_iterator child(current.path(), error);
        if (error) {
            state_->stack.clear();
            return *this;
        }
        if (child != directory_iterator()) {
            state_->stack.push_back(child);
            return *this;
        }
    }
    while (!state_->stack.empty()) {
        state_->stack.back().increment(error);
        if (error) {
            state_->stack.clear();
            return *this;
        }
        if (state_->stack.back() != directory_iterator())
            return *this;
        state_->stack.pop_back();
    }
    error.clear();
    return *this;
}
recursive_directory_iterator& recursive_directory_iterator::operator++() {
    path input = at_end() ? path() : (**this).path();
    std::error_code error;
    increment(error);
    throw_if_error("recursive iterator increment", input, error);
    return *this;
}
bool recursive_directory_iterator::operator==(const recursive_directory_iterator& other) const {
    return at_end() ? other.at_end() : state_ == other.state_;
}
recursive_directory_iterator begin(recursive_directory_iterator iterator) {
    return iterator;
}
recursive_directory_iterator end(const recursive_directory_iterator&) {
    return recursive_directory_iterator();
}
} // namespace filesystem
} // namespace xbox_compat
