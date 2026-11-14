#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/geometry.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using editor::Kit_agent;
using editor::Kit_definition;
using editor::Kit_mesh_report;
using editor::Kit_parameter;
using editor::Kit_parameter_type;
using editor::Kit_parameter_value;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::Kit_shape_library;
using editor::Kit_stairs_layout;

constexpr unsigned int rock_default_triangles = 112;
constexpr float default_module      = 1.0f;
constexpr float default_wall_height = 3.0f;

auto get_library() -> const Kit_shape_library&
{
    static const Kit_shape_library library;
    return library;
}

auto get_shape(const char* id) -> const Kit_shape_definition&
{
    const Kit_shape_definition* shape = get_library().find(id);
    EXPECT_NE(shape, nullptr) << id;
    return *shape;
}

auto defaults(const Kit_shape_definition& shape) -> Kit_parameter_values
{
    return editor::make_default_values(shape, default_module, default_wall_height);
}

auto generate(const Kit_parameter_values& values) -> std::unique_ptr<GEO::Mesh>
{
    std::unique_ptr<GEO::Mesh> mesh = std::make_unique<GEO::Mesh>(3, true);
    values.get_shape()->generator(values, *mesh);
    return mesh;
}

auto describe(const Kit_mesh_report& report) -> std::string
{
    return fmt::format(
        "{} vertices, {} facets, {} triangles, {} components; problems: {}",
        report.vertex_count, report.facet_count, report.triangle_count, report.component_count,
        report.problems.empty() ? std::string{"none"} : report.problems.front()
    );
}

enum class Range_end : unsigned int { min = 0, max };

auto c_str(const Range_end end) -> const char*
{
    return (end == Range_end::max) ? "max" : "min";
}

// One end of one parameter's range, with every other parameter at its default.
auto make_range_end(const Kit_shape_definition& shape, const std::size_t index, const Range_end range_end) -> Kit_parameter_values
{
    const bool at_max = (range_end == Range_end::max);
    Kit_parameter_values values    = defaults(shape);
    const Kit_parameter& parameter = shape.parameters[index];
    Kit_parameter_value  value     = values.get_values()[index];
    if (parameter.type == Kit_parameter_type::float_value) {
        value.float_value = at_max ? parameter.float_max : parameter.float_min;
    } else {
        value.int_value = at_max ? parameter.int_max : parameter.int_min;
    }
    values.set_value(index, value);
    return values;
}

// Every derivable parameter of the shape in the mask, derived from the seed at the default lengths.
auto all_derived(const Kit_shape_definition& shape, const int seed) -> Kit_parameter_values
{
    Kit_parameter_values values = defaults(shape);
    EXPECT_TRUE(values.set_int("seed", seed));
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        if (shape.parameters[i].derive == editor::Kit_derive::derivable) {
            values.set_value_source(i, editor::Kit_value_source::seed);
        }
    }
    values.resolve_derived(default_module, default_wall_height);
    return values;
}

TEST(Kit_shape_library, starter_set_has_the_14_shapes_of_r4_1)
{
    const std::vector<std::string> expected{
        "basic.block", "basic.wedge", "basic.cylinder", "basic.stairs", "basic.floor_slab",
        "architecture.wall", "architecture.door_wall", "architecture.pillar", "architecture.arch",
        "nature.rock", "nature.land_mass", "nature.hill", "nature.tree", "nature.bush"
    };
    const Kit_shape_library& library = get_library();
    ASSERT_EQ(library.get_shapes().size(), expected.size());
    std::set<std::string> ids;
    for (const Kit_shape_definition& shape : library.get_shapes()) {
        SCOPED_TRACE(shape.id);
        EXPECT_TRUE(ids.insert(shape.id).second);
        EXPECT_EQ(shape.id.rfind(shape.kit + ".", 0), 0u);
        EXPECT_NE(library.find_kit(shape.kit), nullptr);
        EXPECT_FALSE(shape.display_name.empty());
        EXPECT_FALSE(shape.description.empty());
        EXPECT_FALSE(shape.tags.empty());
        EXPECT_FALSE(shape.material_slots.empty());
        EXPECT_FALSE(shape.parameters.empty());
        EXPECT_TRUE(static_cast<bool>(shape.generator));
        EXPECT_EQ(library.find(shape.id), &shape);
    }
    for (const std::string& id : expected) {
        EXPECT_NE(library.find(id), nullptr) << id;
    }
    EXPECT_EQ(library.find("basic.nothing"), nullptr);
}

