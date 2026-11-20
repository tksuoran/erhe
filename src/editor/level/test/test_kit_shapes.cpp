#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"
#include "kit_shape_test_support.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_math/aabb.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace editor::kit_test;

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

TEST(Kit_shape_library, library_has_the_starter_set_and_the_more_shapes)
{
    // The shapes each batch states (kit_shape_rows_<batch>.cpp).
    const std::vector<std::string> expected = get_union(&Kit_shape_rows::shapes);
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
    // Q1 of doc/plans/level_kit_more_shapes.md and the water slot of P3.
    for (const char* slot : {"sand", "roof", "water"}) {
        EXPECT_NE(library.find_slot_default(slot), nullptr) << slot;
    }
    EXPECT_EQ(library.find_slot_default("sand")->name, "Kit Sand");
    EXPECT_EQ(library.find_slot_default("roof")->name, "Kit Roof Tile");
    const std::vector<std::string>& stone = library.find_slot_palette("stone")->alternatives;
    const std::vector<std::string>& earth = library.find_slot_palette("earth")->alternatives;
    EXPECT_NE(std::find(stone.begin(), stone.end(), "Kit Shell"), stone.end());
    EXPECT_NE(std::find(earth.begin(), earth.end(), "Kit Sand"), earth.end());
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

// R11.1 at defaults, plus R2.6: the shape's own budget and the kit ceiling.

TEST(Kit_shape_library, every_shape_passes_the_mesh_checks_at_defaults)
{
    const std::vector<Kit_component_row> components = get_union(&Kit_shape_rows::components);
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
        EXPECT_EQ(kit->triangle_ceiling, 2000u);
        // The batch's row of the shape, else one component.
        Kit_component_row expected_components{};
        const std::vector<Kit_component_row>::const_iterator row = std::find_if(
            components.begin(), components.end(), [&shape](const Kit_component_row& candidate) { return candidate.id == shape.id; }
        );
        if (row != components.end()) {
            expected_components = *row;
        }
        if (expected_components.rule == Kit_component_rule::more_than) {
            EXPECT_GT(report.component_count, expected_components.count);
        } else {
            EXPECT_EQ(report.component_count, expected_components.count);
        }
    }
}

TEST(Kit_shape_library, triangle_counts_at_defaults)
{
    const std::vector<std::pair<std::string, unsigned int>> expected = get_union(&Kit_shape_rows::triangle_counts);
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
                if (shape.check_combination && !shape.check_combination(values).empty()) {
                    continue; // a combination the shape refuses (the dune's least length against its default width) is no value of it
                }
                ASSERT_EQ(values.validate(), "");
                const Kit_mesh_report report = editor::check_kit_shape(values);
                EXPECT_TRUE(report.is_valid()) << describe(report);
            }
        }
    }
}

TEST(Kit_shape_library, other_enum_values_pass_the_mesh_checks)
{
    for (const std::string& enum_shape_id : get_union(&Kit_shape_rows::enum_shapes)) {
        const char* const           id    = enum_shape_id.c_str();
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

// R3.7: a seed gives one mesh; another seed gives another. A shape of Kit_randomness::none draws nothing from its
// seed: its explicit values give one mesh at every seed, so its seed sweeps are one seed (sweep_seeds()).
TEST(Kit_shape_library, seeds_select_the_mesh)
{
    std::vector<std::string> seeded;
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        EXPECT_TRUE(shape.find_parameter("seed").has_value()) << shape.id;
        if (shape.randomness != editor::Kit_randomness::seeded) {
            SCOPED_TRACE(shape.id);
            EXPECT_TRUE(editor::kit_meshes_equal(*generate(explicit_values(shape, 7)), *generate(explicit_values(shape, 8))));
            continue;
        }
        SCOPED_TRACE(shape.id);
        seeded.push_back(shape.id);
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
    std::vector<std::string> expected = get_union(&Kit_shape_rows::seeded_shapes);
    std::sort(seeded.begin(), seeded.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(seeded, expected);
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
    ASSERT_TRUE(editor::parse_hexfloat("-0x1p+1", parsed));
    EXPECT_EQ(parsed, -2.0f);
    EXPECT_FALSE(editor::parse_hexfloat("0x", parsed));
    EXPECT_FALSE(editor::parse_hexfloat("0x1p+2 ", parsed));
    EXPECT_FALSE(editor::parse_hexfloat("0x 1p+2", parsed));
    EXPECT_FALSE(editor::parse_hexfloat("0x-1p+2", parsed));
    EXPECT_FALSE(editor::parse_hexfloat("0x0x1p+2", parsed));
    EXPECT_FALSE(editor::parse_hexfloat("0xinf", parsed));
    EXPECT_FALSE(editor::parse_hexfloat("0x1p+200", parsed));
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

// R2.6: the kit's ceiling binds at every in-range value; the all-maximum corner is the most a shape makes, but for the
// helical bolt, whose most lies at 35 and 38 turns (its worst_cases row).
TEST(Kit_shape_library, every_shape_keeps_the_ceiling_at_the_range_maxima)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        const Kit_parameter_values values = range_corner(shape, Range_end::max);
        SCOPED_TRACE(fmt::format("{} {}", shape.id, values.to_canonical_text()));
        ASSERT_EQ(values.validate(), "");
        EXPECT_LE(editor::count_kit_triangles(*generate(values)), get_library().find_kit(shape.kit)->triangle_ceiling);
    }
}

// R11.1 at the corners of the ranges: every parameter at its minimum, every one at its maximum.
TEST(Kit_shape_library, every_shape_passes_the_mesh_checks_at_the_range_corners)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        for (const Range_end range_end : {Range_end::min, Range_end::max}) {
            const Kit_parameter_values values = range_corner(shape, range_end);
            SCOPED_TRACE(fmt::format("{} {} {}", shape.id, c_str(range_end), values.to_canonical_text()));
            if (shape.check_combination && !shape.check_combination(values).empty()) {
                continue; // a combination the shape refuses is no value of it
            }
            const Kit_mesh_report report = editor::check_kit_shape(values);
            EXPECT_TRUE(report.is_valid()) << describe(report);
        }
    }
}

