#include "kit_shape_test_support.hpp"

// The recreated architecture shapes (kit_shapes_architecture_v2.cpp).
namespace editor::kit_test {

auto get_architecture_v2_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.recreated_originals = {"architecture.door_wall", "architecture.pillar", "architecture.arch"};
    rows.triangle_counts = {
        {"architecture.door_wall.v2", 572}, {"architecture.pillar.v2", 100}, {"architecture.arch.v2", 516}
    };
    rows.pins = {
        {"architecture.door_wall.v2 1", 288, 314, 1200, 0x3c3e06d85ee8c4c1ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {0.00000f, 2.18111f, 0.00000f}, 23.27834f},
        {"architecture.pillar.v2 1", 52, 50, 200, 0x8d700e0ae6ddc407ull, {-0.25000f, 0.00000f, -0.25000f}, {0.25000f, 3.00000f, 0.25000f}, {0.00000f, 1.81240f, 0.00000f}, 5.52820f},
        {"architecture.arch.v2 1", 260, 282, 1080, 0x1ba910e3898af7dfull, {-1.30000f, 0.00000f, -0.25000f}, {1.30000f, 2.70000f, 0.25000f}, {0.00000f, 1.63505f, 0.00000f}, 9.22280f}
    };
    return rows;
}

} // namespace editor::kit_test
