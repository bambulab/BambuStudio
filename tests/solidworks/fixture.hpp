#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>

namespace SldprtFixture {
using Bytes = std::vector<unsigned char>;
inline void word(Bytes &b, uint32_t n) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(n >> (8 * i)));
}
inline void set_word(Bytes &b, size_t p, uint32_t n) {
    for (int i = 0; i < 4; ++i) b.at(p + i) = static_cast<unsigned char>(n >> (8 * i));
}
inline void real(Bytes &b, float n) {
    uint32_t bits; std::memcpy(&bits, &n, 4); word(b, bits);
}
inline void descriptor(Bytes &b, unsigned width, unsigned kind, unsigned count) {
    word(b, width); word(b, kind); word(b, 2); word(b, count);
}
// A 20 x 10 mm planar strip in metres. The second triangle must reverse winding.
inline Bytes face(unsigned padding = 0, float x = .020f) {
    Bytes b(padding, 0x55);
    descriptor(b, 4, 8, 1); word(b, 4);
    descriptor(b, 12, 100, 4);
    for (auto p : std::vector<std::array<float, 3>>{{0,0,0}, {x,0,0}, {0,.010f,0}, {x,.010f,0}})
        for (float v : p) real(b, v);
    descriptor(b, 12, 100, 4);
    for (int i = 0; i < 4; ++i) { real(b, 0); real(b, 0); real(b, 1); }
    descriptor(b, 4, 8, 0);
    descriptor(b, 4, 8, 1); word(b, 6);
    descriptor(b, 1, 8, 0);
    return b;
}
inline Bytes compress(const Bytes &plain) {
    z_stream z{};
    if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("deflate init failed");
    Bytes compressed(compressBound(static_cast<uLong>(plain.size())));
    z.next_in = const_cast<Bytef *>(plain.data()); z.avail_in = static_cast<uInt>(plain.size());
    z.next_out = compressed.data(); z.avail_out = static_cast<uInt>(compressed.size());
    const int result = deflate(&z, Z_FINISH);
    compressed.resize(z.total_out); deflateEnd(&z);
    if (result != Z_STREAM_END) throw std::runtime_error("deflate failed");
    return compressed;
}
inline Bytes document(const Bytes &plain = face(), unsigned key = 4, const std::string &name = "Contents/DisplayLists") {
    Bytes b(16, 0); b[7] = static_cast<unsigned char>(key);
    const Bytes compressed = compress(plain);
    word(b, 0);
    for (unsigned char x : {0x14, 0, 6, 0, 8, 0}) b.push_back(x);
    word(b, 0xfd); word(b, 65536);
    word(b, static_cast<uint32_t>(compressed.size())); word(b, static_cast<uint32_t>(plain.size()));
    word(b, static_cast<uint32_t>(name.size()));
    for (unsigned char c : name) b.push_back(static_cast<unsigned char>((c >> key) | (c << (8 - key))));
    b.insert(b.end(), compressed.begin(), compressed.end());
    return b;
}
}