// R2.9: every slot name has a default and alternatives in the palette.
TEST(Kit_shape_library, every_material_slot_has_a_palette_material)
{
    const Kit_shape_library library;
    for (const Kit_shape_definition& shape : library.get_shapes()) {
        for (const std::string& slot : shape.material_slots) {
            EXPECT_NE(library.find_slot_default(slot), nullptr) << shape.id << " slot " << slot;
        }
    }
    for (const editor::Kit_slot_palette& palette : library.get_slot_palettes()) {
        SCOPED_TRACE(palette.slot);
        EXPECT_NE(library.find_palette_material(palette.default_material), nullptr);
        EXPECT_FALSE(palette.alternatives.empty());
        for (const std::string& alternative : palette.alternatives) {
            EXPECT_NE(library.find_palette_material(alternative), nullptr) << alternative;
            EXPECT_NE(alternative, palette.default_material);
        }
    }
    // The material kinds R2.9 names are in the palette.
    for (const char* name : {"Kit Stone", "Kit Wood", "Kit Plaster", "Kit Foliage", "Kit Bark", "Kit Earth", "Kit Grass"}) {
        EXPECT_NE(library.find_palette_material(name), nullptr) << name;
    }
}

// D1.11 at the defaults and at a seed-derived second parameter set: every facet names a declared slot, every
// declared slot has facets, and slot names are unique.
TEST(Kit_shape_library, every_facet_belongs_to_a_declared_slot)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        SCOPED_TRACE(shape.id);
        const std::set<std::string> unique_names{shape.material_slots.begin(), shape.material_slots.end()};
        EXPECT_EQ(unique_names.size(), shape.material_slots.size());
        for (const Kit_parameter_values& values : {defaults(shape), all_derived(shape, 7)}) {
            SCOPED_TRACE(values.to_canonical_text());
            ASSERT_EQ(values.validate(), "");
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            std::vector<uint32_t> facet_slots;
            editor::read_kit_facet_slots(*mesh, facet_slots);
            ASSERT_EQ(facet_slots.size(), mesh->facets.nb());
            std::vector<unsigned int> slot_triangles(shape.material_slots.size(), 0u);
            for (GEO::index_t f = 0, end = mesh->facets.nb(); f < end; ++f) {
                ASSERT_LT(facet_slots[f], shape.material_slots.size()) << "facet " << f;
                slot_triangles[facet_slots[f]] += mesh->facets.nb_vertices(f) - 2;
            }
            unsigned int total = 0;
            for (std::size_t slot = 0, end = shape.material_slots.size(); slot < end; ++slot) {
                EXPECT_GT(slot_triangles[slot], 0u) << "slot " << shape.material_slots[slot];
                GEO::Mesh part{3, true};
                editor::extract_kit_slot_mesh(*mesh, facet_slots, static_cast<uint32_t>(slot), part);
                EXPECT_EQ(editor::count_kit_triangles(part), slot_triangles[slot]) << "slot " << shape.material_slots[slot];
                total += editor::count_kit_triangles(part);
            }
            EXPECT_EQ(total, editor::count_kit_triangles(*mesh));
        }
    }
}

// The tree's trunk is its bark slot and its canopy its foliage slot, each a closed part.
TEST(Kit_shape_library, tree_has_bark_and_foliage_slots)
{
    const Kit_shape_definition& tree = get_shape("nature.tree");
    ASSERT_EQ(tree.material_slots, (std::vector<std::string>{"bark", "foliage"}));
    for (const std::string& canopy : {std::string{"round"}, std::string{"conical"}}) {
        SCOPED_TRACE(canopy);
        Kit_parameter_values values = defaults(tree);
        ASSERT_TRUE(values.set_enum("canopy", canopy));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        for (uint32_t slot = 0; slot < 2; ++slot) {
            GEO::Mesh part{3, true};
            editor::extract_kit_slot_mesh(*mesh, facet_slots, slot, part);
            const Kit_mesh_report report = editor::check_kit_mesh(part);
            EXPECT_TRUE(report.is_valid()) << tree.material_slots[slot] << ": " << describe(report);
            EXPECT_EQ(report.component_count, 1u) << tree.material_slots[slot];
        }
        // The trunk is the six-sided prism: 6 sides and two hexagon caps.
        GEO::Mesh bark{3, true};
        editor::extract_kit_slot_mesh(*mesh, facet_slots, 0, bark);
        EXPECT_EQ(editor::count_kit_triangles(bark), 20u);
    }
}