// The palm at the corners of its size parameters for every species and several seeds: no leaf below the base.
TEST(Kit_palm, every_species_passes_the_checks_at_the_size_corners)
{
    const Kit_shape_definition& palm = get_shape("nature.palm");
    const Kit_parameter& height = palm.parameters[palm.find_parameter("height").value()];
    const Kit_parameter& frond  = palm.parameters[palm.find_parameter("frond_length").value()];
    const Kit_parameter& bend   = palm.parameters[palm.find_parameter("bend").value()];
    const Kit_parameter& droop  = palm.parameters[palm.find_parameter("droop").value()];
    const Kit_parameter& fronds = palm.parameters[palm.find_parameter("fronds").value()];
    class Corner
    {
    public:
        float height;
        float frond_length;
        float bend;
        float droop;
        int   fronds;
    };
    const std::vector<Corner> corners{
        {height.float_min, frond.float_max, bend.float_max, droop.float_max, fronds.int_max},
        {height.float_min, frond.float_max, bend.float_min, droop.float_max, fronds.int_min},
        {height.float_max, frond.float_min, bend.float_min, droop.float_min, fronds.int_min},
        {height.float_max, frond.float_max, bend.float_max, droop.float_max, fronds.int_max},
        {height.float_min, frond.float_min, bend.float_max, droop.float_min, fronds.int_max}
    };
    for (const std::string& species : palm.parameters[palm.find_parameter("species").value()].enum_values) {
        for (const Corner& corner : corners) {
            for (const int seed : sweep_seeds(palm)) {
                Kit_parameter_values values = explicit_values(palm, seed);
                ASSERT_TRUE(values.set_enum("species", species));
                ASSERT_TRUE(values.set_float("height", corner.height));
                ASSERT_TRUE(values.set_float("frond_length", corner.frond_length));
                ASSERT_TRUE(values.set_float("bend", corner.bend));
                ASSERT_TRUE(values.set_float("droop", corner.droop));
                ASSERT_TRUE(values.set_int("fronds", corner.fronds));
                SCOPED_TRACE(values.to_canonical_text());
                const Kit_mesh_report report = editor::check_kit_shape(values);
                EXPECT_TRUE(report.is_valid()) << describe(report);
                EXPECT_EQ(report.bounds_min.y, 0.0f);
                EXPECT_LE(report.triangle_count, get_library().find_kit("nature")->triangle_ceiling);
            }
        }
    }
}

// The cliff's "top" connector lies on the walkable top, between the back and the rim, also for a cliff that leans back.
TEST(Kit_cliff, top_connector_lies_on_the_walkable_top)
{
    const Kit_shape_definition& cliff = get_shape("nature.cliff");
    for (const float overhang : {-0.25f, 0.0f, 0.25f}) {
        Kit_parameter_values values = explicit_values(cliff, 1);
        ASSERT_TRUE(values.set_float("depth", 4.0f));
        ASSERT_TRUE(values.set_float("height", 8.0f));
        ASSERT_TRUE(values.set_float("overhang", overhang));
        ASSERT_TRUE(values.set_int("ledges", 3));
        std::vector<editor::Kit_connector> connectors;
        cliff.get_connectors(values, connectors);
        const auto top = std::find_if(connectors.begin(), connectors.end(), [](const editor::Kit_connector& c) { return std::string_view{c.name} == "top"; });
        ASSERT_NE(top, connectors.end());
        // The rim: the grass facets' front edge; the top connector lies between the back and the rim's nearest point.
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        float rim = std::numeric_limits<float>::max();
        for (GEO::index_t facet = 0; facet < mesh->facets.nb(); ++facet) {
            if (facet_slots[facet] != 1) {
                continue;
            }
            for (GEO::index_t corner = 0; corner < mesh->facets.nb_vertices(facet); ++corner) {
                const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, mesh->facets.vertex(facet, corner));
                if (p.z > -2.0f + 1.0e-4f) {
                    rim = std::min(rim, p.z);
                }
            }
        }
        SCOPED_TRACE(fmt::format("overhang {}: top z {}, rim {}", overhang, top->position.z, rim));
        EXPECT_EQ(top->position.y, 8.0f);
        EXPECT_GT(top->position.z, -2.0f);
        EXPECT_LT(top->position.z, rim);
    }
}

// Pins every shape's generated mesh across code versions and platforms: vertex, facet and corner counts and the
// facet count of each slot exactly; bounds, centroid and surface area within a tolerance (doc/editor/level_tools.md).
TEST(Kit_shape_library, generated_meshes_are_pinned)
{
    constexpr float position_tolerance = 1.0e-4f; // meters
    constexpr float area_tolerance     = 1.0e-4f; // relative
    const std::vector<Mesh_pin> expected = get_union(&Kit_shape_rows::pins);
    std::map<std::string, const Mesh_pin*> expected_by_name;
    for (const Mesh_pin& pin : expected) {
        EXPECT_TRUE(expected_by_name.emplace(pin.name, &pin).second) << "two pin rows name " << pin.name;
    }
    std::vector<Mesh_pin> actual;
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        const std::vector<int> seeds = (shape.randomness == editor::Kit_randomness::seeded) ? std::vector<int>{1, 3, 17} : std::vector<int>{1};
        for (const int seed : seeds) {
            Kit_parameter_values values = defaults(shape);
            ASSERT_TRUE(values.set_int("seed", seed));
            values.resolve_derived(default_module, default_wall_height);
            actual.push_back(make_mesh_pin(fmt::format("{} {}", shape.id, seed), *generate(values)));
        }
    }
    std::string table;
    for (const Mesh_pin& pin : actual) {
        table += fmt::format(
            "        {{\"{}\", {}, {}, {}, 0x{:016x}ull, {{{:.5f}f, {:.5f}f, {:.5f}f}}, {{{:.5f}f, {:.5f}f, {:.5f}f}}, {{{:.5f}f, {:.5f}f, {:.5f}f}}, {:.5f}f}},\n",
            pin.name, pin.vertices, pin.facets, pin.corners, pin.slots,
            pin.min[0], pin.min[1], pin.min[2], pin.max[0], pin.max[1], pin.max[2],
            pin.centroid[0], pin.centroid[1], pin.centroid[2], pin.area
        );
    }
    std::set<std::string> matched;
    for (const Mesh_pin& pin : actual) {
        SCOPED_TRACE(pin.name);
        const std::map<std::string, const Mesh_pin*>::const_iterator found = expected_by_name.find(pin.name);
        if (found == expected_by_name.end()) {
            ADD_FAILURE() << "no pin row for " << pin.name;
            continue;
        }
        matched.insert(pin.name);
        const Mesh_pin& row = *found->second;
        EXPECT_EQ(pin.vertices, row.vertices);
        EXPECT_EQ(pin.facets,   row.facets);
        EXPECT_EQ(pin.corners,  row.corners);
        EXPECT_EQ(pin.slots,    row.slots);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            EXPECT_NEAR(pin.min[axis],      row.min[axis],      position_tolerance);
            EXPECT_NEAR(pin.max[axis],      row.max[axis],      position_tolerance);
            EXPECT_NEAR(pin.centroid[axis], row.centroid[axis], position_tolerance);
        }
        EXPECT_NEAR(pin.area, row.area, area_tolerance * std::max(1.0f, row.area));
    }
    for (const Mesh_pin& row : expected) {
        EXPECT_TRUE(matched.contains(row.name)) << "the pin row " << row.name << " names no generated shape and seed";
    }
    if (testing::Test::HasFailure()) {
        ADD_FAILURE() << "actual pin rows, in library order (each goes in the pins of its batch's kit_shape_rows_<batch>.cpp):\n" << table;
    }
}

