#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_extra_25_32.cpp.
namespace editor::kit_test {

auto get_extra_25_32_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {
        "harbour.cargo", "harbour.crane", "harbour.lighthouse", "industrial.tank", "industrial.truss", "industrial.container", "industrial.vent",
        "industrial.antenna"
    };
    rows.recreated_originals = {};
    rows.seeded_shapes       = {"harbour.cargo", "industrial.antenna"};
    rows.enum_shapes         = {
        "harbour.cargo", "harbour.crane", "harbour.lighthouse", "industrial.tank", "industrial.truss", "industrial.container", "industrial.vent",
        "industrial.antenna"
    };
    rows.components          = {
        {"harbour.cargo", Kit_component_rule::exactly, 25}, {"harbour.crane", Kit_component_rule::exactly, 8},
        {"harbour.lighthouse", Kit_component_rule::exactly, 18}, {"industrial.tank", Kit_component_rule::exactly, 5},
        {"industrial.truss", Kit_component_rule::exactly, 30}, {"industrial.container", Kit_component_rule::exactly,
        39}, {"industrial.vent", Kit_component_rule::exactly, 13}, {"industrial.antenna", Kit_component_rule::exactly,
        6}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"harbour.cargo", 300}, {"harbour.crane", 96}, {"harbour.lighthouse", 622}, {"industrial.tank", 380},
        {"industrial.truss", 360}, {"industrial.container", 468}, {"industrial.vent", 156}, {"industrial.antenna",
        208}
    };
    // R2.6 where the triangle maxima lie: three layers of barrels, a tower, a railed lighthouse of 24 points and six
    // bands, a domed tank of 24 points and four bands, a Pratt truss of 16 bays, a long container of six ribs, a louvre
    // box of eight slats, five dishes.
    rows.worst_cases         = {
        {"harbour.cargo",        R"({"load":"barrels","layers":3,"fill":"0x1p-1","width":"0x1.333334p-1","seed":0})"},
        {"harbour.crane",        R"({"form":"tower","height":"0x1p+1","jib_length":"0x1p+0","jib_heading":"0x0p+0","jib_angle":"0x1.4p+3","seed":0})"},
        {"harbour.lighthouse",   R"({"height":"0x1.8p+2","base_radius":"0x1p+0","taper":"0x1p-1","bands":6,"gallery":"rail","seed":0,"segments":24})"},
        {"industrial.tank",      R"({"radius":"0x1.333334p-2","height":"0x1p+0","pose":"upright","ends":"dome","bands":4,"seed":0,"segments":24})"},
        {"industrial.truss",     R"({"length":"0x1p+0","width":"0x1.99999ap-3","height":"0x1.99999ap-3","bays":16,"pattern":"pratt","seed":0})"},
        {"industrial.container", R"({"length":"long","ribs":6,"door":"closed","door_angle":"0x1.4p+2","seed":0})"},
        {"industrial.vent",      R"({"form":"louvre_box","width":"0x1.333334p-2","height":"0x1.333334p-2","slats":8,"seed":0})"},
        {"industrial.antenna",   R"({"form":"dish","height":"0x1p+0","elements":5,"tilt":"0x0p+0","seed":0})"}
    };
    rows.pins                = {
        {"harbour.cargo 1", 200, 150, 600, 0xd5e29537a2487333ull, {-0.50000f, 0.00000f, -0.49600f}, {0.50000f, 0.88000f, 0.49600f}, {0.00187f, 0.42838f, 0.00121f}, 12.26452f},
        {"harbour.cargo 3", 264, 198, 792, 0xa4e7e6e9af265963ull, {-0.50000f, 0.00000f, -0.49600f}, {0.50000f, 1.24000f, 0.49600f}, {0.00071f, 0.60415f, -0.00393f}, 15.79831f},
        {"harbour.cargo 17", 264, 198, 792, 0xa4e7e6e9af265963ull, {-0.50000f, 0.00000f, -0.49600f}, {0.50000f, 1.24000f, 0.49600f}, {0.00371f, 0.60415f, -0.00215f}, 15.00553f},
        {"harbour.crane 1", 64, 48, 192, 0x9020014de0371165ull, {-0.90000f, 0.00000f, -0.98549f}, {3.49981f, 4.50000f, 0.90000f}, {1.30137f, 1.87706f, -0.34870f}, 7.49951f},
        {"harbour.lighthouse 1", 345, 289, 1200, 0x271f370658a656c8ull, {-2.00000f, 0.00000f, -2.00000f}, {2.06000f, 12.00000f, 2.00000f}, {0.04243f, 8.17860f, 0.00000f}, 174.77808f},
        {"industrial.tank 1", 200, 182, 744, 0x18861dfe43ba61f3ull, {-1.03000f, 0.00000f, -1.03000f}, {1.03000f, 3.00000f, 1.03000f}, {0.00000f, 1.43760f, 0.00000f}, 22.81749f},
        {"industrial.truss 1", 240, 180, 720, 0xcae60a03fc79a121ull, {-2.00000f, 0.00000f, -0.25000f}, {2.00000f, 0.50000f, 0.25000f}, {0.00000f, 0.25000f, 0.00000f}, 7.90968f},
        {"industrial.container 1", 312, 234, 936, 0x59f7c632613ee5efull, {-3.00000f, 0.00000f, -1.20000f}, {3.00000f, 2.60000f, 1.20000f}, {0.38115f, 1.30051f, 0.00000f}, 186.95697f},
        {"industrial.vent 1", 104, 78, 312, 0x0bb080c7307fa39bull, {-0.37500f, 0.00000f, -0.30000f}, {0.37500f, 1.50000f, 0.30000f}, {0.00000f, 0.75404f, 0.00000f}, 7.64178f},
        {"industrial.antenna 1", 116, 94, 396, 0x0b5b414c93db6cabull, {-0.28636f, 0.00000f, -0.27961f}, {0.28339f, 3.00000f, 0.28285f}, {-0.00000f, 2.13392f, 0.00034f}, 2.21148f},
        {"industrial.antenna 3", 164, 134, 564, 0x0a86229a0c40e353ull, {-0.26698f, 0.00000f, -0.26655f}, {0.22500f, 3.00000f, 0.24731f}, {-0.02080f, 1.99954f, -0.02053f}, 2.27462f},
        {"industrial.antenna 17", 260, 214, 900, 0x08dbe534fd0bd0a3ull, {-0.29094f, 0.00000f, -0.22500f}, {0.27399f, 3.00000f, 0.31003f}, {-0.00835f, 1.62524f, 0.02427f}, 3.27655f}
    };
    return rows;
}

} // namespace editor::kit_test
