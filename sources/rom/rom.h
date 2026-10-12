#ifndef SHIPYARD_ROM_ROM_H
#define SHIPYARD_ROM_ROM_H
// N64 ROM intake for games that build their archives from the user's ROM.
// Shipyard finds, loads, normalizes and hashes ROMs; each game owns its version
// tables, header fixes and archive policy. Byte order is converted explicitly
// rather than through host-endian reads, so results match on PC and Xenon.
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace Shipyard {
namespace Rom {

enum ByteOrder {
    ByteOrderUnknown,
    ByteOrderBig,         // .z64
    ByteOrderHalfSwapped, // .v64
    ByteOrderLittle       // .n64
};

enum LoadResult { LoadOk, LoadUnreadable, LoadCompressed, LoadBadSize, LoadBadOrder };

ByteOrder DetectByteOrder(const unsigned char* data, size_t size);
// Converts to .z64 order in place; fails for an unknown order or misaligned size.
bool ToBigEndian(unsigned char* data, size_t size);
bool IsArchive(const unsigned char* data, size_t size);
uint32_t Crc32c(const unsigned char* data, size_t size);
void Sha1(const unsigned char* data, size_t size, unsigned char digest[20]);

inline uint32_t ReadBig32(const unsigned char* data) {
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) | (uint32_t(data[2]) << 8) | data[3];
}

// Lists .z64, .n64 and .v64 files directly inside directory, sorted by name.
std::vector<std::string> Find(const std::string& directory);
// Reads a ROM of an accepted size into data, reusing its storage, in .z64 order.
LoadResult Load(const std::string& path, const size_t* sizes, size_t sizeCount, std::vector<unsigned char>& data);

} // namespace Rom
} // namespace Shipyard

#endif