// R2.5: a stated surface may be open; a solid may not.
TEST(Kit_shape_checks, surfaces_may_be_open)
{
    editor::Kit_mesh_builder builder;
    const uint32_t a = builder.add_vertex(glm::vec3{-1.0f, 0.0f, -1.0f});
    const uint32_t b = builder.add_vertex(glm::vec3{-1.0f, 0.0f,  0.0f});
    const uint32_t c = builder.add_vertex(glm::vec3{ 1.0f, 0.0f,  0.0f});
    const uint32_t d = builder.add_vertex(glm::vec3{ 1.0f, 0.0f, -1.0f});
    const uint32_t e = builder.add_vertex(glm::vec3{-1.0f, 0.0f,  1.0f});
    const uint32_t f = builder.add_vertex(glm::vec3{ 1.0f, 0.0f,  1.0f});
    builder.add_quad(a, b, c, d);
    builder.add_quad(b, e, f, c);
    GEO::Mesh mesh{3, true};
    builder.write(mesh);
    EXPECT_TRUE (editor::check_kit_mesh(mesh, editor::Kit_mesh_kind::surface).is_valid()) << describe(editor::check_kit_mesh(mesh, editor::Kit_mesh_kind::surface));
    EXPECT_FALSE(editor::check_kit_mesh(mesh, editor::Kit_mesh_kind::solid).is_valid());
    mesh.facets.flip(1);
    EXPECT_FALSE(editor::check_kit_mesh(mesh, editor::Kit_mesh_kind::surface).is_valid());
}

// P5: the water is an open surface facing up, without a body; its subdivisions make its quads.
TEST(Kit_water, is_an_open_surface_without_a_body)
{
    const Kit_shape_definition& water = get_shape("nature.water");
    EXPECT_EQ(water.collision, editor::Kit_collision::none);
    EXPECT_EQ(water.mesh_kind, editor::Kit_mesh_kind::surface);
    EXPECT_EQ(water.material_slots, (std::vector<std::string>{"water"}));
    for (const int subdivisions : {1, 3, 32}) {
        Kit_parameter_values values = defaults(water);
        ASSERT_TRUE(values.set_int("subdivisions", subdivisions));
        ASSERT_TRUE(values.set_float("width", 5.0f));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        EXPECT_EQ(editor::count_kit_triangles(*mesh), static_cast<unsigned int>(2 * subdivisions * subdivisions));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_FALSE(editor::check_kit_mesh(*mesh, editor::Kit_mesh_kind::solid).is_valid());
        EXPECT_EQ(report.bounds_min, (glm::vec3{-2.5f, 0.0f, -4.0f}));
        EXPECT_EQ(report.bounds_max, (glm::vec3{ 2.5f, 0.0f,  4.0f}));
        for (GEO::index_t facet = 0; facet < mesh->facets.nb(); ++facet) {
            const GEO::vec3f p0 = erhe::geometry::get_pointf(mesh->vertices, mesh->facets.vertex(facet, 0));
            const GEO::vec3f p1 = erhe::geometry::get_pointf(mesh->vertices, mesh->facets.vertex(facet, 1));
            const GEO::vec3f p2 = erhe::geometry::get_pointf(mesh->vertices, mesh->facets.vertex(facet, 2));
            const glm::vec3 normal = glm::cross(glm::vec3{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z}, glm::vec3{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z});
            EXPECT_GT(normal.y, 0.0f) << "facet " << facet;
        }
    }
}

// Every palm species over seeds: the checks, the ceiling, both slots, and nothing below the trunk's base.
TEST(Kit_palm, every_species_passes_the_checks_over_seeds)
{
    const Kit_shape_definition& palm = get_shape("nature.palm");
    const unsigned int ceiling = get_library().find_kit("nature")->triangle_ceiling;
    for (const std::string& species : palm.parameters[palm.find_parameter("species").value()].enum_values) {
        unsigned int max_triangles = 0;
        for (const int seed : derive_seeds()) {
            Kit_parameter_values values = all_derived(palm, seed);
            ASSERT_TRUE(values.set_enum("species", species));
            SCOPED_TRACE(values.to_canonical_text());
            const Kit_mesh_report report = editor::check_kit_shape(values);
            EXPECT_TRUE(report.is_valid()) << describe(report);
            EXPECT_LE(report.triangle_count, ceiling);
            EXPECT_EQ(report.bounds_min.y, 0.0f);
            // The crown is at the height; upright fronds reach above it by at most their length.
            EXPECT_GE(report.bounds_max.y, 0.9f * values.get_float("height"));
            EXPECT_LE(report.bounds_max.y, values.get_float("height") + values.get_float("frond_length"));
            max_triangles = std::max(max_triangles, report.triangle_count);
        }
        testing::Test::RecordProperty(fmt::format("palm_{}", species), fmt::format("at most {} triangles over derived seeds 0 - 23", max_triangles));
    }
}

// The cliff's ends depend on the values, not the seed, so segments of equal values join (R2.4); its top is flat grass.
TEST(Kit_cliff, ends_follow_the_values_and_the_top_is_flat)
{
    const Kit_shape_definition& cliff = get_shape("nature.cliff");
    for (const std::string& strata : {std::string{"massive"}, std::string{"layered"}, std::string{"blocky"}}) {
        SCOPED_TRACE(strata);
        Kit_parameter_values a = explicit_values(cliff, 3);
        Kit_parameter_values b = explicit_values(cliff, 4);
        for (Kit_parameter_values* values : {&a, &b}) {
            ASSERT_TRUE(values->set_enum("strata", strata));
            ASSERT_TRUE(values->set_float("overhang", 0.1f));
            ASSERT_TRUE(values->set_int("ledges", 2));
        }
        const std::unique_ptr<GEO::Mesh> mesh_a = generate(a);
        const std::unique_ptr<GEO::Mesh> mesh_b = generate(b);
        EXPECT_FALSE(editor::kit_meshes_equal(*mesh_a, *mesh_b));
        const float half = 0.5f * a.get_float("length");
        EXPECT_EQ(vertices_on_x(*mesh_a, -half), vertices_on_x(*mesh_b, -half));
        EXPECT_EQ(vertices_on_x(*mesh_a,  half), vertices_on_x(*mesh_b,  half));
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh_a, facet_slots);
        for (GEO::index_t facet = 0; facet < mesh_a->facets.nb(); ++facet) {
            if (facet_slots[facet] != 1) {
                continue;
            }
            for (GEO::index_t corner = 0; corner < mesh_a->facets.nb_vertices(facet); ++corner) {
                EXPECT_EQ(erhe::geometry::get_pointf(mesh_a->vertices, mesh_a->facets.vertex(facet, corner)).y, a.get_float("height"));
            }
        }
    }
    // The setbacks keep the top walkable at the ends of the ranges.
    Kit_parameter_values steep = explicit_values(cliff, 1);
    ASSERT_TRUE(steep.set_float("overhang", -0.25f));
    ASSERT_TRUE(steep.set_float("height", 48.0f));
    ASSERT_TRUE(steep.set_float("depth", 1.5f));
    ASSERT_TRUE(steep.set_int("ledges", 3));
    ASSERT_TRUE(steep.set_float("roughness", 1.0f));
    EXPECT_EQ(cliff.check_walkability(steep, Kit_agent{}), "");
    EXPECT_TRUE(editor::check_kit_shape(steep).is_valid()) << describe(editor::check_kit_shape(steep));
}

