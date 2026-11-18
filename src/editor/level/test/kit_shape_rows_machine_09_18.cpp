#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_machine_09_18.cpp.
namespace editor::kit_test {

auto get_machine_09_18_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {
        "machine.connecting_rod", "machine.crankshaft", "machine.follower", "machine.camshaft", "machine.bevel_gear",
        "machine.worm", "machine.pulley", "machine.washer", "machine.cylinder", "machine.pillow_block"
    };
    rows.recreated_originals = {};
    rows.seeded_shapes       = {};
    rows.enum_shapes         = {"machine.crankshaft", "machine.follower", "machine.worm", "machine.pulley", "machine.washer"};
    rows.components          = {
        {"machine.connecting_rod", Kit_component_rule::exactly, 7},
        {"machine.crankshaft", Kit_component_rule::exactly, 11},
        {"machine.follower", Kit_component_rule::exactly, 7},
        {"machine.camshaft", Kit_component_rule::exactly, 7},
        {"machine.bevel_gear", Kit_component_rule::exactly, 17},
        {"machine.worm", Kit_component_rule::exactly, 2},
        {"machine.cylinder", Kit_component_rule::exactly, 2},
        {"machine.pillow_block", Kit_component_rule::exactly, 6}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"machine.connecting_rod", 284}, {"machine.crankshaft", 292}, {"machine.follower", 248}, {"machine.camshaft", 580},
        {"machine.bevel_gear", 512}, {"machine.worm", 432}, {"machine.pulley", 336}, {"machine.washer", 128},
        {"machine.cylinder", 1088}, {"machine.pillow_block", 288}
    };
    // R2.6 where the triangle maxima lie: every count at its maximum, in the form with the most triangles.
    rows.worst_cases         = {
        {"machine.crankshaft",   R"({"throws":6,"stroke":"0x1.99999ap-3","journal":"0x1.47ae14p-4","web":"0x1.47ae14p-5","phase":"flat","seed":0})"},
        {"machine.camshaft",     R"({"lobes":8,"length":"0x1p+0","lift":"0x1.47ae14p-5","radius":"0x1.eb851ep-5","step":"0x1.68p+6","seed":0})"},
        {"machine.bevel_gear",   R"({"teeth":32,"module":"0x1.eb851ep-6","face_width":"0x1.47ae14p-4","cone":"0x1.68p+5","bore":"0x1.47ae14p-4","seed":0})"},
        {"machine.worm",         R"({"form":"worm","module":"0x1.eb851ep-6","diameter":"0x1.eb851ep-4","starts":3,"turns":6,"teeth":16,"seed":0})"},
        {"machine.pulley",       R"({"form":"sprocket","diameter":"0x1.333334p-2","width":"0x1.eb851ep-5","grooves":1,"teeth":32,"bore":"0x1.99999ap-5","seed":0})"},
        {"machine.washer",       R"({"form":"toothed","diameter":"0x1.eb851ep-5","thickness":"0x1.47ae14p-7","teeth":16,"seed":0})"},
        {"machine.cylinder",     R"({"bore":"0x1.333334p-2","height":"0x1p-1","fins":10,"wall":"0x1.eb851ep-6","seed":0})"},
        {"machine.pillow_block", R"({"bore":"0x1.99999ap-4","height":"0x1.333334p-3","width":"0x1.99999ap-2","depth":"0x1.99999ap-4","bolts":4,"seed":0})"}
    };
    rows.pins                = {
        {"machine.connecting_rod 1", 152, 142, 568, 0xe40bbaeb0e74d0cbull, {-0.29500f, 0.00000f, -0.08128f}, {0.31400f, 0.06000f, 0.08128f}, {0.05085f, 0.03000f, 0.00000f}, 0.27296f},
        {"machine.crankshaft 1", 168, 106, 504, 0x0c0f053431255cc7ull, {-0.34000f, 0.00000f, -0.06400f}, {0.34000f, 0.36005f, 0.06400f}, {-0.00000f, 0.18614f, -0.00000f}, 0.76887f},
        {"machine.follower 1", 136, 100, 448, 0xcc43ffc1e85863ddull, {-0.04000f, 0.00000f, -0.03804f}, {0.04000f, 0.48800f, 0.03804f}, {0.00000f, 0.17107f, 0.00000f}, 0.14376f},
        {"machine.camshaft 1", 296, 282, 1144, 0xdc144145a283ea7cull, {-0.50000f, 0.00000f, -0.10000f}, {0.50000f, 0.21800f, 0.10000f}, {0.00000f, 0.11452f, 0.00000f}, 0.60720f},
        {"machine.bevel_gear 1", 288, 256, 1024, 0x447bfb7f98e6154aull, {-0.26113f, 0.00000f, -0.26113f}, {0.26113f, 0.12212f, 0.26113f}, {0.00000f, 0.05440f, 0.00000f}, 0.70933f},
        {"machine.worm 1", 220, 400, 1232, 0x5f03d6ec05b0ae02ull, {-0.25739f, -0.00000f, -0.08693f}, {0.25739f, 0.17387f, 0.08693f}, {-0.01749f, 0.08595f, -0.00026f}, 0.29299f},
        {"machine.pulley 1", 168, 168, 672, 0x4afb59e2a3a2aeddull, {-0.15000f, 0.00000f, -0.15000f}, {0.15000f, 0.06000f, 0.15000f}, {0.00000f, 0.03000f, 0.00000f}, 0.21389f},
        {"machine.washer 1", 64, 64, 256, 0x4bd078952b3d3835ull, {-0.06600f, 0.00000f, -0.06600f}, {0.06600f, 0.01000f, 0.06600f}, {0.00000f, 0.00500f, 0.00000f}, 0.02618f},
        {"machine.cylinder 1", 544, 544, 2176, 0x4186e0391e65974aull, {-0.22950f, 0.00000f, -0.22950f}, {0.22950f, 0.50000f, 0.22950f}, {0.00000f, 0.25088f, 0.00000f}, 2.84611f},
        {"machine.pillow_block 1", 152, 140, 568, 0xe4b9d483823ac649ull, {-0.20000f, 0.00000f, -0.05000f}, {0.20000f, 0.24500f, 0.05000f}, {0.00000f, 0.12153f, -0.00000f}, 0.37305f}
    };
    return rows;
}

} // namespace editor::kit_test
