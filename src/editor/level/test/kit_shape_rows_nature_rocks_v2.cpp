#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_nature_rocks_v2.cpp.
namespace editor::kit_test {

auto get_nature_rocks_v2_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {};
    rows.recreated_originals = {"nature.rock_spire"};
    rows.seeded_shapes       = {"nature.rock_spire.v2"};
    rows.enum_shapes         = {"nature.rock_spire.v2"};
    rows.components          = {{"nature.rock_spire.v2", Kit_component_rule::more_than, 2}};
    rows.walkability_open    = {};
    rows.triangle_counts     = {{"nature.rock_spire.v2", 396}};
    rows.worst_cases         = {
        {"nature.rock_spire.v2", R"({"form":"hoodoo","height":"0x1.ep+5","radius":"0x1p+5","taper":"0x1p+0","lean":"0x1.ep+3","roughness":"0x1p+0","strata":16,"seed":0,"segments":20})"},
        {"nature.rock_spire.v2", R"({"form":"pinnacle","height":"0x1.ep+5","radius":"0x1.99999ap-3","taper":"0x1p+0","lean":"0x1.ep+3","roughness":"0x1p+0","strata":16,"seed":1,"segments":20})"}
    };
    rows.pins                = {
        {"nature.rock_spire.v2 1", 206, 396, 1188, 0x27e6ad4a60447496ull, {-4.26199f, 0.00000f, -4.04011f}, {3.58619f, 15.93060f, 3.67141f}, {0.13269f, 5.92629f, -0.39138f}, 213.05231f},
        {"nature.rock_spire.v2 3", 218, 416, 1248, 0x4eb0477cb4a55012ull, {-3.56880f, 0.00000f, -3.75071f}, {4.10082f, 5.01379f, 2.55549f}, {0.17434f, 1.56452f, -0.38708f}, 93.71077f},
        {"nature.rock_spire.v2 17", 218, 416, 1248, 0x5c842db5842d76baull, {-4.08935f, 0.00000f, -3.61736f}, {4.02963f, 17.45032f, 3.06503f}, {0.18783f, 6.10117f, -0.05149f}, 222.88402f}
    };
    return rows;
}

} // namespace editor::kit_test