// The shore's ends and back edge follow the values, so tiles join; the sand runs from the berm down to the front lip.
TEST(Kit_shore, ends_follow_the_values_and_the_sand_slopes_to_the_lip)
{
    const Kit_shape_definition& shore = get_shape("nature.shore");
    const Kit_parameter_values a = explicit_values(shore, 3);
    const Kit_parameter_values b = explicit_values(shore, 4);
    const std::unique_ptr<GEO::Mesh> mesh_a = generate(a);
    const std::unique_ptr<GEO::Mesh> mesh_b = generate(b);
    EXPECT_FALSE(editor::kit_meshes_equal(*mesh_a, *mesh_b));
    const float half = 0.5f * a.get_float("length");
    EXPECT_EQ(vertices_on_x(*mesh_a, -half), vertices_on_x(*mesh_b, -half));
    EXPECT_EQ(vertices_on_x(*mesh_a,  half), vertices_on_x(*mesh_b,  half));
    const Kit_mesh_report report = editor::check_kit_mesh(*mesh_a);
    EXPECT_NEAR(report.bounds_max.y, a.get_float("rise") + 0.1f, 0.2f);
    EXPECT_EQ(report.bounds_min.y, 0.0f);
    // A rise the agent cannot walk at the default run is refused by the walkability check.
    Kit_parameter_values steep = a;
    ASSERT_TRUE(steep.set_float("rise", 4.0f));
    EXPECT_NE(shore.check_walkability(steep, Kit_agent{}), "");
}

} // anonymous namespace

namespace {

// The box of the corner points of the geometry a brush is built from: generated, then processed as a brush's is.
auto processed_corner_bounds(const Kit_parameter_values& values) -> erhe::math::Aabb
{
    erhe::geometry::Geometry geometry{"bounds"};
    values.get_shape()->generator(values, geometry.get_mesh());
    geometry.process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
        }
    );
    const GEO::Mesh& mesh = geometry.get_mesh();
    erhe::math::Aabb bounds{};
    for (const GEO::index_t facet : mesh.facets) {
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(corner));
            bounds.include(glm::vec3{p.x, p.y, p.z});
        }
    }
    return bounds;
}

} // anonymous namespace

TEST(Kit_shape_library, geometry_bounds_equal_the_processed_geometry_bit_for_bit)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        for (const int seed : {0, 3, 17}) {
            Kit_parameter_values values = defaults(shape);
            static_cast<void>(values.set_int("seed", seed));
            values.resolve_derived(default_module, default_wall_height);
            const erhe::math::Aabb cpu       = editor::get_kit_geometry_bounds(values);
            const erhe::math::Aabb processed = processed_corner_bounds(values);
            ASSERT_TRUE(cpu.is_valid_3d()) << shape.id;
            for (int axis = 0; axis < 3; ++axis) {
                EXPECT_EQ(std::bit_cast<uint32_t>(cpu.min[axis]), std::bit_cast<uint32_t>(processed.min[axis])) << shape.id << " seed " << seed;
                EXPECT_EQ(std::bit_cast<uint32_t>(cpu.max[axis]), std::bit_cast<uint32_t>(processed.max[axis])) << shape.id << " seed " << seed;
            }
        }
    }
}

namespace {

// One parameter's values in a corner sweep.
class Sweep_axis
{
public:
    std::string        name;
    std::vector<float> values; // an int or enum parameter takes the value's integer part
};

// Every combination of the axes' values on top of the explicit values of the given seed, the combinations the shape
// refuses left out; the callback gets each.
template <typename Callback>
void for_each_combination(const Kit_shape_definition& shape, const int seed, const std::vector<Sweep_axis>& axes, const Callback& callback)
{
    std::vector<std::size_t> index(axes.size(), 0);
    while (true) {
        Kit_parameter_values values = explicit_values(shape, seed);
        for (std::size_t a = 0; a < axes.size(); ++a) {
            const std::size_t   p         = shape.find_parameter(axes[a].name).value();
            const float         value     = axes[a].values[index[a]];
            Kit_parameter_value parameter = values.get_values()[p];
            if (shape.parameters[p].type == Kit_parameter_type::float_value) {
                parameter.float_value = value;
            } else {
                parameter.int_value = static_cast<int>(value);
            }
            values.set_value(p, parameter);
        }
        if (values.validate().empty()) {
            callback(values);
        }
        std::size_t a = 0;
        while ((a < axes.size()) && (++index[a] == axes[a].values.size())) {
            index[a] = 0;
            ++a;
        }
        if (a == axes.size()) {
            return;
        }
    }
}

auto range_ends(const Kit_shape_definition& shape, const char* name) -> Sweep_axis
{
    const Kit_parameter& parameter = shape.parameters[shape.find_parameter(name).value()];
    if (parameter.type == Kit_parameter_type::float_value) {
        return Sweep_axis{name, {parameter.float_min, parameter.float_max}};
    }
    return Sweep_axis{name, {static_cast<float>(parameter.int_min), static_cast<float>(parameter.int_max)}};
}

auto all_enum_values(const Kit_shape_definition& shape, const char* name) -> Sweep_axis
{
    const Kit_parameter& parameter = shape.parameters[shape.find_parameter(name).value()];
    Sweep_axis axis{name, {}};
    for (std::size_t i = 0; i < parameter.enum_values.size(); ++i) {
        axis.values.push_back(static_cast<float>(i));
    }
    return axis;
}

// Checks every combination: the mesh checks, the base at y 0, the kit's ceiling; returns how many were checked.
auto check_combinations(const Kit_shape_definition& shape, const std::vector<int>& seeds, const std::vector<Sweep_axis>& axes) -> int
{
    int checked = 0;
    unsigned int most = 0;
    for (const int seed : seeds) {
        for_each_combination(shape, seed, axes, [&](const Kit_parameter_values& values) {
            SCOPED_TRACE(values.to_canonical_text());
            const Kit_mesh_report report = editor::check_kit_shape(values);
            EXPECT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.bounds_min.y, 0.0f);
            EXPECT_LE(report.triangle_count, get_library().find_kit(shape.kit)->triangle_ceiling);
            most = std::max(most, report.triangle_count);
            ++checked;
        });
    }
    testing::Test::RecordProperty(fmt::format("{}_most_triangles", shape.id), static_cast<int>(most));
    return checked;
}

} // anonymous namespace

