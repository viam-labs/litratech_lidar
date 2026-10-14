#include "scan_points.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

namespace {
constexpr double k_pi = 3.14159265358979323846;
}

namespace litratech {
namespace {

void append_f32_le(std::vector<unsigned char>& out, float value) {
    static_assert(sizeof(float) == 4, "PCD encoding expects 32-bit float");
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    out.push_back(static_cast<unsigned char>(bits & 0xffu));
    out.push_back(static_cast<unsigned char>((bits >> 8) & 0xffu));
    out.push_back(static_cast<unsigned char>((bits >> 16) & 0xffu));
    out.push_back(static_cast<unsigned char>((bits >> 24) & 0xffu));
}

}  // namespace

std::vector<Point> points_from_blocks(const std::vector<ldcp_sdk::ScanBlock>& blocks,
                                      double range_min_m,
                                      double range_max_m,
                                      bool invert_frame) {
    std::vector<Point> points;
    if (blocks.empty() || blocks.front().block_count <= 0 || blocks.front().block_length <= 0) {
        return points;
    }

    const ldcp_sdk::ScanBlock& first = blocks.front();
    const int fov_deg = first.angular_fov == ldcp_sdk::ANGULAR_FOV_360DEG ? 360 : 270;
    const int beam_count = first.block_count * first.block_length * 360 / fov_deg;
    if (beam_count <= 0) {
        return points;
    }

    const double angle_sign = invert_frame ? -1.0 : 1.0;
    points.reserve(static_cast<size_t>(first.block_count) * static_cast<size_t>(first.block_length));

    for (const ldcp_sdk::ScanBlock& block : blocks) {
        if (block.layers.empty()) {
            continue;
        }
        const std::vector<int>& ranges = block.layers.front().ranges;
        for (size_t i = 0; i < ranges.size(); ++i) {
            if (ranges[i] == 0) {
                continue;
            }
            const double range_m = static_cast<double>(ranges[i]) * k_range_meters_per_count;
            if (range_m < range_min_m || range_m > range_max_m) {
                continue;
            }
            const int beam_index =
                (block.block_index - block.block_count / 2) * static_cast<int>(ranges.size()) +
                static_cast<int>(i);
            const double angle = angle_sign * static_cast<double>(beam_index) * (2.0 * k_pi) /
                                 static_cast<double>(beam_count);
            // Viam: +y forward, +x right. The device angle is measured from
            // forward toward the left, so right is the negative sine.
            points.push_back(Point{static_cast<float>(-range_m * std::sin(angle)),
                                   static_cast<float>(range_m * std::cos(angle)),
                                   0.f});
        }
    }
    return points;
}

std::vector<unsigned char> encode_pcd(const std::vector<Point>& points) {
    std::ostringstream header;
    header << "VERSION .7\n"
           << "FIELDS x y z\n"
           << "SIZE 4 4 4\n"
           << "TYPE F F F\n"
           << "COUNT 1 1 1\n"
           << "WIDTH " << points.size() << "\n"
           << "HEIGHT 1\n"
           << "VIEWPOINT 0 0 0 1 0 0 0\n"
           << "POINTS " << points.size() << "\n"
           << "DATA binary\n";
    const std::string text = header.str();

    std::vector<unsigned char> bytes;
    bytes.reserve(text.size() + points.size() * 12);
    bytes.insert(bytes.end(), text.begin(), text.end());
    for (const Point& point : points) {
        append_f32_le(bytes, point.x);
        append_f32_le(bytes, point.y);
        append_f32_le(bytes, point.z);
    }
    return bytes;
}

namespace {

constexpr int k_preview_size = 480;
constexpr int k_preview_margin = 24;

uint32_t png_crc(const unsigned char* data, size_t length) {
    static const uint32_t table[256] = {
        0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu, 0xe963a535u,
        0x9e6495a3u, 0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u, 0x09b64c2bu, 0x7eb17cbdu,
        0xe7b82d07u, 0x90bf1d91u, 0x1db71064u, 0x6ab020f2u, 0xf3b97148u, 0x84be41deu, 0x1adad47du,
        0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u, 0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu,
        0x14015c4fu, 0x63066cd9u, 0xfa0f3d63u, 0x8d080df5u, 0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u,
        0xa2677172u, 0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu, 0x35b5a8fau, 0x42b2986cu,
        0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u, 0x26d930acu,
        0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u, 0xcfba9599u, 0xb8bda50fu,
        0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u, 0x2f6f7c87u, 0x58684c11u, 0xc1611dabu,
        0xb6662d3du, 0x76dc4190u, 0x01db7106u, 0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu,
        0x9fbfe4a5u, 0xe8b8d433u, 0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu,
        0x086d3d2du, 0x91646c97u, 0xe6635c01u, 0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu,
        0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u, 0x65b0d9c6u, 0x12b7e950u, 0x8bbeb8eau,
        0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u, 0x4db26158u, 0x3ab551ceu,
        0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u, 0xa4d1c46du, 0xd3d6f4fbu, 0x4369e96au,
        0x346ed9fcu, 0xad678846u, 0xda60b8d0u, 0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u,
        0x5005713cu, 0x270241aau, 0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u,
        0xce61e49fu, 0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u,
        0xb7bd5c3bu, 0xc0ba6cadu, 0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au, 0xead54739u,
        0x9dd277afu, 0x04db2615u, 0x73dc1683u, 0xe3630b12u, 0x94643b84u, 0x0d6d6a3eu, 0x7a6a5aa8u,
        0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u, 0xf00f9344u, 0x8708a3d2u, 0x1e01f268u,
        0x6906c2feu, 0xf762575du, 0x806567cbu, 0x196c3671u, 0x6e6b06e7u, 0xfed41b76u, 0x89d32be0u,
        0x10da7a5au, 0x67dd4accu, 0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u, 0xd6d6a3e8u,
        0xa1d1937eu, 0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
        0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u, 0x316e8eefu,
        0x4669be79u, 0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u, 0xcc0c7795u, 0xbb0b4703u,
        0x220216b9u, 0x5505262fu, 0xc5ba3bbeu, 0xb2bd0b28u, 0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u,
        0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du, 0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au,
        0x9c0906a9u, 0xeb0e363fu, 0x72076785u, 0x05005713u, 0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu,
        0x0cb61b38u, 0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u, 0x86d3d2d4u, 0xf1d4e242u,
        0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u, 0x88085ae6u,
        0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u, 0x616bffd3u, 0x166ccf45u,
        0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u, 0xa7672661u, 0xd06016f7u, 0x4969474du,
        0x3e6e77dbu, 0xaed16a4au, 0xd9d65adcu, 0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u,
        0x47b2cf7fu, 0x30b5ffe9u, 0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u,
        0xcdd70693u, 0x54de5729u, 0x23d967bfu, 0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u,
        0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du};
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

void append_be32(std::vector<unsigned char>& out, uint32_t value) {
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xffu));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xffu));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xffu));
    out.push_back(static_cast<unsigned char>(value & 0xffu));
}

