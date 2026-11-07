#include "archive/stored_zip_reader.h"
#include <string.h>

namespace Shipyard {
namespace Archive {
namespace {
uint32_t Little16(const unsigned char* p) {
    return p[0] | (p[1] << 8);
}
uint32_t Little32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
} // namespace

StoredZipReader::StoredZipReader() : mFile(NULL) {
    InitializeCriticalSection(&mLock);
}

StoredZipReader::~StoredZipReader() {
    Close();
    DeleteCriticalSection(&mLock);
}

std::string StoredZipReader::Key(const std::string& name) {
    std::string key;
    key.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i] == '\\' ? '/' : name[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c == '/' && (key.empty() || key[key.size() - 1] == '/'))
            continue;
        key += c;
    }
    if (!key.empty() && key[key.size() - 1] == '/')
        key.erase(key.size() - 1);
    return key;
}

bool StoredZipReader::Open(const char* path) {
    Close();
    mFile = fopen(path, "rb");
    if (mFile == NULL)
        return false;
    // The end record sits in the last 22 bytes plus at most a 64 KiB comment.
    std::vector<unsigned char> tail;
    if (fseek(mFile, 0, SEEK_END) == 0) {
        const long size = ftell(mFile);
        const long length = size < 22 + 65535 ? size : 22 + 65535;
        tail.resize(length > 0 ? length : 0);
        if (tail.empty() || fseek(mFile, size - length, SEEK_SET) != 0 || fread(&tail[0], 1, tail.size(), mFile) != tail.size())
            tail.clear();
    }
    size_t end = tail.size() >= 22 ? tail.size() - 22 + 1 : 0;
    while (end > 0 && Little32(&tail[end - 1]) != 0x06054b50)
        --end;
    if (end == 0) {
        Close();
        return false;
    }
    const unsigned char* record = &tail[end - 1];
    const uint32_t count = Little16(record + 10);
    const uint32_t directorySize = Little32(record + 12);
    const uint32_t directoryOffset = Little32(record + 16);
    std::vector<unsigned char> directory(directorySize);
    if (count == 0xFFFF || directoryOffset == 0xFFFFFFFF || fseek(mFile, (long)directoryOffset, SEEK_SET) != 0 ||
        (directorySize && fread(&directory[0], 1, directorySize, mFile) != directorySize)) {
        Close();
        return false;
    }
    size_t at = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 46 > directory.size() || Little32(&directory[at]) != 0x02014b50) {
            Close();
            return false;
        }
        const unsigned char* header = &directory[at];
        const uint32_t nameLength = Little16(header + 28);
        const size_t next = at + 46 + nameLength + Little16(header + 30) + Little16(header + 32);
        if (next > directory.size() || Little16(header + 10) != 0 || Little32(header + 20) != Little32(header + 24)) {
            Close();
            return false;
        }
        Entry entry;
        entry.name.assign((const char*)header + 46, nameLength);
        entry.offset = Little32(header + 42);
        entry.size = Little32(header + 24);
        if (!entry.name.empty() && entry.name[entry.name.size() - 1] != '/')
            mEntries[Key(entry.name)] = entry;
        at = next;
    }
    return true;
}

void StoredZipReader::Close() {
    if (mFile != NULL)
        fclose(mFile);
    mFile = NULL;
    std::map<std::string, Entry>().swap(mEntries);
}

bool StoredZipReader::Read(const std::string& name, std::string* bytes) const {
    std::map<std::string, Entry>::const_iterator it = mEntries.find(Key(name));
    if (it == mEntries.end())
        return false;
    if (bytes == NULL)
        return true;
    bytes->resize(it->second.size);
    EnterCriticalSection(&mLock);
    unsigned char local[30];
    bool ok = fseek(mFile, (long)it->second.offset, SEEK_SET) == 0 && fread(local, 1, 30, mFile) == 30 &&
              Little32(local) == 0x04034b50 &&
              fseek(mFile, (long)(Little16(local + 26) + Little16(local + 28)), SEEK_CUR) == 0 &&
              (bytes->empty() || fread(&(*bytes)[0], 1, bytes->size(), mFile) == bytes->size());
    LeaveCriticalSection(&mLock);
    return ok;
}

void StoredZipReader::List(const std::string& folder, std::vector<std::string>& names) const {
    const std::string prefix = folder.empty() ? std::string() : Key(folder) + "/";
    for (std::map<std::string, Entry>::const_iterator it = mEntries.lower_bound(prefix);
         it != mEntries.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        names.push_back(it->second.name);
}

} // namespace Archive
} // namespace Shipyard