// Step 62's second batch at the corners where parameters interact (doc/plans/level_kit_more_shapes.md).
TEST(Kit_column, every_order_passes_the_checks_at_the_corners)
{
    const Kit_shape_definition& column = get_shape("architecture.column");
    int checked = 0;
    for_each_combination(column, 1, {
        all_enum_values(column, "order"), range_ends(column, "height"), range_ends(column, "width"),
        Sweep_axis{"flutes", {0.0f, 1.0f, 7.0f, 24.0f}}, range_ends(column, "taper"), range_ends(column, "segments")
    }, [&](const Kit_parameter_values& values) {
        SCOPED_TRACE(values.to_canonical_text());
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_EQ(report.bounds_max.y, values.get_float("height")); // the abacus' top
        EXPECT_LE(report.triangle_count, get_library().find_kit("architecture")->triangle_ceiling);
        ++checked;
    });
    EXPECT_EQ(checked, 3 * 2 * 2 * 4 * 2 * 2);
}

TEST(Kit_entablature, corners_pass_and_ends_join)
{
    const Kit_shape_definition& entablature = get_shape("architecture.entablature");
    const int checked = check_combinations(entablature, {1}, {
        range_ends(entablature, "length"), range_ends(entablature, "height"), range_ends(entablature, "depth"),
        range_ends(entablature, "cornice"), all_enum_values(entablature, "frieze")
    });
    EXPECT_EQ(checked, 32);
    // The two ends' outlines are equal, so runs join without a step; half triglyphs meet into whole ones.
    for (const float length : {1.0f, 2.0f, 3.5f}) {
        Kit_parameter_values values = explicit_values(entablature, 1);
        ASSERT_TRUE(values.set_float("length", length));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<std::array<float, 3>> start = vertices_on_x(*mesh, -0.5f * length);
        std::vector<std::array<float, 3>> end   = vertices_on_x(*mesh,  0.5f * length);
        ASSERT_EQ(start.size(), end.size()) << length;
        for (std::size_t i = 0; i < start.size(); ++i) {
            EXPECT_EQ(start[i][1], end[i][1]);
            EXPECT_EQ(start[i][2], end[i][2]);
        }
    }
}

TEST(Kit_torii, corners_pass_and_the_opening_suits_the_agent_at_derived_values)
{
    const Kit_shape_definition& torii = get_shape("architecture.torii");
    const int checked = check_combinations(torii, {1}, {
        range_ends(torii, "width"), range_ends(torii, "height"), range_ends(torii, "curve"), range_ends(torii, "post_width"),
        range_ends(torii, "lean"), range_ends(torii, "segments")
    });
    EXPECT_GE(checked, 40); // the posts of a narrow, tall, leaning gate meet: refused
    Kit_parameter_values crossing = explicit_values(torii, 1);
    ASSERT_TRUE(crossing.set_float("width", 1.0f));
    ASSERT_TRUE(crossing.set_float("height", 15.0f));
    ASSERT_TRUE(crossing.set_float("lean", 8.0f));
    EXPECT_NE(crossing.validate(), "");
    Kit_parameter_values low = defaults(torii);
    ASSERT_TRUE(low.set_float("height", 2.4f));
    EXPECT_NE(torii.check_walkability(low, Kit_agent{}), ""); // the tie beam is lower than the agent
}

// R2.6 where the triangle maxima lie at mixed values, not at the all-maximum corner (found by a search over every
// combination of range ends and random in-range values): each passes the mesh checks within the kit's ceiling.
TEST(Kit_shape_library, worst_case_combinations_keep_the_ceiling)
{
    const std::vector<std::pair<std::string, std::string>> worst = get_union(&Kit_shape_rows::worst_cases);
    for (const std::pair<std::string, std::string>& entry : worst) {
        const Kit_shape_definition& shape = get_shape(entry.first.c_str());
        Kit_parameter_values values;
        ASSERT_EQ(editor::parse_canonical_text(shape, entry.second, values), "") << entry.second;
        ASSERT_EQ(values.validate(), "") << entry.second;
        SCOPED_TRACE(entry.second);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_LE(report.triangle_count, get_library().find_kit(shape.kit)->triangle_ceiling);
        testing::Test::RecordProperty(fmt::format("{}_worst_case", shape.id), static_cast<int>(report.triangle_count));
    }
}

[[nodiscard]] auto get_shape_ids() -> std::vector<std::string>
{
    std::vector<std::string> ids;
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        ids.push_back(shape.id);
    }
    return ids;
}

[[nodiscard]] auto get_shape_test_name(const testing::TestParamInfo<std::string>& info) -> std::string
{
    std::string name = info.param;
    std::replace(name.begin(), name.end(), '.', '_');
    return name;
}

// One test per shape: c_random_value_set_count mesh checks of random in-range values.
constexpr int c_random_value_set_count = 24;

class Kit_shape_random_values : public testing::TestWithParam<std::string>
{
};

// R2.5 and R2.6 over random in-range values (a fixed stream per shape): every value set the shape accepts passes the
// mesh checks within the kit's ceiling; a shape of Kit_randomness::none gives the same mesh at another seed.
TEST_P(Kit_shape_random_values, pass_the_checks)
{
    const Kit_shape_definition& shape = get_shape(GetParam().c_str());
    editor::Kit_random random{777, shape.id};
    int accepted = 0;
    for (int i = 0; i < c_random_value_set_count; ++i) {
        // A shape that draws nothing from its seed is checked at one seed (seeds_select_the_mesh).
        const uint32_t random_seed = random.next_u32() % 100000u;
        Kit_parameter_values values = explicit_values(shape, (shape.randomness == editor::Kit_randomness::seeded) ? static_cast<int>(random_seed) : 1);
        for (std::size_t p = 0, end = shape.parameters.size(); p < end; ++p) {
            const Kit_parameter& parameter = shape.parameters[p];
            if (parameter.name == "seed") {
                continue;
            }
            Kit_parameter_value value = values.get_values()[p];
            if (parameter.type == Kit_parameter_type::float_value) {
                value.float_value = random.next_range(parameter.float_min, parameter.float_max);
            } else {
                value.int_value = parameter.int_min + static_cast<int>(random.next_u32() % static_cast<uint32_t>(parameter.int_max - parameter.int_min + 1));
            }
            values.set_value(p, value);
        }
        if (!values.validate().empty()) {
            continue; // a combination the shape refuses is no value of it
        }
        ++accepted;
        SCOPED_TRACE(fmt::format("{} {}", shape.id, values.to_canonical_text()));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_LE(report.triangle_count, get_library().find_kit(shape.kit)->triangle_ceiling);
        if (shape.randomness != editor::Kit_randomness::seeded) {
            // Kit_randomness::none over the swept values: another seed gives the same mesh.
            Kit_parameter_values other_seed = values;
            ASSERT_TRUE(other_seed.set_int("seed", 2));
            EXPECT_TRUE(editor::kit_meshes_equal(*generate(values), *generate(other_seed))) << "the mesh depends on the seed";
        }
    }
    EXPECT_GE(accepted, c_random_value_set_count / 2) << shape.id;
}

