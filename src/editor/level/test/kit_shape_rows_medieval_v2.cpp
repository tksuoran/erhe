#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_medieval_v2.cpp.
namespace editor::kit_test {

auto get_medieval_v2_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {};
    rows.recreated_originals = {"architecture.battlement", "architecture.tower"};
    rows.seeded_shapes       = {};
    rows.enum_shapes         = {"architecture.battlement.v2", "architecture.tower.v2"};
    // Closed parts that overlap: breastwork, sill band, merlons and copings; wall, base, courses, surround, parapet and corbels.
    rows.components          = {
        {"architecture.battlement.v2", Kit_component_rule::more_than, 3}, {"architecture.tower.v2", Kit_component_rule::more_than, 10}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {{"architecture.battlement.v2", 112}, {"architecture.tower.v2", 1524}};
    // A run of the most bays with sloped merlons; a wide square tower with two rows of slits (a search over 20000 value sets).
    rows.worst_cases         = {
        {"architecture.battlement.v2",
         R"({"length":"0x1.5ab8b4p+4","height":"0x1.8p+1","thickness":"0x1.edc47p+1","merlon_height":"0x1.99999ap-3","merlon_width":"0x1.99999ap-2","crenel_width":"0x1.333334p-2","merlon":"swallowtail","seed":1})"},
        {"architecture.tower.v2",
         R"({"profile":"square","width":"0x1p+4","height":"0x1.ep+4","batter":"0x1.4p+3","wall":"0x1.9497dcp-2","opening_width":"0x1.99999ap-1","opening_height":"0x1.8dfeaap+1","top":"battlement","slit_rows":2,"seed":1})"}
    };
    rows.pins                = {
        {"architecture.battlement.v2 1", 62, 49, 210, 0xa75f512b36454678ull, {-1.00000f, 0.00000f, -0.16000f}, {1.00000f, 1.80000f, 0.16000f}, {0.00000f, 1.16052f, 0.00000f}, 10.21486f},
        {"architecture.tower.v2 1", 824, 786, 3096, 0x30bc2e39165734c9ull, {-1.79000f, 0.00000f, -1.76281f}, {1.79000f, 6.40000f, 1.76281f}, {0.00000f, 3.58014f, 0.04381f}, 205.64935f}
    };
    return rows;
}

} // namespace editor::kit_test