// R11.1 at defaults, plus R2.6: the shape's own budget and the kit ceiling.

TEST(Kit_shape_library, every_shape_passes_the_mesh_checks_at_defaults)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        SCOPED_TRACE(shape.id);
        const Kit_parameter_values values = defaults(shape);
        ASSERT_EQ(values.validate(), "");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        const Kit_definition* kit = get_library().find_kit(shape.kit);
        ASSERT_NE(kit, nullptr);
        EXPECT_LE(report.triangle_count, shape.triangle_budget);
        EXPECT_LE(shape.triangle_budget, kit->triangle_ceiling);
        EXPECT_LE(report.triangle_count, 500u);
        const unsigned int expected_components = (shape.id == "nature.tree") ? 2u : 1u;
        EXPECT_EQ(report.component_count, expected_components);
    }
}

TEST(Kit_shape_library, triangle_counts_at_defaults)
{
    const std::vector<std::pair<std::string, unsigned int>> expected{
        {"basic.block", 12}, {"basic.wedge", 8}, {"basic.cylinder", 60}, {"basic.stairs", 124}, {"basic.floor_slab", 12},
        {"architecture.wall", 12}, {"architecture.door_wall", 44}, {"architecture.pillar", 12}, {"architecture.arch", 100},
        {"nature.rock", rock_default_triangles}, {"nature.land_mass", 128}, {"nature.hill", 128}, {"nature.tree", 68}, {"nature.bush", 48}
    };
    for (const std::pair<std::string, unsigned int>& entry : expected) {
        EXPECT_EQ(editor::count_kit_triangles(*generate(defaults(get_shape(entry.first.c_str())))), entry.second) << entry.first;
    }
}

TEST(Kit_shape_checks, checks_reject_broken_meshes)
{
    const auto make_box = [](const int variant) -> std::unique_ptr<GEO::Mesh> {
        editor::Kit_mesh_builder builder;
        builder.add_box(glm::vec3{-1.0f, 0.0f, -1.0f}, glm::vec3{1.0f, 2.0f, 1.0f});
        std::unique_ptr<GEO::Mesh> mesh = std::make_unique<GEO::Mesh>(3, true);
        builder.write(*mesh);
        if (variant == 1) { // one facet reversed
            mesh->facets.flip(0);
        } else if (variant == 2) { // every facet reversed
            for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                mesh->facets.flip(f);
            }
        } else if (variant == 3) { // one facet missing
            GEO::vector<GEO::index_t> remove(mesh->facets.nb(), 0);
            remove[0] = 1;
            mesh->facets.delete_elements(remove, false);
        }
        return mesh;
    };
    EXPECT_TRUE (editor::check_kit_mesh(*make_box(0)).is_valid());
    EXPECT_FALSE(editor::check_kit_mesh(*make_box(1)).is_valid());
    EXPECT_FALSE(editor::check_kit_mesh(*make_box(2)).is_valid());
    EXPECT_FALSE(editor::check_kit_mesh(*make_box(3)).is_valid());
    EXPECT_EQ(editor::check_kit_seam_side(*make_box(0), editor::Kit_side::x_pos), "");
    EXPECT_NE(editor::check_kit_seam_side(*make_box(3), editor::Kit_side::x_pos), "");
    EXPECT_EQ(editor::check_kit_origin(*make_box(0), editor::Kit_origin_rule::base_center), "");
}

// R11.1 at both ends of every parameter range.
TEST(Kit_shape_library, every_shape_passes_the_mesh_checks_at_range_ends)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
            for (const Range_end range_end : {Range_end::min, Range_end::max}) {
                SCOPED_TRACE(fmt::format("{} {} at {}", shape.id, shape.parameters[i].name, c_str(range_end)));
                const Kit_parameter_values values = make_range_end(shape, i, range_end);
                ASSERT_EQ(values.validate(), "");
                const Kit_mesh_report report = editor::check_kit_shape(values);
                EXPECT_TRUE(report.is_valid()) << describe(report);
            }
        }
    }
}