INSTANTIATE_TEST_SUITE_P(Kit_shape_library, Kit_shape_random_values, testing::ValuesIn(get_shape_ids()), get_shape_test_name);

// D1.8 and R2.10 at other level lengths: every derivable parameter of every shape derived from 300 seeds gives values
// the shape accepts, and a walkable shape stays walkable. Six built shapes whose derive ranges scale with the lengths are
// left out of the walkability part (doc/plans/open_items.md "Derived walkability at other level lengths").
TEST(Kit_derive, derived_values_are_accepted_and_walkable_at_other_level_lengths)
{
    const std::vector<std::string> open_ids = get_union(&Kit_shape_rows::walkability_open);
    const std::set<std::string>    walkability_open{open_ids.begin(), open_ids.end()};
    const Kit_agent agent{};
    for (const std::pair<float, float> lengths : {std::pair{0.5f, 2.5f}, std::pair{1.0f, 2.4f}, std::pair{2.0f, 4.0f}, std::pair{0.5f, 4.0f}, std::pair{2.0f, 2.4f}, std::pair{0.25f, 2.0f}}) {
        for (const Kit_shape_definition& shape : get_library().get_shapes()) {
            int refused   = 0;
            int unwalkable = 0;
            for (int seed = 0; seed < 300; ++seed) {
                Kit_parameter_values values = editor::make_default_values(shape, lengths.first, lengths.second);
                EXPECT_TRUE(values.set_int("seed", seed));
                for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
                    if (shape.parameters[i].derive == editor::Kit_derive::derivable) {
                        values.set_value_source(i, editor::Kit_value_source::seed);
                    }
                }
                values.resolve_derived(lengths.first, lengths.second);
                if (!values.validate().empty()) {
                    ++refused;
                    continue;
                }
                if (shape.check_walkability && !shape.check_walkability(values, agent).empty()) {
                    ++unwalkable;
                }
            }
            SCOPED_TRACE(fmt::format("{} at module {} m, wall height {} m", shape.id, lengths.first, lengths.second));
            EXPECT_EQ(refused, 0);
            if (!walkability_open.contains(shape.id)) {
                EXPECT_EQ(unwalkable, 0);
            } else {
                testing::Test::RecordProperty(fmt::format("{}_unwalkable_at_{}_{}", shape.id, lengths.first, lengths.second), unwalkable);
            }
        }
    }
}

namespace {

// The steepest facet of the mesh in degrees, but those facing down and those rising less than a step (an edge the agent climbs).
auto steepest_facet_degrees(const GEO::Mesh& mesh, const float step) -> float
{
    float steepest = 0.0f;
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        const GEO::vec3f p0 = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(f, 0));
        const GEO::vec3f p1 = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(f, 1));
        const GEO::vec3f p2 = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(f, 2));
        if ((std::max({p0.y, p1.y, p2.y}) - std::min({p0.y, p1.y, p2.y})) < step) {
            continue;
        }
        const glm::vec3 normal = glm::normalize(glm::cross(glm::vec3{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z}, glm::vec3{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z}));
        if (normal.y <= 0.0f) {
            continue;
        }
        steepest = std::max(steepest, glm::degrees(std::acos(std::min(1.0f, normal.y))));
    }
    return steepest;
}

} // anonymous namespace

// Step 62's third batch at the corners where parameters interact (doc/plans/level_kit_more_shapes.md).
TEST(Kit_basalt_columns, corners_pass_and_neighbouring_tops_are_at_most_one_step_apart)
{
    const Kit_shape_definition& basalt = get_shape("nature.basalt_columns");
    const int checked = check_combinations(basalt, sweep_seeds(basalt), {
        range_ends(basalt, "radius"), range_ends(basalt, "column_width"), range_ends(basalt, "height"), range_ends(basalt, "height_spread"),
        range_ends(basalt, "step")
    });
    EXPECT_EQ(checked, static_cast<int>(sweep_seeds(basalt).size()) * 32);
    // The tops (hexagons facing up) of neighbouring columns differ by at most one step, each a whole number of steps below the height.
    for (const int seed : {1, 5, 9}) {
        Kit_parameter_values values = explicit_values(basalt, seed);
        ASSERT_TRUE(values.set_float("height", 3.0f));
        ASSERT_TRUE(values.set_float("height_spread", 0.6f));
        ASSERT_TRUE(values.set_float("radius", 2.5f));
        SCOPED_TRACE(values.to_canonical_text());
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<glm::vec3> tops;
        for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
            if (mesh->facets.nb_vertices(f) != 6) {
                continue;
            }
            glm::vec3 center{0.0f};
            for (GEO::index_t c = 0; c < 6; ++c) {
                const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, mesh->facets.vertex(f, c));
                center += glm::vec3{p.x, p.y, p.z} / 6.0f;
            }
            if (center.y > 0.0f) {
                tops.push_back(center);
            }
        }
        ASSERT_GT(tops.size(), 7u);
        const float step = values.get_float("step");
        bool stepped = false;
        for (std::size_t i = 0; i < tops.size(); ++i) {
            const float steps = (3.0f - tops[i].y) / step;
            EXPECT_NEAR(steps, std::round(steps), 1.0e-3f);
            for (std::size_t j = i + 1; j < tops.size(); ++j) {
                if (glm::length(glm::vec2{tops[i].x - tops[j].x, tops[i].z - tops[j].z}) < 1.1f * values.get_float("column_width")) {
                    EXPECT_LE(std::abs(tops[i].y - tops[j].y), step + 1.0e-4f);
                    stepped = stepped || (std::abs(tops[i].y - tops[j].y) > 0.5f * step);
                }
            }
        }
        EXPECT_TRUE(stepped); // the tops are terraces, not one level
    }
    Kit_parameter_values high = defaults(basalt);
    ASSERT_TRUE(high.set_float("step", 0.5f));
    EXPECT_NE(basalt.check_walkability(high, Kit_agent{}), "");
}

