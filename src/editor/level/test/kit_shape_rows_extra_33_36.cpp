#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_extra_33_36.cpp.
namespace editor::kit_test {

auto get_extra_33_36_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {"light.garland", "traversal.rope_bridge", "ruin.cracked_floor", "harbour.boat"};
    rows.seeded_shapes       = {"traversal.rope_bridge", "ruin.cracked_floor"};
    rows.enum_shapes         = {"light.garland", "harbour.boat"};
    rows.components          = {
        {"light.garland", Kit_component_rule::exactly, 21}, {"traversal.rope_bridge", Kit_component_rule::exactly,
        46}, {"ruin.cracked_floor", Kit_component_rule::exactly, 9}, {"harbour.boat", Kit_component_rule::exactly, 7}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"light.garland", 492}, {"traversal.rope_bridge", 1000}, {"ruin.cracked_floor", 108}, {"harbour.boat", 488}
    };
    // R2.6 where the triangle maxima lie: 24 paper lanterns, the longest bridge of the narrowest planks, 24
    // fragments, a rowboat with four thwarts.
    rows.worst_cases         = {
        {"light.garland",         R"({"span":"0x1p+0","sag":"0x1.99999ap-5","count":24,"item":"paper_lantern","seed":0})"},
        {"traversal.rope_bridge", R"({"span":"0x1p+4","width":"0x1p+0","sag":"0x1.eb851ep-6","plank_gap":"0x1.47ae14p-7","missing":"0x0p+0","rail_height":"0x1.333334p-1","seed":0})"},
        {"ruin.cracked_floor",    R"({"width":"0x1.99999ap-4","depth":"0x1.99999ap-4","thickness":"0x1.47ae14p-7","fragments":24,"tilt":"0x0p+0","missing":"0x0p+0","seed":0})"},
        {"harbour.boat",          R"({"form":"rowboat","length":"0x1p+1","beam":"0x1.99999ap-1","sheer":"0x0p+0","thwarts":4,"heel":"0x0p+0","seed":0})"}
    };
    rows.pins                = {
        {"light.garland 1", 288, 262, 1016, 0x73b55956f7a792a3ull, {-2.00000f, 0.00000f, -0.04330f}, {2.00000f, 0.67558f, 0.04330f}, {0.00000f, 0.26506f, 0.00011f}, 0.78539f},
        {"traversal.rope_bridge 1", 592, 500, 2000, 0xb09577184d94a546ull, {-3.00000f, 0.00000f, -0.61000f}, {3.00000f, 1.65787f, 0.61000f}, {-0.00841f, 0.52011f, 0.00069f}, 18.99963f},
        {"traversal.rope_bridge 3", 592, 500, 2000, 0xb09577184d94a546ull, {-3.00000f, 0.00000f, -0.61000f}, {3.00000f, 1.47867f, 0.61000f}, {-0.01871f, 0.44992f, -0.00156f}, 18.87509f},
        {"traversal.rope_bridge 17", 592, 500, 2000, 0xb09577184d94a546ull, {-3.00000f, 0.00000f, -0.61000f}, {3.00000f, 1.77733f, 0.61000f}, {-0.01613f, 0.56229f, -0.00026f}, 19.10742f},
        {"ruin.cracked_floor 1", 72, 54, 216, 0x0d855de98e06d1c3ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 0.19589f, 1.00000f}, {-0.01658f, 0.07772f, 0.04068f}, 11.38881f},
        {"ruin.cracked_floor 3", 72, 54, 216, 0x0d855de98e06d1c3ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 0.19717f, 1.00000f}, {-0.01845f, 0.07312f, 0.00215f}, 11.18603f},
        {"ruin.cracked_floor 17", 72, 54, 216, 0x0d855de98e06d1c3ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 0.19897f, 1.00000f}, {0.02424f, 0.05938f, 0.02474f}, 10.54510f},
        {"harbour.boat 1", 258, 422, 1332, 0x66312db82813a488ull, {-4.00000f, 0.00000f, -0.58800f}, {4.00000f, 1.00762f, 0.83815f}, {-0.12338f, 0.41633f, 0.10344f}, 34.34060f}
    };
    return rows;
}

} // namespace editor::kit_test