void append_chunk(std::vector<unsigned char>& out, const char* type, const std::vector<unsigned char>& data) {
    append_be32(out, static_cast<uint32_t>(data.size()));
    const size_t type_at = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t crc = png_crc(out.data() + type_at, 4 + data.size());
    append_be32(out, crc);
}

void put_pixel(std::vector<unsigned char>& rgb, int size, int x, int y, unsigned char r, unsigned char g,
               unsigned char b) {
    if (x < 0 || y < 0 || x >= size || y >= size) {
        return;
    }
    const size_t at = (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 3;
    rgb[at] = r;
    rgb[at + 1] = g;
    rgb[at + 2] = b;
}

std::vector<unsigned char> encode_png(const std::vector<unsigned char>& rgb, int size) {
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<size_t>(size) * (1 + static_cast<size_t>(size) * 3));
    for (int y = 0; y < size; ++y) {
        raw.push_back(0);
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(size) * 3;
        raw.insert(raw.end(), rgb.begin() + static_cast<std::ptrdiff_t>(row),
                   rgb.begin() + static_cast<std::ptrdiff_t>(row + static_cast<size_t>(size) * 3));
    }

    uint32_t adler_a = 1;
    uint32_t adler_b = 0;
    for (unsigned char byte : raw) {
        adler_a = (adler_a + byte) % 65521u;
        adler_b = (adler_b + adler_a) % 65521u;
    }

    std::vector<unsigned char> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t chunk = std::min<size_t>(65535, raw.size() - offset);
        const bool last = offset + chunk == raw.size();
        zlib.push_back(last ? 0x01 : 0x00);
        const auto length = static_cast<uint16_t>(chunk);
        zlib.push_back(static_cast<unsigned char>(length & 0xffu));
        zlib.push_back(static_cast<unsigned char>((length >> 8) & 0xffu));
        const uint16_t nlength = static_cast<uint16_t>(~length);
        zlib.push_back(static_cast<unsigned char>(nlength & 0xffu));
        zlib.push_back(static_cast<unsigned char>((nlength >> 8) & 0xffu));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    const uint32_t adler = (adler_b << 16) | adler_a;
    append_be32(zlib, adler);

    std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<unsigned char> ihdr(13);
    auto be = [](unsigned char* dest, uint32_t value) {
        dest[0] = static_cast<unsigned char>((value >> 24) & 0xffu);
        dest[1] = static_cast<unsigned char>((value >> 16) & 0xffu);
        dest[2] = static_cast<unsigned char>((value >> 8) & 0xffu);
        dest[3] = static_cast<unsigned char>(value & 0xffu);
    };
    be(ihdr.data(), static_cast<uint32_t>(size));
    be(ihdr.data() + 4, static_cast<uint32_t>(size));
    ihdr[8] = 8;
    ihdr[9] = 2;
    append_chunk(png, "IHDR", ihdr);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});
    return png;
}

}  // namespace

