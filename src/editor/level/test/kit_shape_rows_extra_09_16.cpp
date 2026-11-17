#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_extra_09_16.cpp.
namespace editor::kit_test {

auto get_extra_09_16_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {
        "harbour.quay_wall", "industrial.pipe", "prop.pot", "prop.table", "prop.market_stall", "prop.cart", "prop.well", "prop.signpost"
    };
    rows.recreated_originals = {};
    rows.seeded_shapes       = {"prop.signpost"};
    rows.enum_shapes         = {"industrial.pipe", "prop.pot", "prop.table", "prop.market_stall", "prop.cart", "prop.well"};
    rows.components          = {
        {"harbour.quay_wall", Kit_component_rule::exactly, 6}, {"prop.table", Kit_component_rule::exactly, 9},
        {"prop.market_stall", Kit_component_rule::exactly, 12}, {"prop.cart", Kit_component_rule::exactly, 14},
        {"prop.well", Kit_component_rule::exactly, 8}, {"prop.signpost", Kit_component_rule::exactly, 5}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"harbour.quay_wall", 72}, {"industrial.pipe", 196}, {"prop.pot", 236}, {"prop.table", 108},
        {"prop.market_stall", 144}, {"prop.cart", 520}, {"prop.well", 392}, {"prop.signpost", 74}
    };
    // R2.6 where the triangle maxima lie: four fenders, a flanged tee of 16 points, an amphora of 20 points, a bench
    // on posts, a peaked awning, rails on a cart with wheels of 16 points, a gable over a well of 20 points, four arms.
    rows.worst_cases         = {
        {"harbour.quay_wall", R"({"length":"0x1p+0","height":"0x1p-1","thickness":"0x1p+0","batter":"0x0p+0","fenders":4,"seed":0})"},
        {"industrial.pipe",   R"({"form":"tee","diameter":"0x1.99999ap-4","length":"0x1p+0","bend_angle":"0x1.ep+3","flanges":"ends","seed":0,"segments":16})"},
        {"prop.pot",          R"({"form":"amphora","height":"0x1.333334p-3","width":"0x1.333334p-3","neck":"0x1.333334p-2","seed":0,"segments":20})"},
        {"prop.table",        R"({"form":"bench","width":"0x1.333334p-2","depth":"0x1.333334p-2","height":"0x1.333334p-2","legs":"posts","seed":0})"},
        {"prop.market_stall", R"({"width":"0x1.333334p+0","depth":"0x1.333334p-1","height":"0x1.ccccccp+0","awning":"peaked","overhang":"0x0p+0","seed":0})"},
        {"prop.cart",         R"({"length":"0x1.99999ap-1","width":"0x1p-1","wheel_radius":"0x1.99999ap-3","tilt":"0x0p+0","sides":"rails","seed":0,"segments":16})"},
        {"prop.well",         R"({"radius":"0x1.99999ap-2","wall_height":"0x1.99999ap-2","roof":"gable","seed":0,"segments":20})"},
        {"prop.signpost",     R"({"height":"0x1p+0","arms":4,"arm_length":"0x1.333334p-2","lean":"0x0p+0","seed":0})"}
    };
    rows.pins                = {
        {"harbour.quay_wall 1", 48, 36, 144, 0x961f5f49a3ddc005ull, {-1.00000f, 0.00000f, -0.50000f}, {1.00000f, 3.09600f, 0.87672f}, {0.00000f, 1.75592f, 0.42810f}, 34.98780f},
        {"industrial.pipe 1", 100, 92, 380, 0xcb65e93be76ff389ull, {-1.00000f, 0.00000f, -0.27000f}, {1.00000f, 0.51357f, 0.27000f}, {0.00000f, 0.25679f, 0.00000f}, 3.48461f},
        {"prop.pot 1", 120, 110, 456, 0x0c5affbc69c8117bull, {-0.35000f, 0.00000f, -0.35000f}, {0.35000f, 1.20000f, 0.35000f}, {0.00000f, 0.78120f, 0.00000f}, 2.95974f},
        {"prop.table 1", 72, 54, 216, 0x0d855de98e06d1c3ull, {-0.70000f, 0.00000f, -0.40000f}, {0.70000f, 0.75000f, 0.40000f}, {0.00000f, 0.53375f, 0.00000f}, 4.35298f},
        {"prop.market_stall 1", 96, 72, 288, 0xb51bf3a44c0bcdf5ull, {-1.00000f, 0.00000f, -0.70000f}, {1.00000f, 2.40000f, 0.70000f}, {0.00000f, 1.21045f, 0.16283f}, 18.29421f},
        {"prop.cart 1", 288, 244, 1008, 0x73238410b9498395ull, {-0.50000f, 0.00000f, -0.43663f}, {0.50000f, 0.95226f, 1.03333f}, {0.00000f, 0.42700f, 0.05114f}, 5.40033f},
        {"prop.well 1", 212, 180, 752, 0x5e2547712c74b785ull, {-0.95400f, 0.00000f, -0.78600f}, {1.10100f, 1.95000f, 0.78600f}, {0.11668f, 1.04033f, 0.11698f}, 14.01414f},
        {"prop.signpost 1", 47, 36, 146, 0xcde545537e3f8f91ull, {-0.15797f, 0.00000f, -0.18666f}, {0.11626f, 2.40000f, 0.43360f}, {-0.01176f, 1.48226f, 0.07517f}, 1.82796f},
        {"prop.signpost 3", 57, 43, 176, 0xeda001bfdefa22eeull, {-0.23079f, 0.00000f, -0.46445f}, {0.63097f, 2.40000f, 0.37198f}, {0.09171f, 1.48549f, -0.08418f}, 2.05104f},
        {"prop.signpost 17", 47, 36, 146, 0xcde545537e3f8f91ull, {-0.11616f, 0.00000f, -0.35986f}, {0.44689f, 2.40000f, 0.48064f}, {0.15763f, 1.48430f, 0.08342f}, 1.83035f}
    };
    return rows;
}

} // namespace editor::kit_test
