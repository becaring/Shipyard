#include "rom/rom.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif

namespace Shipyard {
namespace Rom {

ByteOrder DetectByteOrder(const unsigned char* data, size_t size) {
    if (size < 4)
        return ByteOrderUnknown;
    const uint32_t magic = ReadBig32(data);
    if (magic == 0x80371240u)
        return ByteOrderBig;
    if (magic == 0x37804012u)
        return ByteOrderHalfSwapped;
    if (magic == 0x40123780u)
        return ByteOrderLittle;
    return ByteOrderUnknown;
}

bool ToBigEndian(unsigned char* data, size_t size) {
    const ByteOrder order = DetectByteOrder(data, size);
    if (order == ByteOrderBig)
        return true;
    if (order == ByteOrderHalfSwapped && size % 2 == 0) {
        for (size_t i = 0; i < size; i += 2)
            std::swap(data[i], data[i + 1]);
        return true;
    }
    if (order == ByteOrderLittle && size % 4 == 0) {
        for (size_t i = 0; i < size; i += 4) {
            std::swap(data[i], data[i + 3]);
            std::swap(data[i + 1], data[i + 2]);
        }
        return true;
    }
    return false;
}

bool IsArchive(const unsigned char* data, size_t size) {
    static const unsigned char zip[] = {'P', 'K', 0x03, 0x04};
    static const unsigned char rar[] = {'R', 'a', 'r', '!'};
    static const unsigned char sevenZip[] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
    return (size >= sizeof(zip) && !memcmp(data, zip, sizeof(zip))) ||
           (size >= sizeof(rar) && !memcmp(data, rar, sizeof(rar))) ||
           (size >= sizeof(sevenZip) && !memcmp(data, sevenZip, sizeof(sevenZip)));
}

uint32_t Crc32c(const unsigned char* data, size_t size) {
    // Slicing-by-8 with byte-indexed loads; the 8 KB table is rebuilt per call
    // because it costs microseconds against a multi-megabyte ROM.
    uint32_t table[8][256];
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1u)));
        table[0][i] = crc;
    }
    for (uint32_t i = 0; i < 256; ++i)
        for (int slice = 1; slice < 8; ++slice)
            table[slice][i] = (table[slice - 1][i] >> 8) ^ table[0][table[slice - 1][i] & 0xff];
    uint32_t crc = 0xFFFFFFFFu;
    for (; size >= 8; size -= 8, data += 8) {
        crc ^= uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
        crc = table[7][crc & 0xff] ^ table[6][(crc >> 8) & 0xff] ^ table[5][(crc >> 16) & 0xff] ^
              table[4][crc >> 24] ^ table[3][data[4]] ^ table[2][data[5]] ^ table[1][data[6]] ^
              table[0][data[7]];
    }
    for (; size; --size, ++data)
        crc = table[0][(crc ^ *data) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static uint32_t Rotate(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

static void Sha1Block(uint32_t state[5], const unsigned char* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = ReadBig32(block + i * 4);
    for (int i = 16; i < 80; ++i)
        w[i] = Rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const uint32_t next = Rotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rotate(b, 30);
        b = a;
        a = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void Sha1(const unsigned char* data, size_t size, unsigned char digest[20]) {
    uint32_t state[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const uint64_t bits = uint64_t(size) * 8;
    size_t offset = 0;
    for (; size - offset >= 64; offset += 64)
        Sha1Block(state, data + offset);
    unsigned char tail[128] = {0};
    const size_t rest = size - offset;
    memcpy(tail, data + offset, rest);
    tail[rest] = 0x80;
    const size_t tailSize = rest < 56 ? 64 : 128;
    for (int i = 0; i < 8; ++i)
        tail[tailSize - 1 - i] = (unsigned char)(bits >> (i * 8));
    for (size_t block = 0; block < tailSize; block += 64)
        Sha1Block(state, tail + block);
    for (int i = 0; i < 20; ++i)
        digest[i] = (unsigned char)(state[i / 4] >> (24 - (i % 4) * 8));
}

static bool HasRomExtension(const char* name) {
    const size_t length = strlen(name);
    if (length < 4 || name[length - 4] != '.')
        return false;
    const char* extension = name + length - 3;
    return !_stricmp(extension, "z64") || !_stricmp(extension, "n64") || !_stricmp(extension, "v64");
}

std::vector<std::string> Find(const std::string& directory) {
    std::vector<std::string> roms;
    std::string prefix = directory;
    if (!prefix.empty() && prefix[prefix.size() - 1] != '\\' && prefix[prefix.size() - 1] != '/')
        prefix += '\\';
    WIN32_FIND_DATAA entry;
    const HANDLE search = FindFirstFileA((prefix + "*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE)
        return roms;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && HasRomExtension(entry.cFileName))
            roms.push_back(prefix + entry.cFileName);
    } while (FindNextFileA(search, &entry));
    FindClose(search);
    std::sort(roms.begin(), roms.end());
    return roms;
}

LoadResult Load(const std::string& path, const size_t* sizes, size_t sizeCount, std::vector<unsigned char>& data) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
        return LoadUnreadable;
    long size = -1;
    if (!fseek(file, 0, SEEK_END))
        size = ftell(file);
    unsigned char header[8] = {0};
    const size_t headerSize = size > 0 && !fseek(file, 0, SEEK_SET) ? fread(header, 1, sizeof(header), file) : 0;
    LoadResult result = size < 0 ? LoadUnreadable : LoadOk;
    if (result == LoadOk && IsArchive(header, headerSize))
        result = LoadCompressed;
    if (result == LoadOk && std::find(sizes, sizes + sizeCount, size_t(size)) == sizes + sizeCount)
        result = LoadBadSize;
    if (result == LoadOk) {
        data.resize(size_t(size));
        if (fseek(file, 0, SEEK_SET) || fread(&data[0], 1, data.size(), file) != data.size())
            result = LoadUnreadable;
    }
    fclose(file);
    if (result == LoadOk && !ToBigEndian(&data[0], data.size()))
        result = LoadBadOrder;
    if (result != LoadOk)
        data.clear();
    return result;
}

} // namespace Rom
} // namespace Shipyard
