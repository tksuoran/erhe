#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_build_21_24.cpp.
namespace editor::kit_test {

auto get_build_21_24_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {"architecture.bridge_deck", "architecture.buttress", "nature.log", "nature.shell", "nature.blocky_tree", "architecture.stone_lantern"};
    rows.recreated_originals = {};
    rows.seeded_shapes       = {"nature.log", "nature.shell", "nature.blocky_tree"};
    rows.enum_shapes         = {"architecture.bridge_deck", "architecture.buttress", "nature.log", "nature.shell", "nature.blocky_tree", "architecture.stone_lantern"};
    rows.components          = {
        {"architecture.bridge_deck", Kit_component_rule::exactly, 42}, {"architecture.buttress", Kit_component_rule::exactly, 8},
        {"nature.log", Kit_component_rule::exactly, 3}, {"nature.blocky_tree", Kit_component_rule::exactly, 11},
        {"architecture.stone_lantern", Kit_component_rule::exactly, 27}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"architecture.bridge_deck", 728}, {"architecture.buttress", 104}, {"nature.log", 360}, {"nature.shell", 320}, {"nature.blocky_tree", 132},
        {"architecture.stone_lantern", 1864}
    };
    // R2.6 where the triangle maxima lie: four arched spans under railings and sixteen courses, four stages under a
    // pinnacle, four stubs on a trunk of fourteen points, a starfish of twelve arms, four cubes on branches, a
    // hexagonal lantern.
    rows.worst_cases         = {
        {"architecture.bridge_deck", R"({"length":"0x1p+5","width":"0x1p+3","thickness":"0x1p+0","height":"0x1p+4","supports":"arched","spans":4,"parapet":"open","parapet_height":"0x1.8p+0","courses":16,"seed":0})"},
        {"architecture.buttress",    R"({"height":"0x1p+4","depth":"0x1.8p+2","width":"0x1.8p+1","steps":4,"cap":"pinnacle","seed":0})"},
        {"nature.log",               R"({"form":"log","length":"0x1p+3","width":"0x1p+1","bend":"0x1p+0","stubs":4,"seed":0,"segments":14})"},
        {"nature.shell",             R"({"form":"starfish","size":"0x1p+1","ribs":12,"seed":0})"},
        {"nature.blocky_tree",       R"({"form":"cluster","trunk_height":"0x1p+3","canopy_size":"0x1p+3","blocks":4,"seed":0})"},
        {"architecture.stone_lantern", R"({"height":"0x1p+2","width":"0x1p+1","cap":"hexagonal","cap_curve":"0x1p+0","seed":0})"}
    };
    rows.pins                = {
        {"architecture.bridge_deck 1", 448, 364, 1456, 0xea726f7f38d136e5ull, {-2.00000f, 0.00000f, -1.00000f}, {2.00000f, 3.05000f, 1.00000f}, {0.00000f, 1.86712f, 0.00000f}, 131.59743f},
        {"architecture.buttress 1", 68, 50, 204, 0x09b9ae9c1340a5b7ull, {-0.25000f, 0.00000f, -0.50000f}, {0.25000f, 3.00000f, 0.50000f}, {0.00000f, 1.68828f, -0.06493f}, 14.15788f},
        {"nature.log 1", 186, 360, 1080, 0xf32a5134be453ceaull, {-1.13168f, 0.00000f, -0.14101f}, {1.13982f, 0.25663f, 0.13753f}, {0.01975f, 0.11437f, -0.00986f}, 1.51012f},
        {"nature.log 3", 186, 360, 1080, 0xf32a5134be453ceaull, {-0.89971f, 0.00000f, -0.15498f}, {0.87642f, 0.23224f, 0.14382f}, {-0.01084f, 0.11184f, -0.01845f}, 1.19229f},
        {"nature.log 17", 186, 360, 1080, 0xf32a5134be453ceaull, {-1.29480f, 0.00000f, -0.49379f}, {1.32628f, 0.71171f, 0.42438f}, {-0.00568f, 0.31309f, -0.08021f}, 5.32555f},
        {"nature.shell 1", 162, 320, 960, 0x5bdc90bcaed8a52cull, {-0.17296f, 0.00000f, -0.08435f}, {0.22816f, 0.19199f, 0.08764f}, {-0.00032f, 0.07643f, -0.00086f}, 0.14261f},
        {"nature.shell 3", 162, 320, 960, 0x5bdc90bcaed8a52cull, {-0.15134f, 0.00000f, -0.07380f}, {0.19964f, 0.16457f, 0.07660f}, {-0.00022f, 0.06658f, -0.00090f}, 0.10629f},
        {"nature.shell 17", 162, 320, 960, 0x5bdc90bcaed8a52cull, {-0.15134f, 0.00000f, -0.07380f}, {0.19964f, 0.15541f, 0.07845f}, {-0.00016f, 0.06662f, -0.00084f}, 0.10669f},
        {"nature.blocky_tree 1", 88, 66, 264, 0xb7a3053f006ecb2full, {-1.09870f, 0.00000f, -0.75747f}, {0.72969f, 2.63187f, 0.58005f}, {-0.18357f, 1.50582f, -0.06628f}, 20.08313f},
        {"nature.blocky_tree 3", 56, 42, 168, 0x04e87c8e6c3af5b3ull, {-1.33012f, 0.00000f, -1.39387f}, {1.05924f, 4.58380f, 1.05924f}, {-0.15900f, 2.20050f, -0.22847f}, 43.69777f},
        {"nature.blocky_tree 17", 56, 42, 168, 0x04e87c8e6c3af5b3ull, {-1.18694f, 0.00000f, -0.94472f}, {0.94472f, 4.33586f, 1.26470f}, {-0.16257f, 2.13054f, 0.12022f}, 34.69960f},
        {"architecture.stone_lantern 1", 974, 1296, 4456, 0xd73d3950d1d1e3a9ull, {-0.30000f, 0.00000f, -0.25981f}, {0.30000f, 1.50000f, 0.25981f}, {0.00000f, 1.00130f, 0.00000f}, 2.96776f}
    };
    return rows;
}

} // namespace editor::kit_test