TEST(Kit_shape_library, other_enum_values_pass_the_mesh_checks)
{
    for (const char* id : {"architecture.pillar", "nature.tree", "nature.rock"}) {
        const Kit_shape_definition& shape = get_shape(id);
        for (const Kit_parameter& parameter : shape.parameters) {
            if (parameter.type != Kit_parameter_type::enum_value) {
                continue;
            }
            for (const std::string& value_name : parameter.enum_values) {
                SCOPED_TRACE(fmt::format("{} {} = {}", id, parameter.name, value_name));
                Kit_parameter_values values = defaults(shape);
                ASSERT_TRUE(values.set_enum(parameter.name, value_name));
                const Kit_mesh_report report = editor::check_kit_shape(values);
                EXPECT_TRUE(report.is_valid()) << describe(report);
            }
        }
    }
}

// R3.2: equal values give an identical mesh.
TEST(Kit_shape_library, equal_values_give_equal_meshes)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        SCOPED_TRACE(shape.id);
        const Kit_parameter_values a = defaults(shape);
        const Kit_parameter_values b = defaults(shape);
        ASSERT_TRUE(a == b);
        EXPECT_EQ(a.to_canonical_text(), b.to_canonical_text());
        EXPECT_TRUE(editor::kit_meshes_equal(*generate(a), *generate(b)));
    }
}

// R3.7: a seed gives one mesh; another seed gives another.
TEST(Kit_shape_library, seeds_select_the_mesh)
{
    int seeded_count = 0;
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        EXPECT_TRUE(shape.find_parameter("seed").has_value()) << shape.id;
        if (shape.randomness != editor::Kit_randomness::seeded) {
            continue;
        }
        SCOPED_TRACE(shape.id);
        ++seeded_count;
        Kit_parameter_values seed_7a = defaults(shape);
        Kit_parameter_values seed_7b = defaults(shape);
        Kit_parameter_values seed_8  = defaults(shape);
        ASSERT_TRUE(seed_7a.set_int("seed", 7));
        ASSERT_TRUE(seed_7b.set_int("seed", 7));
        ASSERT_TRUE(seed_8.set_int("seed", 8));
        EXPECT_TRUE (editor::kit_meshes_equal(*generate(seed_7a), *generate(seed_7b)));
        EXPECT_FALSE(editor::kit_meshes_equal(*generate(seed_7a), *generate(seed_8)));
        EXPECT_NE(seed_7a.to_canonical_text(), seed_8.to_canonical_text());
    }
    EXPECT_EQ(seeded_count, 5);
}

TEST(Kit_parameter_values, canonical_text_round_trips)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        std::vector<Kit_parameter_values> samples{defaults(shape), editor::make_default_values(shape, 0.75f, 2.6f), all_derived(shape, 17)};
        for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
            samples.push_back(make_range_end(shape, i, Range_end::min));
            samples.push_back(make_range_end(shape, i, Range_end::max));
        }
        for (const Kit_parameter_values& values : samples) {
            const std::string text = values.to_canonical_text();
            SCOPED_TRACE(fmt::format("{} {}", shape.id, text));
            Kit_parameter_values parsed;
            ASSERT_EQ(editor::parse_canonical_text(shape, text, parsed), "");
            EXPECT_TRUE(parsed == values);
            EXPECT_EQ(parsed.to_canonical_text(), text);
        }
    }
}

TEST(Kit_parameter_values, canonical_text_form)
{
    const Kit_shape_definition& wall = get_shape("architecture.wall");
    EXPECT_EQ(defaults(wall).to_canonical_text(), R"({"length":"0x1p+2","height":"0x1.8p+1","thickness":"0x1p-2","seed":1})");

    const Kit_shape_definition& pillar = get_shape("architecture.pillar");
    EXPECT_EQ(defaults(pillar).to_canonical_text(), R"({"profile":"square","width":"0x1p-1","height":"0x1.8p+1","segments":12,"seed":1})");

    EXPECT_EQ(editor::format_hexfloat(0.1f), "0x1.99999ap-4");
    EXPECT_EQ(editor::format_hexfloat(-2.0f), "-0x1p+1");
    float parsed = 0.0f;
    ASSERT_TRUE(editor::parse_hexfloat("0x1.99999ap-4", parsed));
    EXPECT_EQ(parsed, 0.1f);
    EXPECT_FALSE(editor::parse_hexfloat("0.1", parsed));
}

