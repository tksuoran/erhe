#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_nature_v2.cpp.
namespace editor::kit_test {

auto get_nature_v2_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {};
    rows.recreated_originals = {"nature.rock", "nature.land_mass", "nature.hill", "nature.tree", "nature.bush"};
    rows.seeded_shapes       = {"nature.rock.v2", "nature.land_mass.v2", "nature.hill.v2", "nature.tree.v2", "nature.bush.v2"};
    rows.enum_shapes         = {"nature.rock.v2", "nature.tree.v2"};
    rows.components          = {
        {"nature.rock.v2", Kit_component_rule::exactly, 3}, {"nature.land_mass.v2", Kit_component_rule::exactly, 1},
        {"nature.hill.v2", Kit_component_rule::exactly, 3}, {"nature.tree.v2", Kit_component_rule::more_than, 5},
        {"nature.bush.v2", Kit_component_rule::more_than, 5}
    };
    rows.walkability_open    = {};
    rows.triangle_counts     = {
        {"nature.rock.v2", 162}, {"nature.land_mass.v2", 280}, {"nature.hill.v2", 304}, {"nature.tree.v2", 458}, {"nature.bush.v2", 408}
    };
    rows.worst_cases         = {};
    rows.pins                = {
        {"nature.rock.v2 1", 87, 108, 378, 0xcc65a7abbd5c9859ull, {-0.38246f, 0.00000f, -0.32975f}, {0.51908f, 0.41698f, 0.34153f}, {0.03528f, 0.13556f, -0.09896f}, 1.72307f},
        {"nature.rock.v2 3", 83, 103, 360, 0x6c8af37161e47062ull, {-1.08382f, 0.00000f, -1.18839f}, {0.83022f, 0.87129f, 0.82110f}, {-0.11019f, 0.29129f, -0.12414f}, 8.59265f},
        {"nature.rock.v2 17", 95, 118, 414, 0x0c305fff1b75f603ull, {-0.61478f, 0.00000f, -0.95683f}, {0.58810f, 0.69334f, 0.62484f}, {-0.18276f, 0.24415f, -0.04642f}, 4.92481f},
        {"nature.land_mass.v2 1", 142, 280, 840, 0x1d7d06dd136a33a5ull, {-1.63238f, 0.00000f, -1.80724f}, {1.63864f, 1.00000f, 1.79658f}, {-0.00724f, 0.57774f, -0.00370f}, 24.12314f},
        {"nature.land_mass.v2 3", 142, 280, 840, 0x1d7d06dd136a33a5ull, {-1.47213f, 0.00000f, -1.86135f}, {1.78628f, 1.00000f, 1.65956f}, {-0.01490f, 0.58049f, -0.01205f}, 23.86616f},
        {"nature.land_mass.v2 17", 142, 280, 840, 0x1d7d06dd136a33a5ull, {-1.50885f, 0.00000f, -1.97722f}, {1.64668f, 1.00000f, 1.64419f}, {-0.00146f, 0.57901f, -0.00286f}, 24.08301f},
        {"nature.hill.v2 1", 158, 262, 828, 0xf8df0aae6243e623ull, {-2.61038f, 0.00000f, -2.81574f}, {2.72897f, 1.13459f, 2.82760f}, {-0.44147f, 0.46375f, 0.05415f}, 55.11851f},
        {"nature.hill.v2 3", 146, 247, 774, 0x59399e20a8b26082ull, {-2.55359f, 0.00000f, -2.84196f}, {2.83937f, 1.08355f, 2.71595f}, {-0.50907f, 0.38240f, 0.09136f}, 53.17775f},
        {"nature.hill.v2 17", 150, 252, 792, 0xb914525b042a8879ull, {-2.63117f, 0.00000f, -2.93051f}, {2.73480f, 0.74337f, 2.67818f}, {0.14951f, 0.26875f, -0.65042f}, 54.24910f},
        {"nature.tree.v2 1", 249, 458, 1374, 0x749f65e5e38f5aa4ull, {-0.50365f, 0.00000f, -1.07343f}, {1.03693f, 2.87100f, 0.91544f}, {0.13823f, 1.91490f, -0.15197f}, 14.64000f},
        {"nature.tree.v2 3", 292, 536, 1608, 0xd30a3dd2d8378012ull, {-1.37001f, 0.00000f, -1.63476f}, {1.75703f, 4.55050f, 1.66454f}, {-0.01467f, 2.98247f, 0.13749f}, 48.37881f},
        {"nature.tree.v2 17", 249, 458, 1374, 0x749f65e5e38f5aa4ull, {-1.11667f, 0.00000f, -1.00312f}, {0.95245f, 4.02450f, 1.28052f}, {0.00166f, 2.84520f, 0.00530f}, 22.08683f},
        {"nature.bush.v2 1", 224, 408, 1224, 0x90a9514e1cced0f2ull, {-0.60605f, 0.00000f, -0.54539f}, {0.51445f, 0.83947f, 0.50183f}, {-0.02048f, 0.37057f, -0.05127f}, 6.59186f},
        {"nature.bush.v2 3", 224, 408, 1224, 0x90a9514e1cced0f2ull, {-0.43904f, 0.00000f, -0.57791f}, {0.61761f, 0.80404f, 0.57261f}, {0.02901f, 0.33909f, 0.01638f}, 5.71063f},
        {"nature.bush.v2 17", 250, 456, 1368, 0x62589ed50ece6ea2ull, {-0.64403f, 0.00000f, -0.51339f}, {0.53523f, 0.74696f, 0.60100f}, {-0.01896f, 0.30260f, -0.02817f}, 6.61271f}
    };
    return rows;
}

} // namespace editor::kit_test
