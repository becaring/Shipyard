#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace Shipyard {
namespace Xenos {
// Prebuilt shader pack: vertex and pixel microcode compiled offline, with the
// XDK's own D3DX, for every Fast3D program key the offline harvest found.
// Entries are keyed by the program key itself and the pack is read into memory
// once. It is written on a PC and read on the console; every integer is
// stored big-endian.
//   header: magic, version, _XDK_VER, D3DX_SDK_VERSION, count, probe, ABI, 0
//           (u32 each), then the SHA-256 of the probe entry's lowered source
//   index:  count entries, strictly ascending by (shaderId0, shaderId1, flags)
//     v2 (40 bytes): shaderId0 (u64); shaderId1, flags, floats, vertex offset,
//                    vertex size, pixel offset, pixel size, checksum (u32)
//     v3 (76 bytes): shaderId0, shaderId1 (u64); the seven u32 fields; then
//                    the SHA-256 of the entry's lowered source
//   blobs:  microcode at 4-byte aligned offsets; identical code is stored once
// v2 stores a 32-bit shaderId1 and the ABI field as 0. The probe proves once
// that this XEX's builder, template and lowering produce the pack's sources;
// v3 source digests let the runtime prove it again for every program it uses.
struct ShaderPackProgram {
    uint64_t shaderId0;
    uint64_t shaderId1;
    uint32_t flags;
    uint32_t floats;
    uint32_t vertexOffset, vertexSize, pixelOffset, pixelSize;
    uint32_t checksum;
    uint32_t digestOffset; // v3 source digest in the file; 0 for v2
};
const uint32_t ShaderPackMagic = 0x5348504B; // 'SHPK'
const uint32_t ShaderPackVersion = 3;
const uint32_t ShaderPackVersion2 = 2;
const size_t ShaderPackHeaderBytes = 64;
const size_t ShaderPackEntryBytes2 = 40;
const size_t ShaderPackEntryBytes3 = 76;
const uint32_t ShaderPackMaxEntries = 1u << 20;
const uint32_t ShaderPackThreePoint = 1u; // FILTER_THREE_POINT
const uint32_t ShaderPackSrgb = 2u;

inline uint32_t ShaderPackFlags(bool threePoint, bool srgb) {
    return (threePoint ? ShaderPackThreePoint : 0u) | (srgb ? ShaderPackSrgb : 0u);
}

// SHA-256 (FIPS 180-4); the offline tool uses Python's hashlib.
inline void ShaderPackSha256(const std::string& message, unsigned char out[32]) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint64_t bits = (uint64_t)message.size() * 8;
    const size_t total = ((message.size() + 8) / 64 + 1) * 64;
    for (size_t block = 0; block < total; block += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            uint32_t word = 0;
            for (int j = 0; j < 4; ++j) {
                const size_t at = block + i * 4 + j;
                unsigned char byte;
                if (at < message.size())
                    byte = (unsigned char)message[at];
                else if (at == message.size())
                    byte = 0x80;
                else if (at >= total - 8)
                    byte = (unsigned char)(bits >> (8 * (total - 1 - at)));
                else
                    byte = 0;
                word = (word << 8) | byte;
            }
            w[i] = word;
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = ((w[i - 15] >> 7) | (w[i - 15] << 25)) ^
                                ((w[i - 15] >> 18) | (w[i - 15] << 14)) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ((w[i - 2] >> 17) | (w[i - 2] << 15)) ^
                                ((w[i - 2] >> 19) | (w[i - 2] << 13)) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = ((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7));
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + s1 + choice + k[i] + w[i];
            const uint32_t s0 = ((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10));
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + majority;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (unsigned char)(h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)h[i];
    }
}
// CRC-32 (IEEE 802.3, as zlib.crc32), continued from a previous value.
inline uint32_t ShaderPackChecksum(const void* bytes, size_t size, uint32_t previous = 0) {
    const unsigned char* data = static_cast<const unsigned char*>(bytes);
    uint32_t crc = ~previous;
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
inline uint32_t ShaderPackRead32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline uint64_t ShaderPackRead64(const unsigned char* p) {
    return ((uint64_t)ShaderPackRead32(p) << 32) | ShaderPackRead32(p + 4);
}
inline size_t ShaderPackEntryBytes(uint32_t version) {
    return version == ShaderPackVersion ? ShaderPackEntryBytes3 : ShaderPackEntryBytes2;
}
// offset is the entry's position in the file, which starts at bytes.
inline ShaderPackProgram ShaderPackParseProgram(const unsigned char* bytes, size_t offset, uint32_t version) {
    const unsigned char* p = bytes + offset;
    const size_t wide = version == ShaderPackVersion ? 4 : 0;
    ShaderPackProgram program;
    program.shaderId0 = ShaderPackRead64(p);
    program.shaderId1 = wide ? ShaderPackRead64(p + 8) : ShaderPackRead32(p + 8);
    p += wide;
    program.flags = ShaderPackRead32(p + 12);
    program.floats = ShaderPackRead32(p + 16);
    program.vertexOffset = ShaderPackRead32(p + 20);
    program.vertexSize = ShaderPackRead32(p + 24);
    program.pixelOffset = ShaderPackRead32(p + 28);
    program.pixelSize = ShaderPackRead32(p + 32);
    program.checksum = ShaderPackRead32(p + 36);
    program.digestOffset = wide ? (uint32_t)(offset + 44) : 0;
    return program;
}
// The checksummed key prefix: shaderId0, shaderId1 (u32 in v2), flags, floats.
inline size_t ShaderPackChecksumHead(const ShaderPackProgram& entry, uint32_t version, unsigned char head[24]) {
    size_t at = 0;
    for (int b = 0; b < 8; ++b)
        head[at++] = (unsigned char)(entry.shaderId0 >> (56 - 8 * b));
    for (int b = version == ShaderPackVersion ? 0 : 4; b < 8; ++b)
        head[at++] = (unsigned char)(entry.shaderId1 >> (56 - 8 * b));
    const uint32_t fields[2] = {entry.flags, entry.floats};
    for (int f = 0; f < 2; ++f)
        for (int b = 0; b < 4; ++b)
            head[at++] = (unsigned char)(fields[f] >> (24 - 8 * b));
    return at;
}
inline bool ShaderPackBefore(uint64_t id0a, uint64_t id1a, uint32_t flagsa,
                             uint64_t id0b, uint64_t id1b, uint32_t flagsb) {
    if (id0a != id0b) return id0a < id0b;
    if (id1a != id1b) return id1a < id1b;
    return flagsa < flagsb;
}
} // namespace Xenos
} // namespace Shipyard