TEST(Kit_parameter_values, canonical_text_rejects_malformed_input)
{
    const Kit_shape_definition& wall = get_shape("architecture.wall");
    Kit_parameter_values values;
    EXPECT_NE(editor::parse_canonical_text(wall, R"({"length":"0x1p+2","height":"0x1.8p+1","seed":1})", values), "");
    EXPECT_NE(editor::parse_canonical_text(wall, R"({"length":"0x1p+2","height":"0x1.8p+1","thickness":"0x1p-2","seed":1,"color":1})", values), "");
    EXPECT_NE(editor::parse_canonical_text(wall, R"({"length":4,"height":"0x1.8p+1","thickness":"0x1p-2","seed":1})", values), "");
    EXPECT_NE(editor::parse_canonical_text(wall, R"({"length":"0x1p+2","height":"0x1.8p+1","thickness":"0x1p-2","seed":1} x)", values), "");
    // A text from before the seed and the mask does not read (no migration).
    EXPECT_NE(editor::parse_canonical_text(wall, R"({"length":"0x1p+2","height":"0x1.8p+1","thickness":"0x1p-2"})", values), "");
    EXPECT_NE(editor::parse_canonical_text(wall, R"({"length":"0x1p+2","height":"0x1.8p+1","thickness":"0x1p-2","seed":1,"derive":["colour"]})", values), "");
    EXPECT_NE(editor::parse_canonical_text(wall, R"({"length":"0x1p+2","height":"0x1.8p+1","thickness":"0x1p-2","seed":1,"derive":["length","length"]})", values), "");
    EXPECT_NE(editor::parse_canonical_text(wall, R"({"length":"0x1p+2","height":"0x1.8p+1","thickness":"0x1p-2","seed":1,"derive":"length"})", values), "");
    EXPECT_EQ(editor::parse_canonical_text(wall, R"( { "thickness" : "0x1p-2", "seed": 1, "length":"0x1p+2", "height":"0x1.8p+1" } )", values), "");
    EXPECT_TRUE(values == defaults(wall));
}

// R3.6: an out-of-range value is rejected with a message naming the parameter and its range.
TEST(Kit_parameter_values, out_of_range_message_names_parameter_and_range)
{
    const Kit_shape_definition& stairs = get_shape("basic.stairs");
    Kit_parameter_values values = defaults(stairs);
    ASSERT_TRUE(values.set_float("rise", 20.0f));
    const std::string message = values.validate();
    EXPECT_NE(message.find("basic.stairs"), std::string::npos) << message;
    EXPECT_NE(message.find("'rise'"),       std::string::npos) << message;
    EXPECT_NE(message.find("0.1 - 16 m"),   std::string::npos) << message;

    Kit_parameter_values segments = defaults(get_shape("basic.cylinder"));
    ASSERT_TRUE(segments.set_int("segments", 2));
    const std::string segments_message = segments.validate();
    EXPECT_NE(segments_message.find("'segments'"),  std::string::npos) << segments_message;
    EXPECT_NE(segments_message.find("3 - 64 count"), std::string::npos) << segments_message;

    Kit_parameter_values too_many_steps = defaults(stairs);
    ASSERT_TRUE(too_many_steps.set_float("rise", 16.0f));
    ASSERT_TRUE(too_many_steps.set_float("step_rise_limit", 0.05f));
    const std::string steps_message = too_many_steps.validate();
    EXPECT_NE(steps_message.find("'rise'"),            std::string::npos) << steps_message;
    EXPECT_NE(steps_message.find("'step_rise_limit'"), std::string::npos) << steps_message;

    Kit_parameter_values not_finite = defaults(get_shape("basic.block"));
    ASSERT_TRUE(not_finite.set_float("width", std::numeric_limits<float>::quiet_NaN()));
    EXPECT_NE(not_finite.validate().find("'width'"), std::string::npos);

    EXPECT_FALSE(values.set_float("no_such_parameter", 1.0f));
    EXPECT_FALSE(values.set_int("rise", 1));
}

