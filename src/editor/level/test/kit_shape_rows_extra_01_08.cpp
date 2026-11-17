#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_extra_01_08.cpp.
namespace editor::kit_test {

auto get_extra_01_08_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {
        "prop.crate", "prop.barrel", "ruin.broken_wall", "ruin.rubble", "traversal.ladder", "harbour.pier", "harbour.mooring", "light.lamp_post"
    };
    rows.recreated_originals = {};
    rows.seeded_shapes       = {"ruin.broken_wall", "ruin.rubble", "harbour.pier", "harbour.mooring"};
    rows.enum_shapes         = {"prop.crate", "prop.barrel", "ruin.broken_wall", "harbour.pier", "harbour.mooring", "light.lamp_post"};
    rows.components          = {
        {"prop.crate", Kit_component_rule::exactly, 24}, {"ruin.rubble", Kit_component_rule::exactly, 17},
        {"traversal.ladder", Kit_component_rule::exactly, 11}, {"harbour.pier", Kit_component_rule::exactly, 15},
        {"harbour.mooring", Kit_component_rule::exactly, 2}, {"light.lamp_post", Kit_component_rule::exactly, 10}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"prop.crate", 288}, {"prop.barrel", 596}, {"ruin.broken_wall", 96}, {"ruin.rubble", 238}, {"traversal.ladder", 132},
        {"harbour.pier", 212}, {"harbour.mooring", 188}, {"light.lamp_post", 258}
    };
    // R2.6 where the triangle maxima lie: five slats under a lid, four hoops on a lying barrel of 24 staves, 24 columns,
    // 48 blocks, the highest ladder with the closest rungs, the longest pier on the closest braced bents, a bollard
    // with a rope, four globes on a post of 16 points.
    rows.worst_cases         = {
        {"prop.crate",       R"({"width":"0x1.333334p-2","height":"0x1.333334p-2","depth":"0x1.333334p-2","slats":5,"lid":"closed","seed":0})"},
        {"prop.barrel",      R"({"height":"0x1.99999ap-2","radius":"0x1.333334p-3","bulge":"0x0p+0","hoops":4,"pose":"lying","seed":0,"segments":24})"},
        {"ruin.broken_wall", R"({"length":"0x1.99999ap-4","height":"0x1.99999ap-4","thickness":"0x1.47ae14p-6","intact":"0x1.333334p-2","break":"stepped","course":"0x1.99999ap-5","steps":24,"seed":0})"},
        {"ruin.rubble",      R"({"radius":"0x1.333334p-2","height":"0x1.99999ap-3","blocks":48,"block_size":"0x1.99999ap-4","seed":0})"},
        {"traversal.ladder", R"({"height":"0x1.8p+3","width":"0x1.333334p-2","rung_spacing":"0x1.333334p-3","lean":"0x0p+0","seed":0})"},
        {"harbour.pier",     R"({"length":"0x1p+4","width":"0x1.333334p+0","pile_depth":"0x1p-1","pile_spacing":"0x1p+0","braces":"cross","plank_jitter":"0x0p+0","seed":0,"segments":12})"},
        {"harbour.mooring",  R"({"form":"bollard","size":"0x1.99999ap-3","lean":"0x0p+0","rope":"coil","seed":0,"segments":16})"},
        {"light.lamp_post",  R"({"height":"0x1.8p+0","arms":4,"arm_length":"0x1.333334p-2","head":"globe","seed":0,"segments":16})"}
    };
    rows.pins                = {
        {"prop.crate 1", 192, 144, 576, 0x4a263b301c082585ull, {-0.25000f, 0.00000f, -0.25000f}, {0.25000f, 1.25000f, 0.25000f}, {0.00000f, 0.69193f, 0.00000f}, 6.36149f},
        {"prop.barrel 1", 300, 290, 1176, 0x562237b4ad100127ull, {-0.34140f, 0.00000f, -0.34140f}, {0.34140f, 1.05000f, 0.34140f}, {0.00000f, 0.52500f, 0.00000f}, 3.10173f},
        {"ruin.broken_wall 1", 50, 55, 206, 0x6d8ab1e137d11532ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {0.04548f, 0.54290f, 0.00000f}, 10.68504f},
        {"ruin.broken_wall 3", 50, 55, 206, 0x6d8ab1e137d11532ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {-0.01026f, 0.84030f, 0.00000f}, 13.84604f},
        {"ruin.broken_wall 17", 50, 55, 206, 0x6d8ab1e137d11532ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {-0.00013f, 0.89790f, 0.00000f}, 14.79545f},
        {"ruin.rubble 1", 153, 121, 480, 0xac006c4a2359971cull, {-0.63750f, 0.00000f, -0.63750f}, {0.63750f, 0.41250f, 0.63750f}, {-0.01376f, 0.14544f, 0.03007f}, 9.39209f},
        {"ruin.rubble 3", 153, 121, 480, 0xac006c4a2359971cull, {-0.42500f, 0.00000f, -0.42500f}, {0.43961f, 0.25000f, 0.42500f}, {0.00437f, 0.07821f, -0.01229f}, 5.88005f},
        {"ruin.rubble 17", 153, 121, 480, 0xac006c4a2359971cull, {-1.06250f, 0.00000f, -1.06250f}, {1.07234f, 0.25000f, 1.06250f}, {0.02603f, 0.06664f, 0.03162f}, 12.23719f},
        {"traversal.ladder 1", 88, 66, 264, 0x8bc5d0a5d7a8b157ull, {-0.25000f, 0.00000f, -0.56498f}, {0.25000f, 3.00000f, 0.03600f}, {0.00000f, 1.50000f, -0.26449f}, 1.98527f},
        {"harbour.pier 1", 136, 98, 408, 0x8c704f9b10f11f37ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 2.99979f, 1.00000f}, {0.00000f, 2.39218f, -0.00012f}, 17.68068f},
        {"harbour.pier 3", 136, 98, 408, 0x8c704f9b10f11f37ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 2.99991f, 1.00000f}, {0.00000f, 2.39216f, -0.00051f}, 17.68441f},
        {"harbour.pier 17", 136, 98, 408, 0x8c704f9b10f11f37ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 2.99989f, 1.00000f}, {0.00000f, 2.39207f, -0.00220f}, 17.63210f},
        {"harbour.mooring 1", 98, 88, 364, 0xd53bb0e5a37d8cf1ull, {-0.27500f, 0.00000f, -0.27500f}, {0.27500f, 0.50000f, 0.27500f}, {0.00649f, 0.26388f, -0.00500f}, 1.33720f},
        {"harbour.mooring 3", 98, 88, 364, 0xd53bb0e5a37d8cf1ull, {-0.27500f, 0.00000f, -0.27500f}, {0.27500f, 0.50000f, 0.27500f}, {0.00352f, 0.26388f, -0.00210f}, 1.33714f},
        {"harbour.mooring 17", 98, 88, 364, 0xd53bb0e5a37d8cf1ull, {-0.27500f, 0.00000f, -0.27500f}, {0.27500f, 0.50000f, 0.27500f}, {0.03156f, 0.26388f, 0.00959f}, 1.33855f},
        {"light.lamp_post 1", 149, 127, 512, 0x10b36312d517adbaull, {-0.20160f, 0.00000f, -0.20160f}, {0.20160f, 3.60000f, 0.20160f}, {0.00000f, 2.49022f, 0.00000f}, 3.13169f}
    };
    return rows;
}

} // namespace editor::kit_test
