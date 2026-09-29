#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace Shipyard {
namespace Xenos {
// Prebuilt shader pack: microcode compiled offline, with the XDK's own
// D3DX, for the lowered sources the runtime would otherwise compile. Entries
// are keyed exactly like the runtime cache: profile, entry and source. The
// pack is written on a PC and read on the console; every integer is stored
// big-endian.
//   header: magic, version, _XDK_VER, D3DX_SDK_VERSION, count, 0, 0, 0 (u32 each)
//   index:  count entries of {first, second (u64); length, offset, size,
//           checksum (u32)}, strictly ascending by (first, second, length)
//   blobs:  microcode at 4-byte aligned offsets; identical code is stored once
struct ShaderPackDigest {
    uint64_t first, second;
    uint32_t length;
};
struct ShaderPackEntry {
    ShaderPackDigest digest;
    uint32_t offset, size, checksum;
};
const uint32_t ShaderPackMagic = 0x5348504B; // 'SHPK'
const uint32_t ShaderPackVersion = 1;
const size_t ShaderPackHeaderBytes = 32;
const size_t ShaderPackEntryBytes = 32;
const uint32_t ShaderPackMaxEntries = 1u << 20;

// SHA-256 (FIPS 180-4) of the key; the first 16 bytes, big-endian, plus the
// key length identify an entry. The offline tool uses Python's hashlib.
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
inline uint32_t ShaderPackRead32(const unsigned char* p);
inline uint64_t ShaderPackRead64(const unsigned char* p);
inline ShaderPackDigest DigestShaderKey(const std::string& key) {
    unsigned char sha[32];
    ShaderPackSha256(key, sha);
    ShaderPackDigest digest = {ShaderPackRead64(sha), ShaderPackRead64(sha + 8), (uint32_t)key.size()};
    return digest;
}
inline bool ShaderPackBefore(const ShaderPackDigest& a, const ShaderPackDigest& b) {
    if (a.first != b.first) return a.first < b.first;
    if (a.second != b.second) return a.second < b.second;
    return a.length < b.length;
}
inline bool ShaderPackSame(const ShaderPackDigest& a, const ShaderPackDigest& b) {
    return a.first == b.first && a.second == b.second && a.length == b.length;
}
// CRC-32 (IEEE 802.3, as zlib.crc32) of the microcode.
inline uint32_t ShaderPackChecksum(const void* bytes, size_t size) {
    const unsigned char* data = static_cast<const unsigned char*>(bytes);
    uint32_t crc = 0xFFFFFFFFU;
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
inline ShaderPackEntry ShaderPackParseEntry(const unsigned char* p) {
    ShaderPackEntry entry;
    entry.digest.first = ShaderPackRead64(p);
    entry.digest.second = ShaderPackRead64(p + 8);
    entry.digest.length = ShaderPackRead32(p + 16);
    entry.offset = ShaderPackRead32(p + 20);
    entry.size = ShaderPackRead32(p + 24);
    entry.checksum = ShaderPackRead32(p + 28);
    return entry;
}
} // namespace Xenos
} // namespace Shipyard
