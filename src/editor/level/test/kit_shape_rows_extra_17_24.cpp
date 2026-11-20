#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_extra_17_24.cpp.
namespace editor::kit_test {

auto get_extra_17_24_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {
        "ruin.broken_pillar", "ruin.broken_arch", "light.lantern", "light.fire", "harbour.buoy", "traversal.trestle", "traversal.scaffold",
        "architecture.tunnel"
    };
    rows.seeded_shapes       = {"ruin.broken_pillar", "light.fire", "harbour.buoy"};
    rows.enum_shapes         = {"ruin.broken_pillar", "light.lantern", "light.fire", "harbour.buoy", "traversal.scaffold", "architecture.tunnel"};
    rows.components          = {
        {"ruin.broken_pillar", Kit_component_rule::exactly, 2}, {"ruin.broken_arch", Kit_component_rule::exactly, 2},
        {"light.lantern", Kit_component_rule::exactly, 11}, {"light.fire", Kit_component_rule::exactly, 7},
        {"harbour.buoy", Kit_component_rule::exactly, 4}, {"traversal.trestle", Kit_component_rule::exactly, 6},
        {"traversal.scaffold", Kit_component_rule::exactly, 29}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"ruin.broken_pillar", 24}, {"ruin.broken_arch", 120}, {"light.lantern", 190}, {"light.fire", 230},
        {"harbour.buoy", 312}, {"traversal.trestle", 72}, {"traversal.scaffold", 348}, {"architecture.tunnel", 92}
    };
    // R2.6 where the triangle maxima lie: three drums of 64 points, 48 segments, a hanging lantern of eight sides, a
    // brazier, a float of 16 points, three braces, six cross-braced levels, a head of 48 segments.
    rows.worst_cases         = {
        {"ruin.broken_pillar",  R"({"profile":"round","width":"0x1.99999ap-5","height":"0x1.99999ap-4","segments":64,"standing":"0x1.99999ap-3","fallen":3,"seed":0})"},
        {"ruin.broken_arch",    R"({"span":"0x1.333334p-2","rise":"0x1.99999ap-3","depth":"0x1.99999ap-5","thickness":"0x1.99999ap-5","segments":48,"gap":"0x1.333334p-3","gap_center":"0x1p-2","seed":0})"},
        {"light.lantern",       R"({"mount":"hanging","size":"0x1.333334p-3","chain_length":"0x1.99999ap-4","sides":8,"seed":0})"},
        {"light.fire",          R"({"form":"brazier","size":"0x1.333334p-2","flame":"0x1.99999ap-4","seed":0})"},
        {"harbour.buoy",        R"({"form":"can","size":"0x1.99999ap-2","tilt":"0x0p+0","bands":1,"seed":0,"segments":16})"},
        {"traversal.trestle",   R"({"height":"0x1p-1","width":"0x1p-1","splay":"0x0p+0","braces":3,"seed":0})"},
        {"traversal.scaffold",  R"({"width":"0x1p+0","depth":"0x1p+0","levels":6,"level_height":"0x1.19999ap+1","braces":"cross","seed":0})"},
        {"architecture.tunnel", R"({"length":"0x1p+0","bore_width":"0x1.333334p+0","bore_height":"0x1.266666p+1","wall":"0x1.99999ap-4","profile":"round","seed":0,"segments":48})"}
    };
    rows.pins                = {
        {"ruin.broken_pillar 1", 16, 12, 48, 0xcd1026a0f6a50639ull, {-0.48101f, 0.00000f, -0.25000f}, {0.26608f, 0.79732f, 1.50767f}, {-0.04037f, 0.31250f, 0.59623f}, 3.63352f},
        {"ruin.broken_pillar 3", 16, 12, 48, 0xcd1026a0f6a50639ull, {-1.43132f, 0.00000f, -0.38102f}, {0.25000f, 1.54519f, 0.37559f}, {-0.53169f, 0.50000f, -0.00273f}, 5.14012f},
        {"ruin.broken_pillar 17", 32, 24, 96, 0x4ca59747b2d7c18dull, {-0.66562f, 0.00000f, -1.34336f}, {1.09409f, 1.78817f, 1.31797f}, {0.03457f, 0.39375f, -0.12273f}, 8.30533f},
        {"ruin.broken_arch 1", 64, 60, 240, 0xcd65661b93493d29ull, {-1.30000f, 0.00000f, -0.25000f}, {1.30000f, 2.70000f, 0.25000f}, {0.21618f, 1.31434f, 0.00000f}, 8.54526f},
        {"light.lantern 1", 117, 91, 372, 0x75f652514f7e92beull, {-0.18600f, 0.00000f, -0.16108f}, {0.18600f, 0.51000f, 0.16108f}, {0.00000f, 0.21651f, 0.00000f}, 0.83495f},
        {"light.fire 1", 129, 105, 440, 0x2a78b6af68ea4cb0ull, {-0.33600f, 0.00000f, -0.33600f}, {0.33600f, 1.12000f, 0.33600f}, {-0.00026f, 0.44296f, -0.00135f}, 2.27471f},
        {"light.fire 3", 129, 105, 440, 0x2a78b6af68ea4cb0ull, {-0.33600f, 0.00000f, -0.33600f}, {0.33600f, 1.12000f, 0.33600f}, {0.00331f, 0.44296f, -0.00078f}, 2.23239f},
        {"light.fire 17", 129, 105, 440, 0x2a78b6af68ea4cb0ull, {-0.33600f, 0.00000f, -0.33600f}, {0.33600f, 0.92000f, 0.33600f}, {0.00333f, 0.42746f, -0.00451f}, 2.17149f},
        {"harbour.buoy 1", 164, 148, 608, 0x21b187a2470d388dull, {-0.33717f, 0.00000f, -0.44163f}, {0.49114f, 1.43000f, 0.32298f}, {0.10115f, 0.65695f, -0.07795f}, 2.89132f},
        {"harbour.buoy 3", 164, 148, 608, 0x21b187a2470d388dull, {-0.33480f, 0.00000f, -0.43284f}, {0.51724f, 1.43000f, 0.32378f}, {0.11986f, 0.65695f, -0.07165f}, 2.89502f},
        {"harbour.buoy 17", 164, 148, 608, 0x21b187a2470d388dull, {-0.33450f, 0.00000f, -0.32816f}, {0.52049f, 1.43000f, 0.38467f}, {0.12218f, 0.65695f, 0.03713f}, 2.89118f},
        {"traversal.trestle 1", 48, 36, 144, 0xcde545537e3f8f91ull, {-0.05600f, 0.00000f, -0.92638f}, {0.05600f, 3.00000f, 0.92638f}, {0.00000f, 1.48667f, 0.00000f}, 3.43142f},
        {"traversal.scaffold 1", 232, 174, 696, 0x30ae35aaa0a737ebull, {-1.00000f, 0.00000f, -0.50000f}, {1.00000f, 5.80000f, 0.50000f}, {0.00000f, 3.03310f, -0.07759f}, 17.24869f},
        {"architecture.tunnel 1", 46, 63, 218, 0xd30185cf648b97eaull, {-1.00000f, 0.00000f, -1.30000f}, {1.00000f, 2.80000f, 1.30000f}, {0.00000f, 1.43895f, 0.00000f}, 43.31943f}
    };
    return rows;
}

} // namespace editor::kit_test
