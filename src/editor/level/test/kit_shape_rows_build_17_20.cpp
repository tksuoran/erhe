#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_build_17_20.cpp.
namespace editor::kit_test {

auto get_build_17_20_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {
        "architecture.timber_bay", "architecture.railing", "nature.bamboo", "architecture.stairs", "architecture.spiral_stairs",
        "nature.rock_arch"
    };
    rows.seeded_shapes       = {"nature.bamboo", "nature.rock_arch"};
    rows.enum_shapes         = {"architecture.timber_bay", "architecture.railing", "architecture.stairs", "architecture.spiral_stairs"};
    rows.components          = {
        {"architecture.timber_bay", Kit_component_rule::exactly, 35}, {"architecture.railing", Kit_component_rule::exactly, 18},
        {"nature.bamboo", Kit_component_rule::more_than, 6}, {"architecture.stairs", Kit_component_rule::exactly, 30},
        {"architecture.spiral_stairs", Kit_component_rule::exactly, 18}, {"nature.rock_arch", Kit_component_rule::exactly, 5}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"architecture.timber_bay", 420}, {"architecture.railing", 782}, {"nature.bamboo", 1152},
        {"architecture.stairs", 520}, {"architecture.spiral_stairs", 348}, {"nature.rock_arch", 396}
    };
    // R2.6 where the triangle maxima lie: every screen and lattice bar, the most balusters, the most canes, the most
    // steps of the two stairs.
    rows.worst_cases         = {
        {"architecture.timber_bay",    R"({"length":"0x1p+1","height":"0x1.8p+1","post_width":"0x1.333334p-3","infill":"shoji","rails":3,"panels":4,"seed":0})"},
        {"architecture.railing",       R"({"length":"0x1.8p+2","height":"0x1p+0","bays":6,"post_width":"0x1.eb851ep-4","style":"balustrade","infill":8,"seed":0})"},
        {"nature.bamboo",              R"({"height":"0x1p+4","culms":16,"radius":"0x1.99999ap-4","lean":"0x1.4p+4","cane_width":"0x1.eb851ep-6","seed":0})"},
        {"architecture.stairs",        R"({"form":"u_landing","rise":"0x1.8p+3","width":"0x1.333334p+0","step_rise_limit":"0x1.851eb8p-3","wall_height":"0x1.ccccccp-1","seed":0})"},
        {"architecture.spiral_stairs", R"({"rise":"0x1.8p+3","radius":"0x1.4p+0","step_rise_limit":"0x1p-3","turn":"right","newel_width":"0x1.333334p-2","seed":0})"}
    };
    rows.pins                = {
        {"architecture.timber_bay 1", 280, 210, 840, 0x2527d685a4a18297ull, {-1.00000f, 0.00000f, -0.09300f}, {1.00000f, 3.00000f, 0.09300f}, {-0.00000f, 1.14917f, 0.00000f}, 21.20987f},
        {"architecture.railing 1", 427, 377, 1536, 0xb4a9e74a284195c7ull, {-1.00000f, 0.00000f, -0.07680f}, {1.00000f, 1.00000f, 0.07680f}, {0.00000f, 0.47866f, -0.00000f}, 4.63449f},
        {"nature.bamboo 1", 792, 864, 2880, 0x6c0846efe7b4e08eull, {-1.07780f, 0.00000f, -1.59044f}, {1.64358f, 5.71778f, 1.67784f}, {0.14284f, 3.34324f, -0.08999f}, 10.07956f},
        {"nature.bamboo 3", 1392, 1520, 4992, 0x9ff489d39ddd160full, {-2.10097f, 0.00000f, -2.09655f}, {2.02643f, 12.17107f, 1.92451f}, {0.00059f, 6.39200f, 0.03164f}, 61.69020f},
        {"nature.bamboo 17", 970, 1040, 3500, 0xbf62c1bfb2587794ull, {-2.02382f, 0.00000f, -2.29942f}, {2.20351f, 12.09682f, 2.04032f}, {0.04704f, 6.16937f, 0.08953f}, 38.93713f},
        {"architecture.stairs 1", 320, 290, 1100, 0x6120e81d73bd1e57ull, {-0.84840f, 0.00000f, -2.27160f}, {0.84840f, 4.04400f, 2.40120f}, {0.00000f, 1.76082f, -0.12899f}, 126.71120f},
        {"architecture.spiral_stairs 1", 210, 141, 630, 0xd1d33fe01b1bb4c4ull, {-1.24829f, 0.00000f, -1.24829f}, {1.24829f, 3.00000f, 1.24829f}, {0.06965f, 1.47524f, -0.06892f}, 30.91816f},
        {"nature.rock_arch 1", 208, 396, 1188, 0xc1bcafcc0c9b500eull, {-3.39246f, 0.00000f, -1.46732f}, {3.54312f, 4.08350f, 1.73708f}, {0.05686f, 1.69742f, -0.05245f}, 58.55318f},
        {"nature.rock_arch 3", 208, 396, 1188, 0xc1bcafcc0c9b500eull, {-4.10132f, 0.00000f, -2.25118f}, {5.47049f, 5.37363f, 2.48560f}, {0.28696f, 1.96508f, -0.01370f}, 119.62263f},
        {"nature.rock_arch 17", 208, 396, 1188, 0xc1bcafcc0c9b500eull, {-4.24317f, 0.00000f, -2.59604f}, {4.82553f, 5.17238f, 1.74554f}, {0.16918f, 1.95445f, -0.09999f}, 103.39135f}
    };
    return rows;
}

} // namespace editor::kit_test
