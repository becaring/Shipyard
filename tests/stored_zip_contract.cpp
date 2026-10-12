#include "archive/stored_zip_writer.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using Shipyard::Archive::StoredZipWriter;

// Writes the archives that test_stored_zip.py reads back with Python's zipfile.
int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    const std::string folder = argv[1];
    uint32_t table[8][256];
    Shipyard::Archive::BuildCrc32Table(table);
    if (Shipyard::Archive::Crc32(table, (const unsigned char*)"123456789", 9) != 0xCBF43926u) {
        puts("FAIL crc32 check value");
        return 1;
    }

    StoredZipWriter writer;
    if (!writer.Open((folder + "\\basic.o2r").c_str()))
        return 1;
    std::vector<unsigned char> large(3u << 20);
    for (size_t i = 0; i < large.size(); ++i)
        large[i] = (unsigned char)(i * 31 + (i >> 9));
    std::string utf8 = "textures/\xC3\xA9t\xC3\xA9";
    bool ok = writer.Add("version", "\x00\x12\x34\x56\x78", 5) && writer.Add("objects/b", "first", 5) &&
              writer.Add("objects/a", "", 0) && writer.Add("large", &large[0], large.size()) &&
              writer.Add("objects/b", "again", 5) && writer.Add("objects/c", "short", 5) &&
              writer.Add("objects/c", "longer value", 12) && writer.Add(utf8, "x", 1) &&
              writer.Add("dup/same", "abc", 3) && writer.Add("dup/same", "abc", 3);
    for (int i = 0; ok && i < 2000; ++i) {
        char name[32];
        sprintf(name, "many/%04d", 1999 - i);
        ok = writer.Add(name, name, strlen(name));
    }
    ok = ok && writer.Contains("objects/b") && writer.Contains("dup/same") && !writer.Contains("objects") &&
         !writer.Contains("missing");
    if (!ok || !writer.Finish()) {
        puts("FAIL basic archive");
        return 1;
    }

    if (!writer.Open((folder + "\\limit.o2r").c_str()))
        return 1;
    bool accepted = true;
    for (unsigned i = 0; accepted && i < 0x10000u; ++i) {
        char name[16];
        sprintf(name, "%05u", i);
        accepted = writer.Add(name, "", 0);
    }
    if (accepted || writer.Finish()) {
        puts("FAIL entry limit");
        return 1;
    }
    StoredZipWriter unopened;
    if (unopened.Add("x", "y", 1) || unopened.Finish()) {
        puts("FAIL unopened writer");
        return 1;
    }
    puts("PASS stored zip writer");
    return 0;
}
