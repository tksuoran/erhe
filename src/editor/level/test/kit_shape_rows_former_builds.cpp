#include "kit_shape_test_support.hpp"

#include <algorithm>
#include <string_view>

// The former builds kept for comparison (doc/editor/level_kit_shapes.md "Former builds"): each "<id>_old" takes the
// rows of its shape, but the triangle count and the pins of the build before the fix.
namespace editor::kit_test {

namespace {

class Former_build
{
public:
    std::string           id;
    unsigned int          triangle_count{0};
    std::vector<Mesh_pin> pins;          // named "<id> <seed>", renamed here
    unsigned int          components{0}; // the former build's exact part count where it differs from the shape's row
};

[[nodiscard]] auto get_former_builds() -> std::vector<Former_build>
{
    return {
        Former_build{"nature.palm", 1510, {
            {"nature.palm 1", 843, 1495, 4500, 0x5d9ce96fdb009a31ull, {-3.05603f, 0.00000f, -1.48759f}, {2.37736f, 12.33090f, 3.88708f}, {-0.45316f, 10.53339f, 0.92783f}, 58.33212f},
            {"nature.palm 3", 555, 983, 2964, 0x6eefdf6fe4d09787ull, {-3.48997f, 0.00000f, -2.01061f}, {1.86270f, 10.79839f, 3.08687f}, {-0.61547f, 9.04642f, 0.50534f}, 35.82668f},
            {"nature.palm 17", 843, 1495, 4500, 0x5d9ce96fdb009a31ull, {-3.22049f, 0.00000f, -4.16760f}, {2.50953f, 12.78151f, 1.69860f}, {-0.43074f, 10.89694f, -1.16128f}, 58.57428f}
        }},
        Former_build{"nature.palm_clump", 1912, {
            {"nature.palm_clump 1", 1134, 1865, 5642, 0x344df1114a183a71ull, {-1.56352f, 0.00000f, -2.17078f}, {1.37917f, 4.85206f, 1.75516f}, {-0.03700f, 2.77721f, -0.01980f}, 18.72364f},
            {"nature.palm_clump 3", 670, 1097, 3322, 0x17ae610c8871716dull, {-0.39434f, 0.00000f, -0.74393f}, {0.88112f, 2.23237f, 0.61127f}, {0.11201f, 1.40232f, -0.03305f}, 2.72475f},
            {"nature.palm_clump 17", 506, 825, 2502, 0x6dfb0758e807b7f2ull, {-1.68756f, 0.00000f, -0.69025f}, {0.93280f, 4.08483f, 0.83392f}, {-0.10641f, 2.28608f, -0.02681f}, 5.82635f}
        }},
        Former_build{"harbour.quay_wall", 72, {
            {"harbour.quay_wall 1", 48, 36, 144, 0x961f5f49a3ddc005ull, {-1.00000f, 0.00000f, -0.50000f}, {1.00000f, 3.09600f, 0.87672f}, {0.00000f, 1.75592f, 0.42810f}, 34.98780f}
        }, 6},
        Former_build{"prop.market_stall", 144, {
            {"prop.market_stall 1", 96, 72, 288, 0xb51bf3a44c0bcdf5ull, {-1.00000f, 0.00000f, -0.70000f}, {1.00000f, 2.40000f, 0.70000f}, {0.00000f, 1.21045f, 0.16283f}, 18.29421f}
        }, 12},
        Former_build{"prop.well", 392, {
            {"prop.well 1", 212, 180, 752, 0x5e2547712c74b785ull, {-0.95400f, 0.00000f, -0.78600f}, {1.10100f, 1.95000f, 0.78600f}, {0.11668f, 1.04033f, 0.11698f}, 14.01414f}
        }},
        Former_build{"light.fire", 230, {
            {"light.fire 1", 129, 105, 440, 0x2a78b6af68ea4cb0ull, {-0.33600f, 0.00000f, -0.33600f}, {0.33600f, 1.12000f, 0.33600f}, {-0.00026f, 0.44296f, -0.00135f}, 2.27471f},
            {"light.fire 3", 129, 105, 440, 0x2a78b6af68ea4cb0ull, {-0.33600f, 0.00000f, -0.33600f}, {0.33600f, 1.12000f, 0.33600f}, {0.00331f, 0.44296f, -0.00078f}, 2.23239f},
            {"light.fire 17", 129, 105, 440, 0x2a78b6af68ea4cb0ull, {-0.33600f, 0.00000f, -0.33600f}, {0.33600f, 0.92000f, 0.33600f}, {0.00333f, 0.42746f, -0.00451f}, 2.17149f}
        }},
        Former_build{"industrial.vent", 156, {
            {"industrial.vent 1", 104, 78, 312, 0x0bb080c7307fa39bull, {-0.37500f, 0.00000f, -0.30000f}, {0.37500f, 1.50000f, 0.30000f}, {0.00000f, 0.75404f, 0.00000f}, 7.64178f}
        }},
        Former_build{"harbour.boat", 504, {
            {"harbour.boat 1", 266, 438, 1380, 0x65dbee3d8b6f6d98ull, {-4.00000f, 0.00000f, -0.58800f}, {4.00000f, 1.01794f, 0.83815f}, {-0.00000f, 0.42549f, 0.10671f}, 34.40163f}
        }},
        Former_build{"machine.spring", 772, {
            {"machine.spring 1", 388, 770, 2312, 0x95c449904f218bc2ull, {-0.11000f, 0.00000f, -0.11000f}, {0.11000f, 0.40000f, 0.11000f}, {0.00103f, 0.20000f, -0.00000f}, 0.27743f}
        }}
    };
}

[[nodiscard]] auto renamed(const std::string& id) -> std::string
{
    return id + "_old";
}

void copy_ids(const std::vector<std::string>& from, const std::string& id, std::vector<std::string>& to)
{
    if (std::find(from.begin(), from.end(), id) != from.end()) {
        to.push_back(renamed(id));
    }
}

} // anonymous namespace

auto get_former_build_rows(const std::vector<Kit_shape_rows>& batches) -> Kit_shape_rows
{
    Kit_shape_rows rows;
    for (const Former_build& former : get_former_builds()) {
        const std::string& id = former.id;
        for (const Kit_shape_rows& batch : batches) {
            copy_ids(batch.shapes,           id, rows.shapes);
            copy_ids(batch.seeded_shapes,    id, rows.seeded_shapes);
            copy_ids(batch.enum_shapes,      id, rows.enum_shapes);
            copy_ids(batch.walkability_open, id, rows.walkability_open);
            for (const Kit_component_row& row : batch.components) {
                if (row.id == id) {
                    rows.components.push_back(
                        (former.components != 0)
                            ? Kit_component_row{.id = renamed(id), .rule = Kit_component_rule::exactly, .count = former.components}
                            : Kit_component_row{.id = renamed(id), .rule = row.rule, .count = row.count}
                    );
                }
            }
            for (const std::pair<std::string, std::string>& row : batch.worst_cases) {
                if (row.first == id) {
                    rows.worst_cases.emplace_back(renamed(id), row.second);
                }
            }
        }
        rows.triangle_counts.emplace_back(renamed(id), former.triangle_count);
        for (Mesh_pin pin : former.pins) {
            pin.name = renamed(id) + pin.name.substr(id.size());
            rows.pins.push_back(pin);
        }
    }
    return rows;
}

} // namespace editor::kit_test
