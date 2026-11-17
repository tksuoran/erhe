#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_nature_plants_v2.cpp.
namespace editor::kit_test {

auto get_nature_plants_v2_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {};
    rows.recreated_originals = {"nature.leaf_plant", "nature.palm_shrub"};
    rows.seeded_shapes       = {"nature.leaf_plant.v2", "nature.palm_shrub.v2"};
    rows.enum_shapes         = {"nature.leaf_plant.v2", "nature.palm_shrub.v2"};
    rows.components          = {{"nature.leaf_plant.v2", Kit_component_rule::more_than, 8}};
    rows.walkability_open    = {};
    rows.triangle_counts     = {{"nature.leaf_plant.v2", 488}, {"nature.palm_shrub.v2", 1246}};
    rows.worst_cases         = {
        {"nature.leaf_plant.v2", R"({"form":"tree_fern","height":"0x1p+3","leaves":14,"droop":"0x1p+0","leaf_width":"0x1.333334p-1","seed":0})"},
        {"nature.leaf_plant.v2", R"({"form":"fern","height":"0x1.333334p-2","leaves":10,"droop":"0x1p+0","leaf_width":"0x1.99999ap-4","seed":1})"},
        {"nature.palm_shrub.v2", R"({"species":"cycad","size":"0x1.8p+2","fronds":16,"seed":2})"},
        {"nature.palm_shrub.v2", R"({"species":"cycad","size":"0x1.333334p-2","fronds":12,"seed":1})"}
    };
    rows.pins                = {
        {"nature.leaf_plant.v2 1", 264, 488, 1464, 0xe4e2b22c4c6890a2ull, {-0.64295f, 0.00000f, -0.59633f}, {0.44566f, 1.36983f, 0.74172f}, {0.00322f, 0.74075f, 0.02671f}, 2.77000f},
        {"nature.leaf_plant.v2 3", 414, 768, 2304, 0x58b4aca7963c868full, {-0.20958f, 0.00000f, -0.21502f}, {0.21721f, 0.45661f, 0.20932f}, {0.00161f, 0.25911f, 0.00638f}, 0.48745f},
        {"nature.leaf_plant.v2 17", 246, 456, 1368, 0x0e9aa3c7bbf9ba82ull, {-0.83343f, 0.00000f, -0.82759f}, {0.88511f, 2.10041f, 0.98743f}, {-0.02775f, 1.14201f, 0.04518f}, 6.25654f},
        {"nature.palm_shrub.v2 1", 679, 1246, 3738, 0x22ae77083aec8217ull, {-1.45501f, 0.00000f, -1.18102f}, {1.42372f, 1.49740f, 1.48621f}, {0.06650f, 0.63298f, -0.05399f}, 12.09671f},
        {"nature.palm_shrub.v2 3", 579, 1062, 3186, 0x184b10d393ff2cfeull, {-1.11559f, 0.00000f, -0.98068f}, {0.93700f, 1.05334f, 1.12683f}, {0.01303f, 0.49561f, -0.01773f}, 6.68229f},
        {"nature.palm_shrub.v2 17", 779, 1430, 4290, 0x1538f3f36ef66444ull, {-1.29283f, 0.00000f, -1.26377f}, {1.20507f, 1.27121f, 1.05130f}, {0.06285f, 0.58328f, -0.02918f}, 11.34427f}
    };
    return rows;
}

} // namespace editor::kit_test
