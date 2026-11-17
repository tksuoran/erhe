#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_build_13_16.cpp.
namespace editor::kit_test {

auto get_build_13_16_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {
        "architecture.window_wall", "architecture.pediment", "architecture.stepped_platform", "nature.succulent",
        "nature.grass_tuft", "nature.boulder_cluster", "nature.stepping_stone"
    };
    rows.recreated_originals = {};
    rows.seeded_shapes       = {"nature.succulent", "nature.grass_tuft", "nature.boulder_cluster", "nature.stepping_stone"};
    rows.enum_shapes         = {"architecture.window_wall", "nature.succulent", "nature.grass_tuft", "nature.boulder_cluster"};
    // The window is its wall, its casing and their bars; the pediment its body and four dentils for each of the count;
    // the plants a stem, a bud or a tussock and one part per leaf, blade or stalk; the heap one part per boulder.
    rows.components          = {
        {"architecture.window_wall", Kit_component_rule::exactly, 4}, {"architecture.pediment", Kit_component_rule::exactly, 37},
        {"nature.succulent", Kit_component_rule::more_than, 8}, {"nature.grass_tuft", Kit_component_rule::more_than, 8},
        {"nature.boulder_cluster", Kit_component_rule::more_than, 1}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"architecture.window_wall", 152}, {"architecture.pediment", 520}, {"architecture.stepped_platform", 76},
        {"nature.succulent", 582}, {"nature.grass_tuft", 614}, {"nature.boulder_cluster", 332}, {"nature.stepping_stone", 108}
    };
    // R2.6 where the triangle maxima lie: a curved head with every bar, the most dentils, a yucca's two swords a leaf,
    // a reed's canes, a heap of worn boulders.
    rows.worst_cases         = {
        {"architecture.window_wall", R"({"length":"0x1p+1","height":"0x1.4p+1","thickness":"0x1.47ae14p-6","opening_width":"0x1.333334p-2","opening_height":"0x1.333334p-2","sill_height":"0x1.99999ap-4","head":"round","mullions":4,"transoms":3,"seed":0,"segments":32})"},
        {"architecture.pediment",    R"({"width":"0x1p+0","pitch":"0x1p+3","depth":"0x1.99999ap-4","recess":"0x1.47ae14p-7","dentils":16,"seed":0})"},
        {"nature.succulent",         R"({"form":"yucca","size":"0x1.99999ap-3","leaves":32,"spread":"0x1.4p+3","seed":0})"},
        {"nature.grass_tuft",        R"({"form":"reed","height":"0x1.99999ap-4","blades":40,"spread":"0x0p+0","seed":0})"},
        {"nature.boulder_cluster",   R"({"radius":"0x1.333334p-2","count":10,"size_spread":"0x0p+0","form":"worn","roughness":"0x1p+0","seed":0})"}
    };
    rows.pins                = {
        {"architecture.window_wall 1", 80, 80, 312, 0x4e11f4f03a037d8dull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {0.00000f, 1.48750f, 0.00000f}, 27.55610f},
        {"architecture.pediment 1", 334, 267, 1054, 0x07008ceb1a0d9a65ull, {-2.00000f, 0.00000f, -0.25000f}, {2.00000f, 0.73784f, 0.25000f}, {0.00000f, 0.35930f, 0.00000f}, 10.76334f},
        {"architecture.stepped_platform 1", 40, 38, 152, 0xce83dfebe9ba3f91ull, {-2.00000f, 0.00000f, -2.00000f}, {2.00000f, 0.60000f, 2.00000f}, {0.00000f, 0.35280f, 0.00000f}, 39.58353f},
        {"nature.succulent 1", 321, 582, 1746, 0x73881889a69c1730ull, {-0.46354f, 0.00000f, -0.37884f}, {0.46943f, 0.43781f, 0.43068f}, {0.00641f, 0.17872f, -0.00296f}, 1.09792f},
        {"nature.succulent 3", 387, 702, 2106, 0xc05224cae98e30a0ull, {-0.93015f, 0.00000f, -0.96103f}, {0.89593f, 0.85418f, 0.73441f}, {0.00498f, 0.36767f, -0.00715f}, 5.62302f},
        {"nature.succulent 17", 277, 502, 1506, 0x5e0e3f697731cee0ull, {-0.76053f, 0.00000f, -0.68864f}, {0.88508f, 0.88650f, 0.94307f}, {0.00024f, 0.35138f, 0.01484f}, 3.83349f},
        {"nature.grass_tuft 1", 347, 614, 1842, 0x0d74d923c47e05b9ull, {-0.39499f, 0.00000f, -0.36130f}, {0.38270f, 0.75882f, 0.47155f}, {-0.00247f, 0.24695f, 0.01107f}, 0.50572f},
        {"nature.grass_tuft 3", 228, 404, 1212, 0x0d942be21e789e46ull, {-0.12383f, 0.00000f, -0.12905f}, {0.12792f, 0.23268f, 0.13643f}, {0.00017f, 0.08468f, 0.00171f}, 0.03663f},
        {"nature.grass_tuft 17", 330, 584, 1752, 0x0e4f50e61e5d8887ull, {-0.65233f, 0.00000f, -0.52812f}, {0.64641f, 1.13693f, 0.62208f}, {-0.00744f, 0.38105f, -0.00174f}, 1.14948f},
        {"nature.boulder_cluster 1", 172, 196, 724, 0xc93bcc9eed448e71ull, {-2.24904f, 0.00000f, -3.12880f}, {2.03375f, 2.87935f, 3.36256f}, {-0.19597f, 1.08200f, 0.32455f}, 78.53170f},
        {"nature.boulder_cluster 3", 192, 222, 816, 0x08b14577aeb9b52bull, {-1.24942f, 0.00000f, -1.89417f}, {0.86537f, 1.24172f, 1.31260f}, {-0.19762f, 0.49903f, 0.09721f}, 18.07667f},
        {"nature.boulder_cluster 17", 271, 291, 1104, 0xf6741c7d32343d11ull, {-1.53566f, 0.00000f, -1.58751f}, {0.98240f, 1.56328f, 1.60100f}, {-0.20310f, 0.61629f, -0.03093f}, 26.28941f},
        {"nature.stepping_stone 1", 56, 44, 196, 0xcdbaa5962fed7419ull, {-0.68103f, 0.00000f, -0.58946f}, {0.75393f, 0.25000f, 0.56108f}, {0.05428f, 0.14025f, -0.02525f}, 3.08143f},
        {"nature.stepping_stone 3", 56, 44, 196, 0xcdbaa5962fed7419ull, {-0.73616f, 0.00000f, -0.61707f}, {0.71722f, 0.25000f, 0.63653f}, {-0.02727f, 0.14181f, -0.01526f}, 3.61776f},
        {"nature.stepping_stone 17", 56, 44, 196, 0xcdbaa5962fed7419ull, {-0.33119f, 0.00000f, -0.37200f}, {0.31610f, 0.25000f, 0.35947f}, {-0.00101f, 0.13984f, 0.00891f}, 1.15601f}
    };
    return rows;
}

} // namespace editor::kit_test