// R2.1: defaults follow the level's module and wall height.
TEST(Kit_parameter_values, defaults_resolve_against_module_and_wall_height)
{
    const Kit_shape_definition& wall   = get_shape("architecture.wall");
    const Kit_parameter_values  values = editor::make_default_values(wall, 2.0f, 4.0f);
    EXPECT_EQ(values.get_float("length"),    8.0f);
    EXPECT_EQ(values.get_float("height"),    4.0f);
    EXPECT_EQ(values.get_float("thickness"), 0.25f);
}

// R2.10 on parameters, without a navigation mesh: the default agent walks the walkable shapes.
TEST(Kit_shape_library, walkable_shapes_suit_the_default_agent_at_defaults)
{
    const Kit_agent agent{};
    int walkability_count = 0;
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        if (!shape.check_walkability) {
            EXPECT_FALSE(shape.has_tag("opening")) << shape.id;
            continue;
        }
        ++walkability_count;
        EXPECT_EQ(shape.check_walkability(defaults(shape), agent), "") << shape.id;
    }
    EXPECT_GE(walkability_count, 6);

    const Kit_parameter_values stairs = defaults(get_shape("basic.stairs"));
    const Kit_stairs_layout    layout = editor::get_stairs_layout(stairs);
    EXPECT_EQ(layout.step_count, 15);
    EXPECT_LE(layout.step_rise, agent.max_climb);
    EXPECT_LE(layout.step_rise, stairs.get_float("step_rise_limit"));
    EXPECT_NEAR(layout.tread_depth, 0.3f, 1.0e-5f);
    EXPECT_NEAR(layout.run, 4.5f, 1.0e-4f);
    EXPECT_LT(layout.slope_degrees, agent.max_slope_degrees);

    const Kit_parameter_values door = defaults(get_shape("architecture.door_wall"));
    EXPECT_GE(door.get_float("opening_width"),  (2.0f * agent.radius) + editor::kit_opening_margin);
    EXPECT_GE(door.get_float("opening_height"), agent.height + editor::kit_opening_margin);

    const Kit_parameter_values arch = defaults(get_shape("architecture.arch"));
    EXPECT_GE(editor::get_arch_clear_width(arch, agent.height + editor::kit_opening_margin), (2.0f * agent.radius) + editor::kit_opening_margin);

    // A step rise limit above the climb makes stairs the agent cannot walk.
    Kit_parameter_values steep = stairs;
    ASSERT_TRUE(steep.set_float("step_rise_limit", 0.6f));
    EXPECT_NE(get_shape("basic.stairs").check_walkability(steep, agent), "");

    Kit_parameter_values low_door = door;
    ASSERT_TRUE(low_door.set_float("opening_height", 1.9f));
    EXPECT_NE(get_shape("architecture.door_wall").check_walkability(low_door, agent), "");
}

// D1.7: stairs and the wedge rise toward -Z from their front edge at +Z.
TEST(Kit_shape_library, stairs_and_wedge_rise_toward_minus_z)
{
    for (const char* id : {"basic.stairs", "basic.wedge"}) {
        SCOPED_TRACE(id);
        const std::unique_ptr<GEO::Mesh> mesh = generate(defaults(get_shape(id)));
        float max_y = 0.0f;
        float max_z = 0.0f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
            max_y = std::max(max_y, p.y);
            max_z = std::max(max_z, p.z);
        }
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
            if (p.z == max_z) {
                EXPECT_LT(p.y, 0.5f * max_y);
            }
            if (p.y == max_y) {
                EXPECT_LT(p.z, 0.0f);
            }
        }
    }
}

