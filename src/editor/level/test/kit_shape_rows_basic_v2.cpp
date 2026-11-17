#include "kit_shape_test_support.hpp"

// The recreated basic kit (kit_shapes_basic_v2.cpp).
namespace editor::kit_test {

auto get_basic_v2_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.recreated_originals = {"basic.block", "basic.wedge", "basic.cylinder", "basic.stairs", "basic.floor_slab"};
    rows.triangle_counts = {
        {"basic.block.v2", 12}, {"basic.wedge.v2", 8}, {"basic.cylinder.v2", 60}, {"basic.stairs.v2", 180}, {"basic.floor_slab.v2", 12}
    };
    rows.pins = {
        {"basic.block.v2 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 6.00000f},
        {"basic.wedge.v2 1", 6, 5, 18, 0x2d401a55eec16520ull, {-0.50000f, 0.00000f, -1.00000f}, {0.50000f, 1.00000f, 1.00000f}, {0.00000f, 0.33333f, -0.33333f}, 7.23607f},
        {"basic.cylinder.v2 1", 32, 18, 96, 0x8cc58f15ad955627ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 4.65218f},
        {"basic.stairs.v2 1", 92, 104, 388, 0x4c5057cd16338a9dull, {-0.50000f, 0.00000f, -2.25000f}, {0.50000f, 3.00000f, 2.25000f}, {0.00000f, 1.04348f, 0.00000f}, 29.40000f},
        {"basic.floor_slab.v2 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 0.20000f, 1.00000f}, {0.00000f, 0.10000f, 0.00000f}, 9.60000f}
    };
    return rows;
}

} // namespace editor::kit_test
