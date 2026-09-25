// SolidWorks container/tessellation layout adapted from the MIT-licensed
// openswx and Wintaru/model_viewer projects, and the Apache-2.0 cadmpeg
// descriptor-table format. See SLDPRT-LICENSE.txt.
#include "SLDPRTReader.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <zlib.h>

namespace Slic3r::SolidWorks {
namespace {
using Bytes = std::vector<unsigned char>;
constexpr size_t max_file_size = 512u * 1024 * 1024;
constexpr size_t max_stream_size = 256u * 1024 * 1024;
constexpr size_t max_vertices = 8u * 1024 * 1024;
constexpr unsigned char chunk_marker[] = {0x14, 0, 6, 0, 8, 0};
constexpr unsigned char tess_marker[] = {4, 0, 0, 0, 8, 0, 0, 0, 2, 0, 0, 0};

void report(const ProgressFn &progress, unsigned percent)
{
    if (progress && !progress(percent))
        throw Cancelled{};
}

[[noreturn]] void invalid(const char *reason)
{
    throw std::runtime_error(std::string("Cannot import this SolidWorks part: ") + reason +
        " Open and save it again in SolidWorks with tessellation data enabled, or export it as STEP.");
}

uint32_t u32(const Bytes &b, size_t offset)
{
    if (offset > b.size() || b.size() - offset < 4)
        invalid("truncated data.");
    return uint32_t(b[offset]) | (uint32_t(b[offset + 1]) << 8) |
           (uint32_t(b[offset + 2]) << 16) | (uint32_t(b[offset + 3]) << 24);
}

float f32(const Bytes &b, size_t offset)
{
    const uint32_t bits = u32(b, offset);
    float value;
    static_assert(sizeof(value) == sizeof(bits), "32-bit float required");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Bytes inflate_stream(const Bytes &file, size_t start, size_t compressed_size, size_t size)
{
    if (size == 0 || size > max_stream_size)
        invalid("the saved mesh exceeds the supported size or is empty.");
    Bytes output(size);
    z_stream stream{};
    stream.next_in = const_cast<Bytef *>(file.data() + start);
    stream.avail_in = static_cast<uInt>(compressed_size);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(size);
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        invalid("could not initialize decompression.");
    const int result = inflate(&stream, Z_FINISH);
    const bool valid = result == Z_STREAM_END && stream.total_out == size &&
                       stream.total_in == compressed_size;
    inflateEnd(&stream);
    if (!valid)
        invalid("the saved mesh is damaged or incomplete.");
    return output;
}

Bytes display_stream(const Bytes &file, const ProgressFn &progress)
{
    constexpr unsigned char ole_magic[] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
    if (file.size() < 8)
        invalid("the file is too short.");
    if (std::equal(std::begin(ole_magic), std::end(ole_magic), file.begin()))
        invalid("legacy OLE documents (SolidWorks 2014 and earlier) are not supported.");
    const auto header_end = file.begin() + std::min(file.size(), size_t(64));
    if (std::search(file.begin(), header_end, std::begin(chunk_marker), std::end(chunk_marker)) == header_end)
        invalid("the file is not a supported SolidWorks document.");

    Bytes display;
    size_t inflated_bytes = 0;
    const unsigned rotation = file[7] & 7;
    size_t pos = 0;
    while (pos < file.size()) {
        report(progress, unsigned(40 * pos / file.size()));
        const auto marker = std::search(file.begin() + pos, file.end(), std::begin(chunk_marker), std::end(chunk_marker));
        if (marker == file.end())
            break;
        const size_t at = size_t(marker - file.begin());
        pos = at + sizeof(chunk_marker);
        if (at < 4 || file.size() - (at - 4) < 30)
            continue;
        const size_t start = at - 4;
        const uint32_t flags = u32(file, start + 14);
        const size_t compressed = u32(file, start + 18);
        const size_t uncompressed = u32(file, start + 22);
        const size_t name_size = u32(file, start + 26);
        if (name_size == 0 || name_size > 512 || name_size > file.size() - start - 30)
            continue;
        std::string name;
        for (size_t i = 0; i < name_size; ++i) {
            const unsigned c = file[start + 30 + i];
            const unsigned decoded = ((c << rotation) | (c >> (8 - rotation))) & 255;
            if (decoded < 32 || decoded >= 128) {
                name.clear();
                break;
            }
            name += char(decoded);
        }
        if (name.empty() || flags < 65536)
            continue; // A reference record has no inline payload.
        const size_t data_start = start + 30 + name_size;
        const bool is_display = name == "Contents/DisplayLists";
        if (compressed > file.size() - data_start) {
            if (is_display)
                invalid("the display stream is truncated.");
            continue;
        }
        pos = data_start + compressed;
        if (!is_display || compressed == 0)
            continue;
        if (uncompressed > max_file_size - inflated_bytes)
            invalid("the display streams exceed the decompression limit.");
        inflated_bytes += uncompressed;
        Bytes next = inflate_stream(file, data_start, compressed, uncompressed);
        // Duplicate records may refer to the same saved cache. Different
        // caches must not be combined: they may be different configurations.
        if (!display.empty() && display != next)
            invalid("multiple different saved display meshes were found.");
        display = std::move(next);
    }
    if (display.empty())
        invalid("no saved display mesh was found.");
    return display;
}

Mesh tessellate(const Bytes &data, const ProgressFn &progress)
{
    Mesh mesh;
    size_t pos = 0;
    while (pos < data.size()) {
        report(progress, 40 + unsigned(59 * pos / data.size()));
        // Headers may be byte-aligned. Never reinterpret the stream as an
        // aligned float/u32 array (also important on ARM and big-endian hosts).
        const auto marker = std::search(data.begin() + pos, data.end(), std::begin(tess_marker), std::end(tess_marker));
        if (marker == data.end())
            break;
        const size_t start = size_t(marker - data.begin());
        pos = start + sizeof(tess_marker);
        const size_t strips = u32(data, start + 12);
        if (strips > (data.size() - start - 16) / 4)
            invalid("a saved display array is truncated.");
        if (strips == 0)
            continue;
        const size_t tail = start + 16 + strips * 4;
        // Edge/index arrays also use 4,8,2. Identify geometry by the next
        // descriptor before interpreting values as strip lengths. Once a
        // geometry record is identified, invalid lengths must fail the import
        // rather than silently omit a face from an otherwise valid mesh.
        if (data.size() - tail < 12 || u32(data, tail) != 12 ||
            u32(data, tail + 4) != 100 || u32(data, tail + 8) != 2)
            continue;
        const size_t position_count = u32(data, tail + 12);
        if (strips > max_vertices / 3)
            invalid("the saved mesh has too many vertices.");
        std::vector<uint32_t> sizes;
        sizes.reserve(strips);
        size_t total = 0;
        for (size_t i = 0; i < strips; ++i) {
            const uint32_t size = u32(data, start + 16 + i * 4);
            if (size < 3 || size > max_vertices || total > max_vertices - size)
                invalid("a saved triangle strip is invalid or too large.");
            total += size;
            sizes.push_back(size);
        }
        // 12-byte float3 positions use descriptor 12,100,2,TOTAL. Integer
        // index arrays use 1,8,2,TOTAL and must not be read as coordinates.
        if (position_count != total)
            invalid("the saved position count does not match the mesh.");
        const size_t positions = tail + 16;
        if (total > (data.size() - positions) / 12)
            invalid("a tessellation record is incomplete.");
        if (mesh.vertices.size() > max_vertices - total)
            invalid("the saved mesh has too many vertices.");
        // Each channel has its own 16-byte descriptor, including normals.
        // Treating that descriptor as floats silently loses small faces.
        size_t next = positions + total * 12;
        size_t normal_start = 0, normal_count = 0;
        const size_t endpoints = 2 * total - 2 * strips;
        for (unsigned channel = 0; channel < 4; ++channel) {
            // Some faces omit auxiliary edge channels. They do not affect
            // the triangle mesh; only parse them when their descriptor exists.
            if (channel == 1 && (data.size() - next < 16 || u32(data, next) != 4 ||
                u32(data, next + 4) != 8 || u32(data, next + 8) != 2))
                break;
            const uint32_t width = u32(data, next);
            const uint32_t kind = u32(data, next + 4);
            const uint32_t flags = u32(data, next + 8);
            const size_t count = u32(data, next + 12);
            const uint32_t expected_width = channel == 0 ? 12 : channel == 3 ? 1 : 4;
            if (width != expected_width || kind != (channel == 0 ? 100u : 8u) || flags != 2)
                invalid("the saved tessellation uses an unsupported channel layout.");
            next += 16;
            if (count > (data.size() - next) / width)
                invalid("a tessellation channel is truncated.");
            if (channel == 0) {
                if (count != 0 && count != total)
                    invalid("the saved normal count does not match the mesh.");
                normal_start = next;
                normal_count = count;
            } else if (channel == 2) {
                if (count != strips)
                    invalid("the saved strip counts do not match.");
                for (size_t i = 0; i < strips; ++i)
                    if (u32(data, next + 4 * i) != 2 * sizes[i] - 2)
                        invalid("the saved strip endpoints do not match.");
            } else if (count != 0 && count != endpoints) {
                invalid("the saved endpoint count does not match the mesh.");
            }
            next += count * width;
        }
        for (size_t i = 0; i < total; ++i) {
            for (size_t j = 0; j < 3; ++j) {
                const float v = f32(data, positions + i * 12 + j * 4);
                if (!std::isfinite(v) || std::abs(v) > 10000.f)
                    invalid("invalid coordinates or normals were found.");
                if (i < normal_count && !std::isfinite(f32(data, normal_start + i * 12 + j * 4)))
                    invalid("invalid coordinates or normals were found.");
            }
        }

        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (size_t i = 0; i < total; ++i) {
            if ((i & 65535) == 0)
                report(progress, 40 + unsigned(59 * (positions + i * 12) / data.size()));
            mesh.vertices.push_back({f32(data, positions + i * 12) * 1000.f,
                                     f32(data, positions + i * 12 + 4) * 1000.f,
                                     f32(data, positions + i * 12 + 8) * 1000.f});
        }
        for (uint32_t count : sizes) {
            for (uint32_t k = 0; k + 2 < count; ++k) {
                uint32_t a = base + k, b = a + 1, c = a + 2;
                if (k & 1)
                    std::swap(a, b); // Triangle strip winding alternates.
                const auto &p = mesh.vertices[a], &q = mesh.vertices[b], &r = mesh.vertices[c];
                const std::array<double, 3> u = {double(q[0]) - p[0], double(q[1]) - p[1], double(q[2]) - p[2]};
                const std::array<double, 3> v = {double(r[0]) - p[0], double(r[1]) - p[1], double(r[2]) - p[2]};
                const std::array<double, 3> cross = {u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]};
                if (cross[0] == 0 && cross[1] == 0 && cross[2] == 0)
                    continue; // Duplicate vertices may join strips.
                mesh.indices.insert(mesh.indices.end(), {a, b, c});
            }
            base += count;
        }
        pos = next;
    }
    if (mesh.indices.empty())
        invalid("the saved display mesh contains no supported triangles.");
    return mesh;
}
} // namespace

Mesh decode(const Bytes &file, const ProgressFn &progress)
{
    report(progress, 0);
    if (file.size() > max_file_size)
        invalid("the file exceeds the 512 MiB import limit.");
    Mesh mesh = tessellate(display_stream(file, progress), progress);
    report(progress, 100);
    return mesh;
}

Mesh read(std::istream &input, const ProgressFn &progress)
{
    report(progress, 0);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0 || size > std::streamoff(max_file_size))
        invalid("the file is empty, unreadable or exceeds the 512 MiB import limit.");
    input.seekg(0);
    Bytes file(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char *>(file.data()), size))
        invalid("the file could not be read completely.");
    return decode(file, progress);
}
} // namespace Slic3r::SolidWorks
