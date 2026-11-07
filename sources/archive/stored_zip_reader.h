#ifndef SHIPYARD_ARCHIVE_STORED_ZIP_READER_H
#define SHIPYARD_ARCHIVE_STORED_ZIP_READER_H
// Reads entries of an uncompressed zip on demand, holding only its directory in
// memory, so a folder of thousands of small files can be copied as one file.
// Names match case-insensitively with '/' separators. Reads are safe from any
// thread. Compressed and zip64 archives fail to open.
#include <stdint.h>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif

namespace Shipyard {
namespace Archive {

class StoredZipReader {
  public:
    StoredZipReader();
    ~StoredZipReader();
    bool Open(const char* path);
    void Close();
    bool IsOpen() const { return mFile != NULL; }
    // Null bytes only checks that the entry exists.
    bool Read(const std::string& name, std::string* bytes) const;
    // Appends the stored names of every entry inside folder, at any depth; an empty folder lists all.
    void List(const std::string& folder, std::vector<std::string>& names) const;

  private:
    struct Entry {
        std::string name;
        uint32_t offset;
        uint32_t size;
    };
    static std::string Key(const std::string& name);
    FILE* mFile;
    std::map<std::string, Entry> mEntries;
    mutable CRITICAL_SECTION mLock;

    StoredZipReader(const StoredZipReader&);
    StoredZipReader& operator=(const StoredZipReader&);
};

} // namespace Archive
} // namespace Shipyard
#endif
