#ifndef SHIPYARD_FILESYSTEM_H
#define SHIPYARD_FILESYSTEM_H
#include "../async/native.h"
#include <string>
#include <vector>
#include <memory>
#include <iterator>
#include <system_error>
#include <stdint.h>

namespace xbox_compat {
namespace filesystem {
class path {
public:
    path() {
    }
    path(const char* value) : text_(value ? value : "") {
    }
    path(const std::string& value) : text_(value) {
    }
    const std::string& string() const {
        return text_;
    }
    std::string generic_string() const;
    path& make_preferred();
    const char* c_str() const {
        return text_.c_str();
    }
    operator std::string() const {
        return text_;
    }
    bool empty() const {
        return text_.empty();
    }
    void clear() {
        text_.clear();
    }
    path root_name() const;
    path root_directory() const;
    path root_path() const;
    path relative_path() const;
    bool is_absolute() const;
    path parent_path() const;
    path filename() const;
    path extension() const;
    path stem() const;
    path lexically_normal() const;
    path& replace_extension(const path& extension = path());
    path& operator/=(const path& right);
    path& concat(const path& right) {
        text_ += right.text_;
        return *this;
    }
    path& operator+=(const path& right) {
        return concat(right);
    }

private:
    std::string text_;
};
path operator/(path left, const path& right);
bool operator==(const path& left, const path& right);
bool operator!=(const path& left, const path& right);
bool operator<(const path& left, const path& right);

class filesystem_error : public std::system_error {
public:
    filesystem_error(const char* operation, const path& first, const std::error_code& error);
    filesystem_error(const char* operation, const path& first, const path& second,
                     const std::error_code& error);
    const filesystem::path& path1() const {
        return first_;
    }
    const filesystem::path& path2() const {
        return second_;
    }

private:
    filesystem::path first_, second_;
};
namespace file_type {
enum type { none = 0, not_found = -1, regular = 1, directory = 2 };
}
class file_status {
public:
    explicit file_status(file_type::type value = file_type::none) : value_(value) {
    }
    file_type::type type() const {
        return value_;
    }

private:
    file_type::type value_;
};

// Native FILETIME ticks; no chrono clock conversion.
class file_time_type {
public:
    file_time_type() : ticks_(0) {
    }
    explicit file_time_type(uint64_t ticks) : ticks_(ticks) {
    }
    static file_time_type min() {
        return file_time_type();
    }
    uint64_t native_ticks() const {
        return ticks_;
    }
    bool operator==(const file_time_type& other) const {
        return ticks_ == other.ticks_;
    }
    bool operator!=(const file_time_type& other) const {
        return ticks_ != other.ticks_;
    }
    bool operator<(const file_time_type& other) const {
        return ticks_ < other.ticks_;
    }

private:
    uint64_t ticks_;
};
namespace directory_options {
enum type { none = 0, follow_directory_symlink = 1 };
}
namespace platform_properties {
#if defined(_XBOX)
// Xbox mounts are roots; the XDK filesystem has no directory reparse links.
const bool directory_symlinks = false;
#else
const bool directory_symlinks = true;
#endif
} // namespace platform_properties

// Xbox uses a synchronized logical cwd, set to an existing absolute device
// path before relative operations. Windows uses the process cwd.
void InitWorkingDirectory(const path& directory);
bool InitWorkingDirectory(const path& directory, std::error_code& error);
path current_path();
path current_path(std::error_code& error);
void current_path(const path& directory);
void current_path(const path& directory, std::error_code& error);
path absolute(const path& input);
path absolute(const path& input, std::error_code& error);
std::string to_native_path(const path& input, std::error_code& error);
file_status status(const path& input);
file_status status(const path& input, std::error_code& error);
bool exists(const path& input);
bool exists(const path& input, std::error_code& error);
bool is_directory(const path& input);
bool is_directory(const path& input, std::error_code& error);
bool is_regular_file(const path& input);
bool is_regular_file(const path& input, std::error_code& error);
uint64_t file_size(const path& input);
uint64_t file_size(const path& input, std::error_code& error);
file_time_type last_write_time(const path& input);
file_time_type last_write_time(const path& input, std::error_code& error);
bool create_directory(const path& input);
bool create_directory(const path& input, std::error_code& error);
bool create_directories(const path& input);
bool create_directories(const path& input, std::error_code& error);
// Copy fails if the destination exists.
bool copy_file(const path& from, const path& to);
bool copy_file(const path& from, const path& to, std::error_code& error);
namespace copy_options {
enum type { none = 0, overwrite_existing = 1 };
}
bool copy_file(const path& from, const path& to, copy_options::type options);
bool copy_file(const path& from, const path& to, copy_options::type options, std::error_code& error);
bool remove(const path& input);
bool remove(const path& input, std::error_code& error);
void rename(const path& from, const path& to);
void rename(const path& from, const path& to, std::error_code& error);

// Whole-file reads are capped at 128 MiB; archive streaming remains separate.
bool read_file(const path& input, std::vector<unsigned char>& output, std::error_code& error);
bool write_file(const path& output, const void* bytes, size_t size, std::error_code& error);

class directory_entry {
public:
    directory_entry() {
    }
    explicit directory_entry(const filesystem::path& value) : path_(value) {
    }
    const filesystem::path& path() const {
        return path_;
    }
    operator const filesystem::path&() const {
        return path_;
    }
    bool is_directory() const {
        return filesystem::is_directory(path_);
    }
    bool is_directory(std::error_code& error) const {
        return filesystem::is_directory(path_, error);
    }
    bool is_regular_file() const {
        return filesystem::is_regular_file(path_);
    }
    bool is_regular_file(std::error_code& error) const {
        return filesystem::is_regular_file(path_, error);
    }

private:
    filesystem::path path_;
};
class directory_iterator {
public:
    directory_iterator() {
    }
    explicit directory_iterator(const path& root);
    directory_iterator(const path& root, std::error_code& error);
    const directory_entry& operator*() const;
    const directory_entry* operator->() const {
        return &**this;
    }
    directory_iterator& operator++();
    directory_iterator& increment(std::error_code& error);
    bool operator==(const directory_iterator& other) const;
    bool operator!=(const directory_iterator& other) const {
        return !(*this == other);
    }

private:
    struct state;
    std::shared_ptr<state> state_;
    bool at_end() const;
};
directory_iterator begin(directory_iterator iterator);
directory_iterator end(const directory_iterator& iterator);

class recursive_directory_iterator {
public:
    recursive_directory_iterator() {
    }
    explicit recursive_directory_iterator(const path& root);
    recursive_directory_iterator(const path& root, std::error_code& error);
    recursive_directory_iterator(const path& root, directory_options::type options);
    recursive_directory_iterator(const path& root, directory_options::type options,
                                 std::error_code& error);
    const directory_entry& operator*() const;
    const directory_entry* operator->() const {
        return &**this;
    }
    recursive_directory_iterator& operator++();
    recursive_directory_iterator& increment(std::error_code& error);
    bool operator==(const recursive_directory_iterator& other) const;
    bool operator!=(const recursive_directory_iterator& other) const {
        return !(*this == other);
    }

private:
    struct state;
    std::shared_ptr<state> state_;
    bool at_end() const;
};
recursive_directory_iterator begin(recursive_directory_iterator iterator);
recursive_directory_iterator end(const recursive_directory_iterator& iterator);
} // namespace filesystem
} // namespace xbox_compat
#endif
