#include "scan_points.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
    std::cerr << message << "\n";
    return EXIT_FAILURE;
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 1e-5f;
}

ldcp_sdk::ScanBlock make_block(int index, int count, int length, const std::vector<int>& ranges) {
    ldcp_sdk::ScanBlock block;
    block.block_index = index;
    block.block_count = count;
    block.block_length = length;
    block.angular_fov = ldcp_sdk::ANGULAR_FOV_360DEG;
    block.layers.resize(1);
    block.layers[0].ranges = ranges;
    return block;
}

}  // namespace

int main() {
    // Four beams around a full turn. Raw range 500 is 1.00 m.
    const std::vector<ldcp_sdk::ScanBlock> blocks = {
        make_block(0, 2, 2, {500, 500}),
        make_block(1, 2, 2, {0, 500}),
    };

    const std::vector<litratech::Point> points =
        litratech::points_from_blocks(blocks, 0.05, 30.0, false);
    if (points.size() != 3) {
        return fail("expected 3 returns, with the zero range omitted");
    }
    // beam indexes -2, -1, and +1 → angles -pi, -pi/2, +pi/2.
    if (!near(points[0].x, -1.f) || !near(points[0].y, 0.f)) {
        return fail("beam behind the sensor was not placed on -x");
    }
    if (!near(points[1].x, 0.f) || !near(points[1].y, -1.f)) {
        return fail("beam to the right was not placed on -y");
    }
    if (!near(points[2].x, 0.f) || !near(points[2].y, 1.f)) {
        return fail("beam to the left was not placed on +y");
    }

    const std::vector<litratech::Point> mirrored =
        litratech::points_from_blocks(blocks, 0.05, 30.0, true);
    if (!near(mirrored[1].y, 1.f) || !near(mirrored[2].y, -1.f)) {
        return fail("invert_frame did not mirror y");
    }

    const std::vector<litratech::Point> clipped =
        litratech::points_from_blocks(blocks, 0.05, 0.5, false);
    if (!clipped.empty()) {
        return fail("ranges beyond range_max were kept");
    }

    const std::vector<unsigned char> pcd = litratech::encode_pcd(points);
    const std::string prefix = "VERSION .7\n";
    const std::string header(pcd.begin(), pcd.begin() + static_cast<std::ptrdiff_t>(prefix.size()));
    if (header != prefix) {
        return fail("PCD header did not start with VERSION .7");
    }
    if (pcd.size() != std::string("VERSION .7\n"
                                  "FIELDS x y z\n"
                                  "SIZE 4 4 4\n"
                                  "TYPE F F F\n"
                                  "COUNT 1 1 1\n"
                                  "WIDTH 3\n"
                                  "HEIGHT 1\n"
                                  "VIEWPOINT 0 0 0 1 0 0 0\n"
                                  "POINTS 3\n"
                                  "DATA binary\n")
                             .size() +
                           3 * 12) {
        return fail("PCD payload size did not match three xyz points");
    }

    return EXIT_SUCCESS;
}
