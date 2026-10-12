#include "rom/rom.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace Shipyard::Rom;

static int failures = 0;
#define CHECK(condition)                                                                                   \
    do {                                                                                                   \
        if (!(condition)) {                                                                                \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                                            \
            ++failures;                                                                                    \
        }                                                                                                  \
    } while (0)

static uint32_t BitwiseCrc32c(const unsigned char* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static std::string Hex(const unsigned char* digest) {
    char text[41];
    for (int i = 0; i < 20; ++i)
        sprintf(text + i * 2, "%02x", digest[i]);
    return std::string(text, 40);
}

static void Write(const std::string& path, const std::vector<unsigned char>& bytes) {
    FILE* file = fopen(path.c_str(), "wb");
    if (!bytes.empty())
        fwrite(&bytes[0], 1, bytes.size(), file);
    fclose(file);
}

int main(int argc, char** argv) {
    const unsigned char check[] = "123456789";
    CHECK(Crc32c(check, 9) == 0xE3069283u);
    std::vector<unsigned char> noise(4099);
    uint32_t seed = 0x1234567u;
    for (size_t i = 0; i < noise.size(); ++i) {
        seed = seed * 1103515245u + 12345u;
        noise[i] = (unsigned char)(seed >> 16);
    }
    for (size_t length = 0; length <= noise.size(); length += length < 40 ? 1 : 257)
        CHECK(Crc32c(&noise[0], length) == BitwiseCrc32c(&noise[0], length));

    unsigned char digest[20];
    Sha1((const unsigned char*)"", 0, digest);
    CHECK(Hex(digest) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    Sha1((const unsigned char*)"abc", 3, digest);
    CHECK(Hex(digest) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    const char* twoBlocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Sha1((const unsigned char*)twoBlocks, strlen(twoBlocks), digest);
    CHECK(Hex(digest) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    std::vector<unsigned char> million(1000000, 'a');
    Sha1(&million[0], million.size(), digest);
    CHECK(Hex(digest) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

    std::vector<unsigned char> z64(64);
    const unsigned char magic[] = {0x80, 0x37, 0x12, 0x40};
    memcpy(&z64[0], magic, 4);
    for (size_t i = 4; i < z64.size(); ++i)
        z64[i] = (unsigned char)(i * 7);
    std::vector<unsigned char> v64(z64), n64(z64);
    for (size_t i = 0; i < z64.size(); i += 2)
        v64[i] = z64[i + 1], v64[i + 1] = z64[i];
    for (size_t i = 0; i < z64.size(); i += 4)
        for (int j = 0; j < 4; ++j)
            n64[i + j] = z64[i + 3 - j];
    CHECK(DetectByteOrder(&z64[0], z64.size()) == ByteOrderBig);
    CHECK(DetectByteOrder(&v64[0], v64.size()) == ByteOrderHalfSwapped);
    CHECK(DetectByteOrder(&n64[0], n64.size()) == ByteOrderLittle);
    std::vector<unsigned char> converted(v64);
    CHECK(ToBigEndian(&converted[0], converted.size()) && converted == z64);
    converted = n64;
    CHECK(ToBigEndian(&converted[0], converted.size()) && converted == z64);
    converted = z64;
    CHECK(ToBigEndian(&converted[0], converted.size()) && converted == z64);
    CHECK(!ToBigEndian(&n64[0], 6));
    CHECK(ReadBig32(&z64[0]) == 0x80371240u);
    unsigned char unknown[4] = {0x12, 0x34, 0x56, 0x78};
    CHECK(DetectByteOrder(unknown, 4) == ByteOrderUnknown && !ToBigEndian(unknown, 4));

    const unsigned char zip[] = {'P', 'K', 3, 4}, rar[] = {'R', 'a', 'r', '!'};
    const unsigned char sevenZip[] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
    CHECK(IsArchive(zip, 4) && IsArchive(rar, 4) && IsArchive(sevenZip, 6));
    CHECK(!IsArchive(sevenZip, 5) && !IsArchive(&z64[0], z64.size()));

    if (argc > 1) {
        const std::string folder = argv[1];
        Write(folder + "\\b.N64", n64);
        Write(folder + "\\a.z64", z64);
        Write(folder + "\\c.v64", v64);
        Write(folder + "\\notes.txt", z64);
        std::vector<unsigned char> zipped(z64);
        memcpy(&zipped[0], zip, 4);
        Write(folder + "\\d.z64", zipped);
        Write(folder + "\\e.z64", std::vector<unsigned char>(z64.begin(), z64.begin() + 32));
        std::vector<unsigned char> odd(z64);
        memcpy(&odd[0], unknown, 4);
        Write(folder + "\\f.z64", odd);
        const std::vector<std::string> found = Find(folder);
        CHECK(found.size() == 6);
        CHECK(found.size() == 6 && found[0] == folder + "\\a.z64" && found[1] == folder + "\\b.N64");
        const size_t sizes[] = {16, 64};
        std::vector<unsigned char> data;
        for (int i = 0; i < 3; ++i)
            CHECK(Load(found[i], sizes, 2, data) == LoadOk && data == z64);
        CHECK(Load(folder + "\\d.z64", sizes, 2, data) == LoadCompressed && data.empty());
        CHECK(Load(folder + "\\e.z64", sizes, 2, data) == LoadBadSize);
        CHECK(Load(folder + "\\f.z64", sizes, 2, data) == LoadBadOrder);
        CHECK(Load(folder + "\\missing.z64", sizes, 2, data) == LoadUnreadable);
        CHECK(Find(folder + "\\missing").empty());
    }
    if (failures)
        return 1;
    puts("PASS rom intake: crc32c, sha1, byte orders, archives, discovery and loading");
    return 0;
}
