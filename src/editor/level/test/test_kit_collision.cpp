#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shape_library.hpp"
#include "kit_shape_test_support.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_math/aabb.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The collision kind every shape states (U54, doc/editor/level_tools.md "Kit brushes").
using namespace editor::kit_test;

namespace {

using editor::Kit_collision;
using editor::Kit_parameter;
using editor::Kit_parameter_type;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;

// Whether a shape states one kind, or a kind per values (Kit_shape_definition::get_collision).
enum class Kit_collision_source : unsigned int { fixed = 0, by_values };

class Kit_collision_row
{
public:
    const char*          id;
    Kit_collision        collision;
    Kit_collision_source source;
    const char*          not_a_box; // why values whose hull fills the bounds keep the shape's own kind; empty: none do
};

// Every shape of the library, so a new shape states its body here.
const std::vector<Kit_collision_row> c_collision_rows{
    {"basic.block",                     Kit_collision::box,           Kit_collision_source::fixed, ""},
    {"basic.wedge",                     Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"basic.cylinder",                  Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"basic.stairs",                    Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"basic.floor_slab",                Kit_collision::box,           Kit_collision_source::fixed, ""},
    {"architecture.wall",               Kit_collision::box,           Kit_collision_source::fixed, ""},
    {"architecture.door_wall",          Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the doorway stays open"},
    {"architecture.pillar",             Kit_collision::convex_hull,   Kit_collision_source::by_values, ""},
    {"architecture.arch",               Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"architecture.column",             Kit_collision::convex_hull,   Kit_collision_source::fixed, "a round shaft between the square plinth and abacus"},
    {"architecture.entablature",        Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the cornice overhangs the frieze"},
    {"architecture.torii",              Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"architecture.battlement",         Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the crenels between the merlons stay open"},
    {"architecture.tower",              Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the doorway and the inside stay open"},
    {"architecture.roof",               Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.rock",                     Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"nature.land_mass",                Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.hill",                     Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"nature.tree",                     Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.bush",                     Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"nature.cliff",                    Kit_collision::triangle_mesh, Kit_collision_source::fixed, "its ledges and overhang are surfaces to stand on and under"},
    {"nature.shore",                    Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.water",                    Kit_collision::none,          Kit_collision_source::fixed, ""},
    {"nature.dune",                     Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"nature.ground_patch",             Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"nature.rock_spire",               Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.basalt_columns",           Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.palm",                     Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.leaf_plant",               Kit_collision::none,          Kit_collision_source::fixed, ""},
    {"nature.palm_clump",               Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.palm_shrub",               Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"architecture.window_wall",        Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the window stays open"},
    {"architecture.pediment",           Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"architecture.stepped_platform",   Kit_collision::triangle_mesh, Kit_collision_source::by_values, "more than one tier: steps to climb"},
    {"nature.succulent",                Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"nature.grass_tuft",               Kit_collision::none,          Kit_collision_source::fixed, ""},
    {"nature.boulder_cluster",          Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.stepping_stone",           Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"architecture.timber_bay",         Kit_collision::triangle_mesh, Kit_collision_source::fixed, "an open timber frame"},
    {"architecture.railing",            Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the gaps between the balusters stay open"},
    {"nature.bamboo",                   Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"architecture.stairs",             Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"architecture.spiral_stairs",      Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"nature.rock_arch",                Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"architecture.bridge_deck",        Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the way under the deck, between its supports, stays open"},
    {"architecture.buttress",           Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"nature.log",                      Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"nature.shell",                    Kit_collision::none,          Kit_collision_source::fixed, ""},
    {"nature.blocky_tree",              Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"architecture.stone_lantern",      Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"prop.crate",                      Kit_collision::convex_hull,   Kit_collision_source::by_values, ""},
    {"prop.barrel",                     Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"ruin.broken_wall",                Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"ruin.rubble",                     Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"traversal.ladder",                Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the rungs are climbed between the rails"},
    {"harbour.pier",                    Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"harbour.mooring",                 Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"light.lamp_post",                 Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"harbour.quay_wall",               Kit_collision::triangle_mesh, Kit_collision_source::by_values, "fenders stand out of the water face, or the face leans"},
    {"industrial.pipe",                 Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"prop.pot",                        Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"prop.table",                      Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the space under the top stays open"},
    {"prop.market_stall",               Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the stall under its awning stays open"},
    {"prop.cart",                       Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"prop.well",                       Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"prop.signpost",                   Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"ruin.broken_pillar",              Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"ruin.broken_arch",                Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"light.lantern",                   Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"light.fire",                      Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"harbour.buoy",                    Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"traversal.trestle",               Kit_collision::triangle_mesh, Kit_collision_source::fixed, "an open frame under its deck"},
    {"traversal.scaffold",              Kit_collision::triangle_mesh, Kit_collision_source::fixed, "an open frame with decks inside"},
    {"architecture.tunnel",             Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the passage through it stays open"},
    {"harbour.cargo",                   Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"harbour.crane",                   Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"harbour.lighthouse",              Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"industrial.tank",                 Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"industrial.truss",                Kit_collision::triangle_mesh, Kit_collision_source::fixed, "an open lattice"},
    {"industrial.container",            Kit_collision::triangle_mesh, Kit_collision_source::by_values, "a door ajar or open lets the inside be reached"},
    {"industrial.vent",                 Kit_collision::convex_hull,   Kit_collision_source::by_values, ""},
    {"industrial.antenna",              Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"light.garland",                   Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"traversal.rope_bridge",           Kit_collision::triangle_mesh, Kit_collision_source::fixed, "the walkway between the ropes stays open"},
    {"ruin.cracked_floor",              Kit_collision::triangle_mesh, Kit_collision_source::by_values, "missing or tilted fragments leave holes and edges; a slab under 4 cm is uneven by its sunk fragments"},
    {"harbour.boat",                    Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.gear",                    Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.rack",                    Kit_collision::triangle_mesh, Kit_collision_source::fixed, "its teeth mesh with a gear"},
    {"machine.bolt",                    Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"machine.nut",                     Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"machine.spring",                  Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"machine.bearing",                 Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"machine.piston",                  Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"machine.cam",                     Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.connecting_rod",          Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.crankshaft",              Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.follower",                Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"machine.camshaft",                Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.bevel_gear",              Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.worm",                    Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.pulley",                  Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.washer",                  Kit_collision::convex_hull,   Kit_collision_source::fixed, ""},
    {"machine.cylinder",                Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
    {"machine.pillow_block",            Kit_collision::triangle_mesh, Kit_collision_source::fixed, ""},
};

// A convex hull that fills at least this share of its bounds' volume is close to a box (U54).
constexpr double c_box_share = 0.9;

// The volume of the mesh's convex hull; 0 when it has none.
[[nodiscard]] auto get_hull_volume(const GEO::Mesh& mesh) -> double
{
    GEO::Mesh hull{};
    if (!erhe::geometry::make_convex_hull(mesh, hull)) {
        return 0.0;
    }
    double volume = 0.0;
    for (const GEO::index_t facet : hull.facets) {
        const GEO::index_t corner_count = hull.facets.nb_vertices(facet);
        const GEO::vec3f   a            = erhe::geometry::get_pointf(hull.vertices, hull.facets.vertex(facet, 0));
        for (GEO::index_t i = 1; (i + 1) < corner_count; ++i) {
            const GEO::vec3f b = erhe::geometry::get_pointf(hull.vertices, hull.facets.vertex(facet, i));
            const GEO::vec3f c = erhe::geometry::get_pointf(hull.vertices, hull.facets.vertex(facet, i + 1));
            volume += glm::dot(glm::dvec3{a.x, a.y, a.z}, glm::cross(glm::dvec3{b.x, b.y, b.z}, glm::dvec3{c.x, c.y, c.z})) / 6.0;
        }
    }
    return std::abs(volume);
}

// The share of the bounds' volume the mesh's convex hull fills at the values: its outside, so neither hollows nor
// overlapping parts count.
[[nodiscard]] auto get_hull_share(const Kit_parameter_values& values) -> double
{
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    erhe::math::Aabb bounds{};
    for (const GEO::index_t vertex : mesh->vertices) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, vertex);
        bounds.include(glm::vec3{p.x, p.y, p.z});
    }
    const glm::dvec3 size{bounds.diagonal()};
    const double     bounds_volume = size.x * size.y * size.z;
    return (bounds_volume > 0.0) ? (get_hull_volume(*mesh) / bounds_volume) : 0.0;
}

[[nodiscard]] auto is_refused(const Kit_parameter_values& values) -> bool
{
    const Kit_shape_definition& shape = *values.get_shape();
    return !values.validate().empty() || (shape.check_combination && !shape.check_combination(values).empty());
}

[[nodiscard]] auto find_row(const std::string_view id) -> const Kit_collision_row*
{
    for (const Kit_collision_row& row : c_collision_rows) {
        if (id == row.id) {
            return &row;
        }
    }
    return nullptr;
}

// The value sets a shape's body is checked at: the defaults and the range corners; for a kind per values also the
// derived values its bounds are checked at, and from the defaults and corners every value of each enum, the two
// smallest of each count and each float at its minimum.
void for_each_value_set(const Kit_shape_definition& shape, const std::function<void(const Kit_parameter_values&)>& callback)
{
    const std::vector<Kit_parameter_values> sets{defaults(shape), range_corner(shape, Range_end::min), range_corner(shape, Range_end::max)};
    const auto visit = [&callback](const Kit_parameter_values& values) {
        if (!is_refused(values)) {
            callback(values);
        }
    };
    for (const Kit_parameter_values& values : sets) {
        visit(values);
    }
    if (!shape.get_collision) {
        return;
    }
    for (const Kit_parameter_values& values : bounds_value_sets(shape)) {
        visit(values);
    }
    for (const Kit_parameter& parameter : shape.parameters) {
        for (const Kit_parameter_values& set : sets) {
            if (parameter.type == Kit_parameter_type::enum_value) {
                for (const std::string& name : parameter.enum_values) {
                    Kit_parameter_values values = set;
                    values.set_enum(parameter.name, name);
                    visit(values);
                }
            } else if ((parameter.type == Kit_parameter_type::int_value) && (parameter.name != "seed")) {
                for (const int count : {parameter.int_min, parameter.int_min + 1}) {
                    Kit_parameter_values values = set;
                    values.set_int(parameter.name, count);
                    visit(values);
                }
            } else if (parameter.type == Kit_parameter_type::float_value) {
                Kit_parameter_values values = set;
                values.set_float(parameter.name, parameter.float_min);
                visit(values);
            }
        }
    }
}

TEST(Kit_collision, every_shape_states_its_body)
{
    std::map<std::string, const Kit_collision_row*> rows;
    for (const Kit_collision_row& row : c_collision_rows) {
        EXPECT_TRUE(rows.emplace(row.id, &row).second) << row.id << " has two rows";
    }
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        const std::map<std::string, const Kit_collision_row*>::const_iterator it = rows.find(shape.id);
        if (it == rows.end()) {
            ADD_FAILURE() << shape.id << " has no collision row";
            continue;
        }
        EXPECT_EQ(shape.collision, it->second->collision) << shape.id << " states " << editor::c_str(shape.collision);
        EXPECT_EQ(static_cast<bool>(shape.get_collision), (it->second->source == Kit_collision_source::by_values)) << shape.id;
        rows.erase(it);
    }
    for (const std::pair<const std::string, const Kit_collision_row*>& row : rows) {
        ADD_FAILURE() << row.first << " is no shape of the library";
    }
}

// A box body's hull fills its bounds; values whose hull fills the bounds but keep another kind need the row's reason.
TEST(Kit_collision, boxes_fill_their_bounds_and_filled_bounds_are_boxes_or_excluded)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        const Kit_collision_row* const row = find_row(shape.id);
        if ((shape.collision == Kit_collision::none) || (row == nullptr)) {
            continue;
        }
        const bool excluded = (row->not_a_box[0] != '\0');
        bool       filled   = false;
        for_each_value_set(shape, [&shape, &filled, excluded](const Kit_parameter_values& values) {
            SCOPED_TRACE(fmt::format("{} {}", shape.id, values.to_canonical_text()));
            const double        share     = get_hull_share(values);
            const Kit_collision collision = editor::get_kit_collision(values);
            EXPECT_TRUE((collision == Kit_collision::box) || (collision == shape.collision)) << editor::c_str(collision);
            if (collision == Kit_collision::box) {
                EXPECT_GE(share, c_box_share);
            } else if (share >= c_box_share) {
                filled = true;
                EXPECT_TRUE(excluded) << "the hull fills " << share << " of the bounds: state a box or why not";
            }
        });
        EXPECT_TRUE(!excluded || filled) << shape.id << " states why it is no box, but no value set fills its bounds";
    }
}

} // anonymous namespace
