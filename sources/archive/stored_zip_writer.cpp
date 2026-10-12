#include "archive/stored_zip_writer.h"
#include <string.h>

namespace Shipyard {
namespace Archive {

static const size_t kBufferSize = 1u << 20;
// 1980-01-01 00:00 keeps archives byte-identical across runs.
static const uint16_t kDosDate = (1u << 5) | 1u;

static void Put16(unsigned char*& out, uint32_t value) {
    out[0] = (unsigned char)value;
    out[1] = (unsigned char)(value >> 8);
    out += 2;
}

static void Put32(unsigned char*& out, uint32_t value) {
    Put16(out, value & 0xFFFFu);
    Put16(out, value >> 16);
}

void BuildCrc32Table(uint32_t table[8][256]) {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        table[0][i] = crc;
    }
    for (uint32_t i = 0; i < 256; ++i)
        for (int slice = 1; slice < 8; ++slice)
            table[slice][i] = (table[slice - 1][i] >> 8) ^ table[0][table[slice - 1][i] & 0xff];
}

uint32_t Crc32(const uint32_t table[8][256], const unsigned char* data, size_t size) {
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

// Second, independent fingerprint so skipping a repeat never rests on CRC-32 alone.
static uint64_t Fnv1a(const unsigned char* data, size_t size) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ data[i]) * 0x100000001B3ull;
    return hash;
}

static uint16_t NameFlags(const std::string& name) {
    for (size_t i = 0; i < name.size(); ++i)
        if ((unsigned char)name[i] >= 0x80)
            return 0x0800; // UTF-8 name
    return 0;
}

StoredZipWriter::StoredZipWriter() : mFile(NULL), mFailed(false), mOffset(0) {
    BuildCrc32Table(mTable);
}

StoredZipWriter::~StoredZipWriter() {
    if (mFile)
        fclose(mFile);
}

bool StoredZipWriter::Open(const char* path) {
    if (mFile)
        fclose(mFile);
    mFile = fopen(path, "wb");
    mFailed = mFile == NULL;
    mOffset = 0;
    mEntries.clear();
    mBuffer.clear();
    mBuffer.reserve(kBufferSize);
    return !mFailed;
}

bool StoredZipWriter::Flush() {
    if (!mBuffer.empty() && fwrite(&mBuffer[0], 1, mBuffer.size(), mFile) != mBuffer.size())
        mFailed = true;
    mBuffer.clear();
    return !mFailed;
}

bool StoredZipWriter::Write(const void* data, size_t size) {
    mOffset += size;
    if (mBuffer.size() + size > kBufferSize && !Flush())
        return false;
    if (size >= kBufferSize) {
        if (fwrite(data, 1, size, mFile) != size)
            mFailed = true;
        return !mFailed;
    }
    const unsigned char* bytes = (const unsigned char*)data;
    mBuffer.insert(mBuffer.end(), bytes, bytes + size);
    return true;
}

bool StoredZipWriter::Add(const std::string& name, const void* data, size_t size) {
    if (!mFile || mFailed || name.empty() || name.size() > 0xFFFFu ||
        mOffset + 30 + name.size() + size > 0xFFFFFFFFu) {
        mFailed = true;
        return false;
    }
    Entry entry;
    entry.offset = (uint32_t)mOffset;
    entry.size = (uint32_t)size;
    entry.crc = Crc32(mTable, (const unsigned char*)data, size);
    entry.check = Fnv1a((const unsigned char*)data, size);
    std::map<std::string, Entry>::iterator existing = mEntries.find(name);
    if (existing != mEntries.end() && existing->second.size == entry.size && existing->second.crc == entry.crc &&
        existing->second.check == entry.check)
        return true;
    unsigned char header[30];
    unsigned char* out = header;
    Put32(out, 0x04034B50u);
    Put16(out, 10);
    Put16(out, NameFlags(name));
    Put16(out, 0); // stored
    Put16(out, 0);
    Put16(out, kDosDate);
    Put32(out, entry.crc);
    Put32(out, entry.size);
    Put32(out, entry.size);
    Put16(out, (uint32_t)name.size());
    Put16(out, 0);
    if (existing != mEntries.end())
        existing->second = entry;
    else if (mEntries.size() < 0xFFFFu)
        mEntries.insert(std::make_pair(name, entry));
    else
        mFailed = true;
    return !mFailed && Write(header, sizeof(header)) && Write(name.data(), name.size()) && (!size || Write(data, size));
}

bool StoredZipWriter::Contains(const std::string& name) const {
    return mEntries.find(name) != mEntries.end();
}

bool StoredZipWriter::Finish() {
    if (!mFile)
        return false;
    const uint64_t directoryStart = mOffset;
    for (std::map<std::string, Entry>::const_iterator it = mEntries.begin(); it != mEntries.end() && !mFailed; ++it) {
        const std::string& name = it->first;
        const Entry& entry = it->second;
        unsigned char header[46];
        unsigned char* out = header;
        Put32(out, 0x02014B50u);
        Put16(out, 20);
        Put16(out, 10);
        Put16(out, NameFlags(name));
        Put16(out, 0);
        Put16(out, 0);
        Put16(out, kDosDate);
        Put32(out, entry.crc);
        Put32(out, entry.size);
        Put32(out, entry.size);
        Put16(out, (uint32_t)name.size());
        Put16(out, 0);
        Put16(out, 0);
        Put16(out, 0);
        Put16(out, 0);
        Put32(out, 0);
        Put32(out, entry.offset);
        if (Write(header, sizeof(header)))
            Write(name.data(), name.size());
    }
    const uint64_t directorySize = mOffset - directoryStart;
    if (mOffset > 0xFFFFFFFFu)
        mFailed = true;
    unsigned char end[22];
    unsigned char* out = end;
    Put32(out, 0x06054B50u);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, (uint32_t)mEntries.size());
    Put16(out, (uint32_t)mEntries.size());
    Put32(out, (uint32_t)directorySize);
    Put32(out, (uint32_t)directoryStart);
    Put16(out, 0);
    if (!mFailed)
        Write(end, sizeof(end));
    Flush();
    if (fclose(mFile))
        mFailed = true;
    mFile = NULL;
    mEntries.clear();
    return !mFailed;
}

} // namespace Archive
} // namespace Shipyard
