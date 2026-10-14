#include "scan_points.hpp"

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
            points.push_back(Point{static_cast<float>(range_m * std::cos(angle)),
                                   static_cast<float>(range_m * std::sin(angle)),
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

}  // namespace litratech
