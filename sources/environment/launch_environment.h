#pragma once

// Xbox has no process environment. Explicit, immutable launch configuration.
// File handle/read pattern follows SoH360 platform/xbox360/platform.cpp ReadPath.
// The parser and reader preserve the two consumer implementations' behavior.
#if defined(_XBOX)
#include <xtl.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <string>
#include <vector>

namespace Shipyard { namespace Environment {

enum FileStatus { FileLoaded, FileAbsent, FileIoError, FileInvalid };
enum PublishStatus { PublishLoaded, PublishAbsentResult, PublishInvalid,
                     PublishIoError, PublishAlreadyInitialized };

struct FileHandle {
    HANDLE handle;
    explicit FileHandle(HANDLE h) : handle(h) {}
    ~FileHandle() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
private:
    FileHandle(const FileHandle&);
    FileHandle& operator=(const FileHandle&);
};

// The caller selects the path and decides whether an absent file is acceptable.
inline FileStatus ReadBoundedFile(const char* path, std::string& result) {
    FileHandle file(CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    if (file.handle == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? FileAbsent : FileIoError;
    DWORD high = 0;
    DWORD length = GetFileSize(file.handle, &high);
    if (length == INVALID_FILE_SIZE || high != 0) return FileIoError;
    // Explicit configuration size limit; reject rather than truncate.
    if (length > 1024 * 1024) return FileInvalid;
    try {
        std::string text(length, '\0');
        DWORD actual = 0;
        if (length && (!ReadFile(file.handle, &text[0], length, &actual, NULL) || actual != length))
            return FileIoError;
        result.swap(text);
        return FileLoaded;
    } catch (...) {
        return FileIoError;
    }
}

class Store {
public:
    Store() : initialized_(false) {}

    bool Initialized() const { return initialized_; }

    PublishStatus PublishAbsent() {
        if (initialized_) return PublishAlreadyInitialized;
        initialized_ = true;
        return PublishAbsentResult;
    }

    PublishStatus PublishText(const std::string& text) {
        if (initialized_) return PublishAlreadyInitialized;
        try {
            std::vector<Entry> candidate;
            if (!Parse(text, candidate)) return PublishInvalid;
            values_.swap(candidate);
            initialized_ = true;
            return PublishLoaded;
        } catch (...) {
            return PublishIoError;
        }
    }

    const char* Get(const char* name) const {
        if (!name || !initialized_) return NULL;
        for (size_t i = 0; i < values_.size(); ++i)
            if (values_[i].name == name) return values_[i].value.c_str();
        return NULL;
    }

private:
    struct Entry {
        std::string name;
        std::string value;
        Entry(const std::string& n, const std::string& v) : name(n), value(v) {}
    };
    std::vector<Entry> values_;
    bool initialized_;

    static bool ValidName(const std::string& name) {
        if (name.empty()) return false;
        for (size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            if (c != '_' && !(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
                !(i > 0 && c >= '0' && c <= '9')) return false;
        }
        return true;
    }

    static bool Parse(const std::string& text, std::vector<Entry>& result) {
        if (text.find('\0') != std::string::npos) return false;
        size_t cursor = 0;
        while (cursor < text.size()) {
            size_t end = text.find('\n', cursor);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(cursor, end - cursor);
            cursor = end + 1;
            if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
            const size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#') continue;
            const size_t equals = line.find('=');
            if (equals == std::string::npos) return false;
            const std::string name = line.substr(0, equals);
            if (!ValidName(name)) return false;
            for (size_t i = 0; i < result.size(); ++i)
                if (result[i].name == name) return false;
            result.push_back(Entry(name, line.substr(equals + 1)));
        }
        return true;
    }

    Store(const Store&);
    Store& operator=(const Store&);
};

}} // namespace Shipyard::Environment
