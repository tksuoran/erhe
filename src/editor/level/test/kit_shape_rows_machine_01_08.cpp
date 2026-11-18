#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_machine_01_08.cpp.
namespace editor::kit_test {

auto get_machine_01_08_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {"machine.gear", "machine.rack", "machine.bolt", "machine.nut", "machine.spring", "machine.bearing", "machine.piston", "machine.cam"};
    rows.recreated_originals = {};
    rows.seeded_shapes       = {};
    rows.enum_shapes         = {"machine.gear", "machine.bolt", "machine.nut", "machine.spring", "machine.bearing", "machine.cam"};
    rows.components          = {
        {"machine.gear", Kit_component_rule::exactly, 17},
        {"machine.rack", Kit_component_rule::exactly, 13},
        {"machine.bolt", Kit_component_rule::exactly, 2},
        {"machine.bearing", Kit_component_rule::exactly, 13},
        {"machine.piston", Kit_component_rule::exactly, 2},
        {"machine.cam", Kit_component_rule::exactly, 2}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"machine.gear", 1088}, {"machine.rack", 252}, {"machine.bolt", 508}, {"machine.nut", 72},
        {"machine.spring", 772}, {"machine.bearing", 864}, {"machine.piston", 568}, {"machine.cam", 384}
    };
    // R2.6 where the triangle maxima lie: every count at its maximum, in the form with the most triangles.
    rows.worst_cases         = {
        {"machine.gear",    R"({"teeth":40,"module":"0x1.eb851ep-6","face_width":"0x1.47ae14p-4","bore":"0x1.47ae14p-4","body":"web","spokes":3,"seed":0})"},
        {"machine.rack",    R"({"teeth":40,"module":"0x1.eb851ep-6","face_width":"0x1.47ae14p-4","height":"0x1.47ae14p-4","seed":0})"},
        {"machine.bolt",    R"({"head":"socket","diameter":"0x1.eb851ep-5","length":"0x1.333334p-2","thread":"0x1.333334p-1","turns":40,"seed":0})"},
        {"machine.nut",     R"({"form":"cap","diameter":"0x1.eb851ep-5","height":"0x1.99999ap-5","seed":0})"},
        {"machine.spring",  R"({"form":"extension","diameter":"0x1.99999ap-3","wire":"0x1.47ae14p-6","coils":16,"length":"0x1.99999ap-2","seed":0})"},
        {"machine.bearing", R"({"form":"ball","bore":"0x1.99999ap-4","diameter":"0x1.333334p-2","width":"0x1.47ae14p-4","elements":20,"seed":0})"},
        {"machine.piston",  R"({"diameter":"0x1.333334p-2","height":"0x1.333334p-2","rings":4,"seed":0})"}
    };
    rows.pins                = {
        {"machine.gear 1", 576, 512, 2112, 0x3bd2807f93fe169full, {-0.26988f, 0.00000f, -0.26988f}, {0.26988f, 0.10000f, 0.26988f}, {0.00000f, 0.04222f, 0.00000f}, 0.86833f},
        {"machine.rack 1", 152, 102, 456, 0x0c859f79b81a2cf3ull, {-0.56549f, 0.00000f, -0.08000f}, {0.56549f, 0.08000f, 0.06750f}, {0.00000f, 0.04000f, 0.02963f}, 0.68314f},
        {"machine.bolt 1", 258, 246, 1000, 0x0986642a36543e83ull, {-0.05400f, 0.00000f, -0.04677f}, {0.05400f, 0.33900f, 0.04677f}, {0.00000f, 0.10708f, 0.00000f}, 0.08829f},
        {"machine.nut 1", 36, 54, 180, 0x0d855de98e06d1c3ull, {-0.05520f, 0.00000f, -0.04780f}, {0.05520f, 0.05000f, 0.04780f}, {0.00000f, 0.02500f, 0.00000f}, 0.03641f},
        {"machine.spring 1", 388, 770, 2312, 0x95c449904f218bc2ull, {-0.11000f, 0.00000f, -0.11000f}, {0.11000f, 0.40000f, 0.11000f}, {0.00103f, 0.20000f, -0.00000f}, 0.27743f},
        {"machine.bearing 1", 452, 512, 1888, 0x1e35c585972d23eaull, {-0.15000f, 0.00000f, -0.15000f}, {0.15000f, 0.08000f, 0.15000f}, {0.00000f, 0.04000f, 0.00000f}, 0.40017f},
        {"machine.piston 1", 288, 268, 1104, 0x72a3a4d8ce53312dull, {-0.14829f, 0.00000f, -0.14880f}, {0.14829f, 0.30000f, 0.14880f}, {0.00000f, 0.20900f, 0.00000f}, 0.60635f},
        {"machine.cam 1", 192, 192, 768, 0x27807aa212502605ull, {-0.15000f, 0.00000f, -0.15000f}, {0.25000f, 0.09600f, 0.15000f}, {0.00423f, 0.04500f, -0.00000f}, 0.27374f}
    };
    return rows;
}

} // namespace editor::kit_test