TEST(Kit_roof, every_form_passes_the_checks_at_the_corners_with_its_base_at_the_wall_tops)
{
    const Kit_shape_definition& roof = get_shape("architecture.roof");
    const int checked = check_combinations(roof, {1}, {
        all_enum_values(roof, "form"), range_ends(roof, "width"), range_ends(roof, "depth"), range_ends(roof, "pitch"),
        range_ends(roof, "overhang"), range_ends(roof, "curve")
    });
    EXPECT_EQ(checked, 4 * 32);
    // A square hip roof, and one whose ridge would be shorter than 0.1 m, end in a point.
    for (const float width : {4.0f, 4.05f}) {
        Kit_parameter_values values = defaults(roof);
        ASSERT_TRUE(values.set_enum("form", "hip"));
        ASSERT_TRUE(values.set_float("width", width));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << width << " " << describe(report);
    }
    // The wall line connectors lie on the base plane at the wall lines, their up axes facing out of the room.
    const Kit_parameter_values values = defaults(roof);
    std::vector<editor::Kit_connector> connectors;
    roof.get_connectors(values, connectors);
    ASSERT_EQ(connectors.size(), 5u);
    for (const editor::Kit_connector& connector : connectors) {
        EXPECT_EQ(connector.position.y, 0.0f) << connector.name;
        EXPECT_EQ(connector.outward, (glm::vec3{0.0f, -1.0f, 0.0f})) << connector.name;
        if (std::string_view{connector.name} != "base") {
            EXPECT_GT(glm::dot(connector.up, connector.position), 0.0f) << connector.name;
            EXPECT_EQ(glm::length(connector.position), 2.0f) << connector.name;
        }
    }
}

TEST(Kit_palm_clump, every_species_passes_the_checks_at_the_corners_over_seeds)
{
    const Kit_shape_definition& clump = get_shape("nature.palm_clump");
    const int checked = check_combinations(clump, sweep_seeds(clump), {
        all_enum_values(clump, "species"), range_ends(clump, "stems"), range_ends(clump, "height"), range_ends(clump, "spread"),
        range_ends(clump, "fronds")
    });
    EXPECT_EQ(checked, static_cast<int>(sweep_seeds(clump).size()) * 3 * 16);
    // The binding combination: nine areca stems at the least height carry the 40 fronds; worst_case_combinations_keep_the_ceiling holds it.
    Kit_parameter_values binding;
    ASSERT_EQ(editor::parse_canonical_text(clump, R"({"species":"areca","stems":9,"height":"0x1p-1","spread":"0x0p+0","fronds":10,"seed":1})", binding), "");
    EXPECT_GE(editor::count_kit_triangles(*generate(binding)), 1940u);
}

// The dune's and the ground patch's walkability checks bound the generated slopes: a value set either check passes
// makes no facet steeper than the agent's slope.
TEST(Kit_dune, corners_pass_and_the_walkability_check_bounds_the_slopes)
{
    const Kit_agent agent{};
    for (const char* id : {"nature.dune", "nature.ground_patch"}) {
        const Kit_shape_definition& shape = get_shape(id);
        std::vector<Sweep_axis> axes;
        for (const Kit_parameter& parameter : shape.parameters) {
            if (parameter.name != "seed") {
                axes.push_back(range_ends(shape, parameter.name.c_str()));
            }
        }
        const int checked = check_combinations(shape, sweep_seeds(shape), axes);
        // The dune refuses a length under 1.04 widths: every corner with the least length (2 m against 2 m or 32 m).
        const int refused = (std::string_view{id} == "nature.dune") ? (1 << (static_cast<int>(axes.size()) - 1)) : 0;
        EXPECT_EQ(checked, static_cast<int>(sweep_seeds(shape).size()) * ((1 << static_cast<int>(axes.size())) - refused));
        editor::Kit_random random{99, shape.id};
        int walkable = 0;
        for (int i = 0; i < 300; ++i) {
            Kit_parameter_values values = explicit_values(shape, static_cast<int>(random.next_u32() % 1000u));
            for (std::size_t p = 0, end = shape.parameters.size(); p < end; ++p) {
                const Kit_parameter& parameter = shape.parameters[p];
                if (parameter.type != Kit_parameter_type::float_value) {
                    continue;
                }
                Kit_parameter_value value = values.get_values()[p];
                value.float_value = random.next_range(parameter.float_min, parameter.float_max);
                values.set_value(p, value);
            }
            if (!values.validate().empty() || !shape.check_walkability(values, agent).empty()) {
                continue;
            }
            ++walkable;
            SCOPED_TRACE(values.to_canonical_text());
            const float step = (std::string_view{id} == "nature.ground_patch") ? values.get_float("thickness") + 1.0e-4f : 0.0f;
            EXPECT_LE(steepest_facet_degrees(*generate(values), step), agent.max_slope_degrees);
        }
        EXPECT_GE(walkable, 30) << id;
    }
}

// A roof's wall line connectors joined to the tops of four walls whose fronts face out of a 4 m room: each join puts
// the roof over the room's center at the walls' top, untwisted.
TEST(Kit_roof, wall_line_connectors_join_wall_tops_untwisted)
{
    const Kit_shape_definition& roof = get_shape("architecture.roof");
    const Kit_shape_definition& wall = get_shape("architecture.wall");
    std::vector<editor::Kit_connector> roof_connectors;
    roof.get_connectors(defaults(roof), roof_connectors);
    std::vector<editor::Kit_connector> wall_connectors;
    wall.get_connectors(defaults(wall), wall_connectors);
    const auto find = [](const std::vector<editor::Kit_connector>& connectors, const std::string_view name) -> editor::Kit_connector {
        const auto it = std::find_if(connectors.begin(), connectors.end(), [name](const editor::Kit_connector& c) { return std::string_view{c.name} == name; });
        EXPECT_NE(it, connectors.end()) << name;
        return *it;
    };
    const editor::Kit_connector wall_top = find(wall_connectors, "top");
    class Room_wall
    {
    public:
        const char* roof_connector;
        float       yaw_degrees; // the wall's front (+Z) turned to face out
        glm::vec3   position;
    };
    for (const Room_wall& room_wall : {Room_wall{"front", 0.0f, {0.0f, 0.0f, 2.0f}}, Room_wall{"back", 180.0f, {0.0f, 0.0f, -2.0f}},
                                       Room_wall{"left", -90.0f, {-2.0f, 0.0f, 0.0f}}, Room_wall{"right", 90.0f, {2.0f, 0.0f, 0.0f}}}) {
        SCOPED_TRACE(room_wall.roof_connector);
        const glm::quat turn = glm::angleAxis(glm::radians(room_wall.yaw_degrees), glm::vec3{0.0f, 1.0f, 0.0f});
        editor::Kit_connector target = wall_top;
        target.position = room_wall.position + (turn * wall_top.position);
        target.outward  = turn * wall_top.outward;
        target.up       = turn * wall_top.up;
        const editor::Kit_join_pose pose = editor::join_connectors(find(roof_connectors, room_wall.roof_connector), target, 0, 0.0f);
        EXPECT_NEAR(glm::length(pose.position - glm::vec3{0.0f, 3.0f, 0.0f}), 0.0f, 1.0e-5f);
        EXPECT_NEAR(std::abs(glm::dot(pose.rotation, glm::quat{1.0f, 0.0f, 0.0f, 0.0f})), 1.0f, 1.0e-5f); // no turn, no twist
    }
}

