#ifndef SHIPYARD_ARCHIVE_STORED_ZIP_WRITER_H
#define SHIPYARD_ARCHIVE_STORED_ZIP_WRITER_H
// Streams uncompressed zip (O2R) entries to one buffered sequential file, so
// extraction never holds the whole archive in memory or pays for deflate.
// A repeated name with identical bytes is skipped; changed bytes replace the
// earlier entry, whose old bytes stay unreferenced. The directory is name-sorted.
// No zip64: more than 65535 entries or 4 GiB of data fails the archive.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>

namespace Shipyard {
namespace Archive {

uint32_t Crc32(const uint32_t table[8][256], const unsigned char* data, size_t size);
void BuildCrc32Table(uint32_t table[8][256]);

class StoredZipWriter {
  public:
    StoredZipWriter();
    ~StoredZipWriter();
    bool Open(const char* path);
    bool Add(const std::string& name, const void* data, size_t size);
    bool Contains(const std::string& name) const;
    // Writes the central directory and closes the file; false if any step failed.
    bool Finish();

  private:
    struct Entry {
        uint32_t offset;
        uint32_t size;
        uint32_t crc;
        uint64_t check;
    };
    bool Write(const void* data, size_t size);
    bool Flush();

    FILE* mFile;
    bool mFailed;
    uint64_t mOffset;
    std::vector<unsigned char> mBuffer;
    std::map<std::string, Entry> mEntries;
    uint32_t mTable[8][256];

    StoredZipWriter(const StoredZipWriter&);
    StoredZipWriter& operator=(const StoredZipWriter&);
};

} // namespace Archive
} // namespace Shipyard

#endif