std::vector<unsigned char> render_preview_png(const std::vector<Point>& points, double max_range_m) {
    const int size = k_preview_size;
    std::vector<unsigned char> rgb(static_cast<size_t>(size) * static_cast<size_t>(size) * 3);
    for (size_t i = 0; i < rgb.size(); i += 3) {
        rgb[i] = 16;
        rgb[i + 1] = 18;
        rgb[i + 2] = 22;
    }

    const double center = static_cast<double>(size) / 2.0;
    const double scale = (center - k_preview_margin) / std::max(max_range_m, 1e-6);
    for (int i = k_preview_margin; i <= size - k_preview_margin; ++i) {
        put_pixel(rgb, size, static_cast<int>(center), i, 40, 44, 52);
        put_pixel(rgb, size, i, static_cast<int>(center), 40, 44, 52);
    }

    for (const Point& point : points) {
        const int px = static_cast<int>(std::lround(center + static_cast<double>(point.x) * scale));
        const int py = static_cast<int>(std::lround(center - static_cast<double>(point.y) * scale));
        const double range = std::hypot(point.x, point.y);
        const double closeness = 1.0 - std::min(std::max(range / std::max(max_range_m, 1e-6), 0.0), 1.0);
        const auto shade = static_cast<unsigned char>(80.0 + 175.0 * closeness);
        put_pixel(rgb, size, px, py, shade, shade, 210);
    }

    const int origin = static_cast<int>(center);
    for (int y = origin - 3; y <= origin + 3; ++y) {
        for (int x = origin - 3; x <= origin + 3; ++x) {
            if ((x - origin) * (x - origin) + (y - origin) * (y - origin) <= 9) {
                put_pixel(rgb, size, x, y, 220, 224, 230);
            }
        }
    }
    return encode_png(rgb, size);
}

}  // namespace litratech