// D1.8: a derived value lies in its derive range (on its step), inside the valid range, for every seed.
TEST(Kit_derive, derived_values_lie_in_their_ranges)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        for (int seed = 0; seed < 200; ++seed) {
            const Kit_parameter_values values = all_derived(shape, seed);
            SCOPED_TRACE(fmt::format("{} seed {}: {}", shape.id, seed, values.to_canonical_text()));
            ASSERT_EQ(values.validate(), "");
            for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
                const Kit_parameter& parameter = shape.parameters[i];
                if (parameter.derive != editor::Kit_derive::derivable) {
                    continue;
                }
                const Kit_parameter_value value = values.get_values()[i];
                if (parameter.type == Kit_parameter_type::float_value) {
                    const editor::Kit_derive_range range = parameter.resolve_derive_range(default_module, default_wall_height);
                    EXPECT_GE(value.float_value, range.min_value) << parameter.name;
                    EXPECT_LE(value.float_value, range.max_value) << parameter.name;
                    const float steps = (value.float_value - range.min_value) / range.step_value;
                    EXPECT_NEAR(steps, std::round(steps), 1.0e-3f) << parameter.name;
                } else {
                    EXPECT_GE(value.int_value, parameter.derive_int_min) << parameter.name;
                    EXPECT_LE(value.int_value, parameter.derive_int_max) << parameter.name;
                }
            }
        }
    }
}

// D1.8: equal seeds derive equal values; across seeds every derivable parameter takes several values.
TEST(Kit_derive, derived_values_follow_the_seed)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        SCOPED_TRACE(shape.id);
        EXPECT_TRUE(all_derived(shape, 5) == all_derived(shape, 5));
        EXPECT_EQ(all_derived(shape, 5).to_canonical_text(), all_derived(shape, 5).to_canonical_text());
        for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
            if (shape.parameters[i].derive != editor::Kit_derive::derivable) {
                continue;
            }
            std::set<std::pair<float, int>> seen;
            for (int seed = 0; seed < 50; ++seed) {
                const Kit_parameter_value value = all_derived(shape, seed).get_values()[i];
                seen.insert(std::make_pair(value.float_value, value.int_value));
            }
            EXPECT_GE(seen.size(), 2u) << shape.parameters[i].name;
        }
    }
}

// D1.8: a derived value depends on the seed, the name and the derive range only, not on the other parameters.
TEST(Kit_derive, derived_value_ignores_the_other_parameters)
{
    const Kit_shape_definition& tree = get_shape("nature.tree");
    Kit_shape_definition        grown = tree;
    grown.parameters.insert(grown.parameters.begin(), editor::make_ratio_parameter("leafiness", "A parameter added later", 0.5f));
    for (int seed = 0; seed < 20; ++seed) {
        Kit_parameter_values before = editor::make_default_values(tree, default_module, default_wall_height);
        Kit_parameter_values after  = editor::make_default_values(grown, default_module, default_wall_height);
        ASSERT_TRUE(before.set_int("seed", seed));
        ASSERT_TRUE(after.set_int("seed", seed));
        before.resolve_derived(default_module, default_wall_height);
        after.resolve_derived(default_module, default_wall_height);
        EXPECT_EQ(before.get_float("trunk_height"), after.get_float("trunk_height"));
        EXPECT_EQ(before.get_float("canopy_size"),  after.get_float("canopy_size"));
    }
}

// Derive ranges stated in modules or wall heights follow the level's lengths, like the defaults.
TEST(Kit_derive, derive_ranges_follow_the_level_lengths)
{
    const Kit_parameter& width = get_shape("basic.block").parameters[0];
    const editor::Kit_derive_range at_1 = width.resolve_derive_range(1.0f, 3.0f);
    const editor::Kit_derive_range at_2 = width.resolve_derive_range(2.0f, 3.0f);
    EXPECT_EQ(at_1.min_value, 0.5f);
    EXPECT_EQ(at_1.max_value, 3.0f);
    EXPECT_EQ(at_2.min_value, 1.0f);
    EXPECT_EQ(at_2.max_value, 6.0f);
    EXPECT_EQ(at_2.step_value, 0.5f);
}

// R2.10 for every seed: the derive ranges of walkable shapes keep them walkable.
TEST(Kit_derive, walkable_shapes_stay_walkable_at_every_seed)
{
    const Kit_agent agent{};
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        if (!shape.check_walkability) {
            continue;
        }
        for (int seed = 0; seed < 300; ++seed) {
            const Kit_parameter_values values = all_derived(shape, seed);
            EXPECT_EQ(shape.check_walkability(values, agent), "") << shape.id << " " << values.to_canonical_text();
        }
    }
}