// Long, narrow and tall dunes over every segment count and asymmetry: the top is a strip across the crest ring, so no
// sliver fan folds at any length (the cases a review's random harness found are among them); a length under 1.04
// widths is refused.
TEST(Kit_dune, found_case_passes_and_a_short_dune_is_refused)
{
    const Kit_shape_definition& dune = get_shape("nature.dune");
    for (const char* text : {
        R"({"length":"0x1.7f6cf8p+5","width":"0x1.607df4p+1","height":"0x1.6e42aep+2","asymmetry":"0x1.80554ap-2","seed":275,"segments":35})"}) {
        Kit_parameter_values values;
        ASSERT_EQ(editor::parse_canonical_text(dune, text, values), "");
        ASSERT_EQ(values.validate(), "");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << text << " " << describe(report);
    }
    Kit_parameter_values short_dune = defaults(dune);
    ASSERT_TRUE(short_dune.set_float("length", 5.0f));
    ASSERT_TRUE(short_dune.set_float("width", 5.0f));
    EXPECT_NE(short_dune.validate(), "");
}

// One test per width: a third of the sweep stays well inside the per-test time bound of a Debug build.
class Kit_dune_widths : public testing::TestWithParam<float>
{
};

TEST_P(Kit_dune_widths, long_narrow_and_tall_dunes_pass_the_checks)
{
    const Kit_shape_definition& dune  = get_shape("nature.dune");
    const float                 width = GetParam();
    int checked = 0;
    for (const float length : {2.08f, 8.0f, 24.0f, 48.0f, 64.0f}) {
        for (const float height : {0.05f, 1.0f, 4.0f, 8.0f}) {
            for (const float asymmetry : {0.0f, 0.4f, 0.8f}) {
                for (int segments = 8; segments <= 48; segments += 5) {
                    for (const int seed : {275, 497, 587}) {
                        Kit_parameter_values values = explicit_values(dune, seed);
                        ASSERT_TRUE(values.set_float("length", length));
                        ASSERT_TRUE(values.set_float("width", width));
                        ASSERT_TRUE(values.set_float("height", height));
                        ASSERT_TRUE(values.set_float("asymmetry", asymmetry));
                        ASSERT_TRUE(values.set_int("segments", segments));
                        if (!values.validate().empty()) {
                            EXPECT_LT(length, 1.04f * width);
                            continue;
                        }
                        ++checked;
                        const Kit_mesh_report report = editor::check_kit_shape(values);
                        EXPECT_TRUE(report.is_valid()) << values.to_canonical_text() << " " << describe(report);
                    }
                }
            }
        }
    }
    EXPECT_GE(checked, 1000);
}

INSTANTIATE_TEST_SUITE_P(Kit_dune, Kit_dune_widths, testing::Values(2.0f, 2.75f, 4.0f));

namespace {

// Every former id resolves to its shape and names no other; an unknown id resolves to nothing.
TEST(Kit_former_ids, find_resolves_former_ids_to_their_shape)
{
    const Kit_shape_library& library = get_library();
    std::set<std::string> names;
    int former_count = 0;
    for (const Kit_shape_definition& shape : library.get_shapes()) {
        SCOPED_TRACE(shape.id);
        EXPECT_TRUE(names.insert(shape.id).second);
        for (const std::string& former_id : shape.former_ids) {
            EXPECT_TRUE(names.insert(former_id).second) << former_id;
            EXPECT_EQ(library.find(former_id), &shape) << former_id;
            ++former_count;
        }
    }
    EXPECT_EQ(former_count, 18);
    EXPECT_EQ(library.find("nature.rock.v3"), nullptr);
}

// A saved piece record (shape id and parameter text) of either id reads as the shape through read_kit_piece_record(),
// the reader Level_kits::read_piece_values() is, and generates its mesh.
TEST(Kit_former_ids, piece_records_of_either_id_read_and_generate)
{
    const Kit_shape_library& library = get_library();
    for (const Kit_shape_definition& shape : library.get_shapes()) {
        for (const std::string& former_id : shape.former_ids) {
            for (const std::string* const record_shape : {&shape.id, &former_id}) {
                SCOPED_TRACE(*record_shape);
                for (const Kit_parameter_values& saved : {defaults(shape), all_derived(shape, 17)}) {
                    Kit_parameter_values values;
                    ASSERT_EQ(editor::read_kit_piece_record(library, *record_shape, saved.to_canonical_text(), values), "");
                    EXPECT_EQ(values.get_shape(), &shape);
                    EXPECT_TRUE(values == saved);
                    const Kit_mesh_report report = editor::check_kit_shape(values);
                    EXPECT_TRUE(report.is_valid()) << describe(report);
                    EXPECT_GT(report.triangle_count, 0u);
                }
            }
        }
    }
    Kit_parameter_values values;
    EXPECT_EQ(editor::read_kit_piece_record(library, "basic.block.v3", "{}", values), "the kits have no shape 'basic.block.v3'");
}

// A record whose parameter text names a parameter the shape lacks, or lacks one it has, does not read: the piece keeps
// its loaded mesh (doc/editor/level_tools.md "Derive from seed"). The rock's bevels and the tree's canopy are gone.
TEST(Kit_former_ids, records_of_other_parameters_do_not_read)
{
    const Kit_shape_library& library = get_library();
    Kit_parameter_values values;
    const std::string unknown = editor::read_kit_piece_record(
        library, "nature.rock.v2", R"({"form":"angular","size":"0x1p+0","roughness":"0x1.333334p-2","bevels":1,"seed":1,"derive":["size"]})", values
    );
    EXPECT_EQ(unknown, "the parameters do not read: nature.rock: unknown parameter 'bevels'");
    const std::string missing = editor::read_kit_piece_record(
        library, "nature.rock", R"({"form":"slab","size":"0x1p+0","roughness":"0x1.333334p-2","seed":1})", values
    );
    EXPECT_EQ(missing, "the parameters do not read: nature.rock: parameter 'pieces' is missing");
    EXPECT_NE(
        editor::read_kit_piece_record(library, "nature.tree.v2", R"({"trunk_height":"0x1.ccccccp+0","canopy_size":"0x1p+1","canopy":"round","seed":1,"segments":8})", values),
        ""
    );
}

} // anonymous namespace
