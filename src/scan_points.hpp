#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <ldcp/data_types.h>

namespace litratech {

// LDCP range samples are counts of 2 mm, matching LitraTech's ltme_node driver.
inline constexpr double k_range_meters_per_count = 0.002;

struct Point {
    float x = 0;
    float y = 0;
    float z = 0;
};

// Convert one revolution of scan blocks into a planar point cloud.
//
// Angles follow ltme_node: beam 0 is forward (+x), positive angles rotate
// toward +y, and a 270 degree device is centered on that forward axis.
// A raw range of 0 is a no-return and is omitted. invert_frame mirrors the
// scan across the forward axis.
std::vector<Point> points_from_blocks(const std::vector<ldcp_sdk::ScanBlock>& blocks,
                                      double range_min_m,
                                      double range_max_m,
                                      bool invert_frame);

// Binary PCD (x y z, little-endian) accepted by viam-server's point cloud reader.
std::vector<unsigned char> encode_pcd(const std::vector<Point>& points);

}  // namespace litratech