TEST(Kit_derive, mask_round_trips_and_refuses_fixed_parameters)
{
    const Kit_shape_definition& rock     = get_shape("nature.rock");
    const Kit_parameter_values  defaults_values = defaults(rock);
    EXPECT_EQ(defaults_values.get_value_source(rock.find_parameter("size").value()), editor::Kit_value_source::seed);
    EXPECT_EQ(defaults_values.get_value_source(rock.find_parameter("form").value()), editor::Kit_value_source::explicit_value);
    const std::string text = defaults_values.to_canonical_text();
    EXPECT_NE(text.find(R"(,"derive":["size"]})"), std::string::npos) << text;

    Kit_parameter_values masked = all_derived(rock, 3);
    Kit_parameter_values parsed;
    ASSERT_EQ(editor::parse_canonical_text(rock, masked.to_canonical_text(), parsed), "");
    EXPECT_TRUE(parsed == masked);

    // A mask is part of equality and of the text, so of the mesh key.
    Kit_parameter_values unmasked = masked;
    unmasked.set_value_source(rock.find_parameter("size").value(), editor::Kit_value_source::explicit_value);
    EXPECT_FALSE(unmasked == masked);
    EXPECT_NE(unmasked.to_canonical_text(), masked.to_canonical_text());

    Kit_parameter_values seed_masked = defaults_values;
    seed_masked.set_value_source(rock.find_parameter("seed").value(), editor::Kit_value_source::seed);
    EXPECT_NE(seed_masked.validate().find("'seed' cannot be derived"), std::string::npos) << seed_masked.validate();
    const Kit_shape_definition& stairs = get_shape("basic.stairs");
    Kit_parameter_values limit_masked = defaults(stairs);
    limit_masked.set_value_source(stairs.find_parameter("step_rise_limit").value(), editor::Kit_value_source::seed);
    EXPECT_NE(limit_masked.validate(), "");
}

TEST(Kit_derive, next_seed_is_another_seed_in_range)
{
    std::set<int> seeds;
    int seed = 1;
    for (int i = 0; i < 100; ++i) {
        const int next = editor::kit_next_seed(seed);
        EXPECT_NE(next, seed);
        EXPECT_GE(next, 0);
        EXPECT_LE(next, editor::kit_seed_max);
        EXPECT_EQ(editor::kit_next_seed(seed), next);
        seeds.insert(next);
        seed = next;
    }
    EXPECT_EQ(seeds.size(), 100u);
}

// D1.9: every rock form at 0, 1 and 2 bevels passes the mesh checks and the kit's triangle ceiling.
TEST(Kit_rock, every_form_and_bevel_count_passes_the_checks)
{
    const Kit_shape_definition& rock = get_shape("nature.rock");
    for (const std::string& form : rock.parameters[rock.find_parameter("form").value()].enum_values) {
        for (int bevels = 0; bevels <= 2; ++bevels) {
            unsigned int max_triangles = 0;
            for (int seed = 0; seed < 40; ++seed) {
                for (const float roughness : {0.0f, 0.3f, 1.0f}) {
                    Kit_parameter_values values = defaults(rock);
                    ASSERT_TRUE(values.set_enum("form", form));
                    ASSERT_TRUE(values.set_int("bevels", bevels));
                    ASSERT_TRUE(values.set_int("seed", seed));
                    ASSERT_TRUE(values.set_float("roughness", roughness));
                    values.resolve_derived(default_module, default_wall_height);
                    SCOPED_TRACE(values.to_canonical_text());
                    const Kit_mesh_report report = editor::check_kit_shape(values);
                    EXPECT_TRUE(report.is_valid()) << describe(report);
                    EXPECT_LE(report.triangle_count, 500u);
                    EXPECT_EQ(report.component_count, 1u);
                    max_triangles = std::max(max_triangles, report.triangle_count);
                }
            }
            Kit_parameter_values values = defaults(rock);
            ASSERT_TRUE(values.set_enum("form", form));
            ASSERT_TRUE(values.set_int("bevels", bevels));
            const unsigned int at_defaults = editor::count_kit_triangles(*generate(values));
            testing::Test::RecordProperty(fmt::format("rock_{}_bevels_{}", form, bevels), fmt::format("{} at the default seed, at most {}", at_defaults, max_triangles));
        }
    }
}

} // anonymous namespace
