#ifndef SHIPYARD_CONSOLE_VARIABLE_KEY_H
#define SHIPYARD_CONSOLE_VARIABLE_KEY_H
#include <string>
#include <cstring>
#include <stddef.h>
#include <stdint.h>

namespace Shipyard { namespace ConsoleVariables {
// Container keys own their bytes. Only a direct temporary lookup may borrow;
// even a copy of that temporary becomes owning before it can be retained.
class NameKey {
public:
    enum BorrowedLookup { Borrow };
    NameKey() : borrowed_(NULL), length_(0) {}
    NameKey(const char* value) : owned_(value), borrowed_(NULL), length_(owned_.size()) {}
    NameKey(const std::string& value) : owned_(value), borrowed_(NULL), length_(value.size()) {}
    NameKey(const char* value, BorrowedLookup) : borrowed_(value), length_(std::strlen(value)) {}
    NameKey(const NameKey& other) : owned_(other.c_str(), other.size()), borrowed_(NULL), length_(other.size()) {}
    NameKey& operator=(const NameKey& other) {
        if (this != &other) {
            owned_.assign(other.c_str(), other.size());
            borrowed_ = NULL;
            length_ = other.size();
        }
        return *this;
    }
    const char* c_str() const { return borrowed_ ? borrowed_ : owned_.c_str(); }
    size_t size() const { return length_; }
    bool operator==(const NameKey& other) const {
        return size() == other.size() && std::memcmp(c_str(), other.c_str(), size()) == 0;
    }
private:
    std::string owned_;
    const char* borrowed_;
    size_t length_;
};
struct NameHash {
    size_t operator()(const NameKey& key) const {
        uint32_t hash = 2166136261u;
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(key.c_str());
        for (size_t i = 0; i < key.size(); ++i) hash = (hash ^ bytes[i]) * 16777619u;
        return hash;
    }
};
}}
#endif
