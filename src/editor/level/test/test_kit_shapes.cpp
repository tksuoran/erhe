#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"

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

// The id the tests key a shape's expectations by: a recreated shape shares its original's.
auto contract_id(const Kit_shape_definition& shape) -> const std::string&
{
    return editor::get_contract_id(shape);
}

// The originals that have a recreated shape beside them; a batch of recreated shapes adds its originals here.
auto recreated_originals() -> const std::vector<std::string>&
{
    static const std::vector<std::string> ids{"basic.block", "basic.wedge", "basic.cylinder", "basic.stairs", "basic.floor_slab"};
    return ids;
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

TEST(Kit_shape_library, library_has_the_starter_set_and_the_more_shapes)
{
    // The 14 shapes of R4.1, then the built shapes of doc/plans/level_kit_more_shapes.md.
    const std::vector<std::string> expected{
        "basic.block", "basic.wedge", "basic.cylinder", "basic.stairs", "basic.floor_slab",
        "architecture.wall", "architecture.door_wall", "architecture.pillar", "architecture.arch",
        "nature.rock", "nature.land_mass", "nature.hill", "nature.tree", "nature.bush",
        "architecture.column", "architecture.entablature", "architecture.torii", "architecture.battlement", "architecture.tower",
        "nature.cliff", "nature.shore", "nature.water", "nature.palm", "nature.leaf_plant",
        "nature.rock_spire", "nature.basalt_columns", "architecture.roof", "nature.palm_clump", "nature.palm_shrub",
        "nature.dune", "nature.ground_patch"
    };
    // The recreated shapes stand beside them (doc/editor/level_tools.md "Recreated shapes").
    const std::vector<std::string>& recreated = recreated_originals();
    const Kit_shape_library& library = get_library();
    ASSERT_EQ(library.get_shapes().size(), expected.size() + recreated.size());
    for (const std::string& id : recreated) {
        const Kit_shape_definition* const shape = library.find(id + editor::c_kit_recreated_id_suffix);
        ASSERT_NE(shape, nullptr) << id;
        EXPECT_EQ(shape->recreates, id);
    }
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
        EXPECT_EQ(kit->triangle_ceiling, 2000u);
        const std::string& id = contract_id(shape);
        // The palms and the leaf plant are trunks and one closed part per leaf or leaf half, the basalt one part per
        // column; the column, entablature, torii and roof are closed parts that overlap (base, shaft, capital; profile,
        // triglyphs; posts, beams; slab, body, tile courses, caps).
        if ((id == "nature.palm") || (id == "nature.leaf_plant") || (id == "nature.palm_clump") ||
            (id == "nature.palm_shrub") || (id == "nature.basalt_columns")) {
            EXPECT_GT(report.component_count, 10u);
        } else if ((id == "architecture.column") || (id == "architecture.entablature") || (id == "architecture.torii") ||
                   (id == "architecture.roof")) {
            EXPECT_GT(report.component_count, 3u);
        } else {
            // The tree: trunk and canopy; the tower: its walls and the lintel over its doorway.
            const unsigned int expected_components = ((id == "nature.tree") || (id == "architecture.tower")) ? 2u : 1u;
            EXPECT_EQ(report.component_count, expected_components);
        }
    }
}

TEST(Kit_shape_library, triangle_counts_at_defaults)
{
    const std::vector<std::pair<std::string, unsigned int>> expected{
        {"basic.block", 12}, {"basic.wedge", 8}, {"basic.cylinder", 60}, {"basic.stairs", 124}, {"basic.floor_slab", 12},
        {"basic.block.v2", 12}, {"basic.wedge.v2", 8}, {"basic.cylinder.v2", 60}, {"basic.stairs.v2", 180}, {"basic.floor_slab.v2", 12},
        {"architecture.wall", 12}, {"architecture.door_wall", 44}, {"architecture.pillar", 12}, {"architecture.arch", 100},
        {"nature.rock", rock_default_triangles}, {"nature.land_mass", 128}, {"nature.hill", 128}, {"nature.tree", 68}, {"nature.bush", 48},
        {"nature.cliff", 428}, {"nature.shore", 238}, {"nature.water", 2}, {"nature.palm", 1510},
        {"architecture.column", 724}, {"architecture.entablature", 540}, {"architecture.torii", 400}, {"architecture.battlement", 44},
        {"architecture.tower", 676}, {"nature.leaf_plant", 620}, {"nature.rock_spire", 720}, {"nature.basalt_columns", 740},
        {"architecture.roof", 508}, {"nature.palm_clump", 1912}, {"nature.palm_shrub", 584}, {"nature.dune", 284}, {"nature.ground_patch", 144}
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
    for (const char* id : {"architecture.pillar", "nature.tree", "nature.rock", "nature.cliff", "nature.palm", "architecture.column",
                           "architecture.entablature", "architecture.battlement", "architecture.tower", "nature.leaf_plant",
                           "nature.rock_spire", "architecture.roof", "nature.palm_clump", "nature.palm_shrub"}) {
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
    EXPECT_EQ(seeded_count, 15);
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
    for (const char* id : {"basic.stairs", "basic.wedge", "basic.stairs.v2", "basic.wedge.v2"}) {
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

// D1.9: every rock form at 0, 1 and 2 bevels passes the mesh checks and the kit's triangle ceiling (R2.6).
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
                    EXPECT_LE(report.triangle_count, get_library().find_kit("nature")->triangle_ceiling);
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

// Values of the shape with an empty mask (every value explicit) and the given seed.
auto explicit_values(const Kit_shape_definition& shape, const int seed) -> Kit_parameter_values
{
    Kit_parameter_values values = defaults(shape);
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        values.set_value_source(i, editor::Kit_value_source::explicit_value);
    }
    EXPECT_TRUE(values.set_int("seed", seed));
    return values;
}

// The positions of the mesh's vertices on the plane x = plane_x, sorted.
auto vertices_on_x(const GEO::Mesh& mesh, const float plane_x) -> std::vector<std::array<float, 3>>
{
    std::vector<std::array<float, 3>> points;
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, v);
        if (p.x == plane_x) {
            points.push_back({p.x, p.y, p.z});
        }
    }
    std::sort(points.begin(), points.end());
    return points;
}

// Every parameter of the shape at the minimum or the maximum of its range, an empty mask.
auto range_corner(const Kit_shape_definition& shape, const Range_end range_end) -> Kit_parameter_values
{
    Kit_parameter_values values = defaults(shape);
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        values.set_value_source(i, editor::Kit_value_source::explicit_value);
        const Kit_parameter& parameter = shape.parameters[i];
        if (parameter.name == "seed") {
            continue;
        }
        Kit_parameter_value value = values.get_values()[i];
        if (parameter.type == Kit_parameter_type::float_value) {
            value.float_value = (range_end == Range_end::max) ? parameter.float_max : parameter.float_min;
        } else {
            value.int_value = (range_end == Range_end::max) ? parameter.int_max : parameter.int_min;
        }
        values.set_value(i, value);
    }
    return values;
}

// R2.6: the kit's ceiling binds at every in-range value; the all-maximum corner is the most a shape makes.
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
            for (const int seed : {0, 1, 2, 3}) {
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

// What generated_meshes_are_pinned keeps of a mesh: counts and slots exactly, positions within a tolerance.
class Mesh_pin
{
public:
    std::string          name;
    unsigned int         vertices{0};
    unsigned int         facets  {0};
    unsigned int         corners {0};
    uint64_t             slots   {0}; // FNV-1a 64 of the facet count of each slot, in slot order
    std::array<float, 3> min     {};
    std::array<float, 3> max     {};
    std::array<float, 3> centroid{}; // mean of the vertex positions
    float                area    {0.0f};
};

auto make_mesh_pin(const std::string& name, const GEO::Mesh& mesh) -> Mesh_pin
{
    Mesh_pin pin;
    pin.name     = name;
    pin.vertices = mesh.vertices.nb();
    pin.facets   = mesh.facets.nb();
    glm::dvec3 sum{0.0};
    glm::vec3  low { std::numeric_limits<float>::max()};
    glm::vec3  high{-std::numeric_limits<float>::max()};
    const auto point = [&mesh](const GEO::index_t v) -> glm::vec3 {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, v);
        return glm::vec3{p.x, p.y, p.z};
    };
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        const glm::vec3 p = point(v);
        sum  += glm::dvec3{p};
        low   = glm::min(low, p);
        high  = glm::max(high, p);
    }
    const glm::dvec3 mean = (pin.vertices > 0) ? (sum / static_cast<double>(pin.vertices)) : glm::dvec3{0.0};
    std::vector<uint32_t> facet_slots;
    editor::read_kit_facet_slots(mesh, facet_slots);
    std::vector<uint32_t> slot_counts;
    double area = 0.0;
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        const GEO::index_t corner_count = mesh.facets.nb_vertices(f);
        pin.corners += corner_count;
        const glm::dvec3 origin{point(mesh.facets.vertex(f, 0))};
        for (GEO::index_t c = 1; (c + 1) < corner_count; ++c) {
            const glm::dvec3 a = glm::dvec3{point(mesh.facets.vertex(f, c))} - origin;
            const glm::dvec3 b = glm::dvec3{point(mesh.facets.vertex(f, c + 1))} - origin;
            area += 0.5 * glm::length(glm::cross(a, b));
        }
        if (facet_slots[f] >= slot_counts.size()) {
            slot_counts.resize(facet_slots[f] + 1, 0);
        }
        ++slot_counts[facet_slots[f]];
    }
    pin.slots = 0xcbf29ce484222325ull;
    for (const uint32_t count : slot_counts) {
        for (int i = 0; i < 4; ++i) {
            pin.slots ^= static_cast<uint64_t>((count >> (8 * i)) & 0xffu);
            pin.slots *= 0x100000001b3ull;
        }
    }
    for (int i = 0; i < 3; ++i) {
        pin.min[i]      = low[i];
        pin.max[i]      = high[i];
        pin.centroid[i] = static_cast<float>(mean[i]);
    }
    pin.area = static_cast<float>(area);
    return pin;
}

// Pins every shape's generated mesh across code versions and platforms: vertex, facet and corner counts and the
// facet count of each slot exactly; bounds, centroid and surface area within a tolerance (doc/editor/level_tools.md).
TEST(Kit_shape_library, generated_meshes_are_pinned)
{
    constexpr float position_tolerance = 1.0e-4f; // meters
    constexpr float area_tolerance     = 1.0e-4f; // relative
    const std::vector<Mesh_pin> expected{
        {"basic.block 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 6.00000f},
        {"basic.block.v2 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 6.00000f},
        {"basic.wedge 1", 6, 5, 18, 0x2d401a55eec16520ull, {-0.50000f, 0.00000f, -1.00000f}, {0.50000f, 1.00000f, 1.00000f}, {0.00000f, 0.33333f, -0.33333f}, 7.23607f},
        {"basic.wedge.v2 1", 6, 5, 18, 0x2d401a55eec16520ull, {-0.50000f, 0.00000f, -1.00000f}, {0.50000f, 1.00000f, 1.00000f}, {0.00000f, 0.33333f, -0.33333f}, 7.23607f},
        {"basic.cylinder 1", 32, 18, 96, 0x8cc58f15ad955627ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 4.65218f},
        {"basic.cylinder.v2 1", 32, 18, 96, 0x8cc58f15ad955627ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 4.65218f},
        {"basic.stairs 1", 64, 92, 308, 0xcb65e93be76ff389ull, {-0.50000f, 0.00000f, -2.25000f}, {0.50000f, 3.00000f, 2.25000f}, {0.00000f, 1.50000f, 0.00000f}, 29.40000f},
        {"basic.stairs.v2 1", 92, 104, 388, 0x4c5057cd16338a9dull, {-0.50000f, 0.00000f, -2.25000f}, {0.50000f, 3.00000f, 2.25000f}, {0.00000f, 1.04348f, 0.00000f}, 29.40000f},
        {"basic.floor_slab 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 0.20000f, 1.00000f}, {0.00000f, 0.10000f, 0.00000f}, 9.60000f},
        {"basic.floor_slab.v2 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 0.20000f, 1.00000f}, {0.00000f, 0.10000f, 0.00000f}, 9.60000f},
        {"architecture.wall 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {0.00000f, 1.50000f, 0.00000f}, 27.50000f},
        {"architecture.door_wall 1", 24, 22, 88, 0x0cdadef454be63e3ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {0.00000f, 1.73333f, 0.00000f}, 23.32000f},
        {"architecture.pillar 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-0.25000f, 0.00000f, -0.25000f}, {0.25000f, 3.00000f, 0.25000f}, {0.00000f, 1.50000f, 0.00000f}, 6.50000f},
        {"architecture.arch 1", 52, 50, 200, 0x8d700e0ae6ddc407ull, {-1.30000f, 0.00000f, -0.25000f}, {1.30000f, 2.70000f, 0.25000f}, {0.00000f, 1.48994f, 0.00000f}, 9.75364f},
        {"architecture.column 1", 370, 454, 1632, 0x0087a9af94c808e8ull, {-0.31500f, 0.00000f, -0.31500f}, {0.31500f, 3.00000f, 0.31500f}, {0.00000f, 1.47232f, 0.00000f}, 6.95078f},
        {"architecture.entablature 1", 344, 274, 1088, 0x841c1415a8ad577cull, {-1.00000f, 0.00000f, -0.44000f}, {1.00000f, 0.75000f, 0.44000f}, {0.00000f, 0.35913f, 0.00000f}, 9.52515f},
        {"architecture.torii 1", 216, 168, 736, 0x4afb59e2a3a2aeddull, {-2.55000f, 0.00000f, -0.18750f}, {2.55000f, 3.78000f, 0.18750f}, {0.00000f, 2.32201f, 0.00000f}, 20.62952f},
        {"architecture.battlement 1", 24, 22, 88, 0x0cdadef454be63e3ull, {-1.00000f, 0.00000f, -0.12500f}, {1.00000f, 1.80000f, 0.12500f}, {0.00000f, 0.90000f, 0.00000f}, 8.11000f},
        {"architecture.tower 1", 334, 427, 1530, 0x73bb4286a4031605ull, {-1.66083f, 0.00000f, -1.63560f}, {1.66083f, 6.40000f, 1.72083f}, {0.00000f, 3.57305f, 0.01988f}, 125.54085f},
        {"architecture.roof 1", 296, 254, 1016, 0xf10a827156626e1bull, {-2.33000f, 0.00000f, -2.30000f}, {2.33000f, 1.83248f, 2.30000f}, {0.00000f, 0.83722f, 0.00000f}, 156.34335f},
        {"nature.rock 1", 58, 97, 306, 0xac804b820e4fe984ull, {-0.21238f, 0.00000f, -0.30518f}, {0.23571f, 0.35739f, 0.26512f}, {0.01986f, 0.12991f, -0.01321f}, 0.67238f},
        {"nature.rock 3", 52, 89, 278, 0xab55ed54ea11293cull, {-0.42355f, 0.00000f, -0.52139f}, {0.53290f, 0.57794f, 0.56602f}, {-0.01152f, 0.21975f, -0.02118f}, 2.47714f},
        {"nature.rock 17", 57, 99, 308, 0xec75a392babb62a6ull, {-0.53616f, 0.00000f, -0.41667f}, {0.53399f, 0.53632f, 0.47814f}, {-0.00520f, 0.21338f, 0.01204f}, 2.21727f},
        {"nature.land_mass 1", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-1.85458f, 0.00000f, -2.14295f}, {2.03018f, 1.00000f, 2.16942f}, {0.02663f, 0.49913f, -0.01562f}, 29.45218f},
        {"nature.land_mass 3", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-2.18004f, 0.00000f, -1.92235f}, {2.10604f, 1.00000f, 1.96032f}, {-0.03651f, 0.49683f, 0.00018f}, 29.59030f},
        {"nature.land_mass 17", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-2.12133f, 0.00000f, -2.04869f}, {2.29766f, 1.00000f, 2.03226f}, {0.00202f, 0.49771f, -0.00784f}, 30.76304f},
        {"nature.hill 1", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-2.83959f, 0.00000f, -3.13385f}, {2.99969f, 0.65000f, 3.16990f}, {0.02293f, 0.24571f, -0.01162f}, 56.45112f},
        {"nature.hill 3", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-3.12943f, 0.00000f, -2.82175f}, {3.05097f, 1.15000f, 2.90477f}, {-0.02655f, 0.43359f, 0.00287f}, 56.71866f},
        {"nature.hill 17", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-3.06270f, 0.00000f, -2.97855f}, {3.32143f, 0.70000f, 3.02918f}, {0.00343f, 0.26353f, -0.00586f}, 57.12008f},
        {"nature.tree 1", 38, 56, 180, 0xa19deeea55812f9dull, {-0.94027f, 0.00000f, -0.89835f}, {0.80734f, 2.97000f, 0.93816f}, {-0.01557f, 1.79453f, -0.00565f}, 9.78669f},
        {"nature.tree 3", 38, 56, 180, 0xa19deeea55812f9dull, {-1.33725f, 0.00000f, -1.40188f}, {1.23756f, 4.71000f, 1.59014f}, {-0.02287f, 2.82603f, 0.01781f}, 24.08631f},
        {"nature.tree 17", 38, 56, 180, 0xa19deeea55812f9dull, {-0.98720f, 0.00000f, -1.07994f}, {1.16362f, 4.14000f, 1.06425f}, {-0.00568f, 2.66775f, 0.00670f}, 14.01106f},
        {"nature.bush 1", 26, 48, 144, 0x4d7ab5fa3a724ae5ull, {-0.70145f, 0.00000f, -0.65352f}, {0.54069f, 0.78000f, 0.68968f}, {-0.02239f, 0.28680f, -0.00804f}, 3.62053f},
        {"nature.bush 3", 26, 48, 144, 0x4d7ab5fa3a724ae5ull, {-0.55467f, 0.00000f, -0.59916f}, {0.48508f, 0.75000f, 0.72354f}, {-0.02115f, 0.27234f, 0.01868f}, 3.05131f},
        {"nature.bush 17", 26, 48, 144, 0x4d7ab5fa3a724ae5ull, {-0.51817f, 0.00000f, -0.60427f}, {0.67877f, 0.69000f, 0.59587f}, {-0.00531f, 0.25871f, 0.00662f}, 2.91611f},
        {"nature.cliff 1", 216, 395, 1218, 0xc108f4e24607d651ull, {-2.00000f, 0.00000f, -1.00000f}, {2.00000f, 6.90000f, 1.99953f}, {0.00022f, 3.45461f, 0.80411f}, 101.90959f},
        {"nature.cliff 3", 144, 251, 786, 0x9500dab4b480d18eull, {-2.00000f, 0.00000f, -1.00000f}, {2.00000f, 3.30000f, 1.43399f}, {0.00098f, 1.65618f, 0.60090f}, 55.39624f},
        {"nature.cliff 17", 168, 299, 930, 0x644c8f3b707981b1ull, {-2.00000f, 0.00000f, -1.00000f}, {2.00000f, 3.90000f, 1.31200f}, {-0.00100f, 1.95505f, 0.39021f}, 62.68912f},
        {"nature.shore 1", 121, 237, 712, 0x29c0ffce82052448ull, {-2.00000f, 0.00000f, -3.00000f}, {2.00000f, 1.10450f, 3.00000f}, {0.00000f, 0.67512f, 0.00000f}, 62.04102f},
        {"nature.shore 3", 121, 237, 712, 0x29c0ffce82052448ull, {-2.00000f, 0.00000f, -3.00000f}, {2.00000f, 1.10131f, 3.00000f}, {0.00000f, 0.69196f, 0.00000f}, 62.01799f},
        {"nature.shore 17", 121, 237, 712, 0x29c0ffce82052448ull, {-2.00000f, 0.00000f, -3.00000f}, {2.00000f, 1.12084f, 3.00000f}, {0.00000f, 0.68936f, 0.00000f}, 62.01485f},
        {"nature.water 1", 4, 1, 4, 0xad2aca7747985764ull, {-4.00000f, 0.00000f, -4.00000f}, {4.00000f, 0.00000f, 4.00000f}, {0.00000f, 0.00000f, 0.00000f}, 64.00000f},
        {"nature.dune 1", 144, 270, 824, 0x045c03b19e2880b0ull, {-4.66538f, 0.00000f, -3.37500f}, {4.66538f, 0.63700f, 3.37500f}, {-0.04318f, 0.29990f, 0.46217f}, 107.53407f},
        {"nature.dune 3", 144, 270, 824, 0x045c03b19e2880b0ull, {-5.16852f, 0.00000f, -3.25000f}, {5.16852f, 0.73500f, 3.25000f}, {-0.10119f, 0.34604f, 0.44383f}, 118.06809f},
        {"nature.dune 17", 144, 270, 824, 0x045c03b19e2880b0ull, {-5.91538f, 0.00000f, -3.37500f}, {5.91538f, 0.68600f, 3.37500f}, {0.14362f, 0.32297f, 0.46524f}, 141.90427f},
        {"nature.ground_patch 1", 74, 144, 432, 0x4a263b301c082585ull, {-2.87980f, 0.00000f, -2.64799f}, {2.60800f, 0.20750f, 2.34955f}, {0.00000f, 0.07099f, 0.00000f}, 43.67327f},
        {"nature.ground_patch 3", 74, 144, 432, 0x4a263b301c082585ull, {-2.15619f, 0.00000f, -2.70255f}, {2.05114f, 0.18500f, 2.47553f}, {0.00000f, 0.06558f, -0.00000f}, 32.11528f},
        {"nature.ground_patch 17", 74, 144, 432, 0x4a263b301c082585ull, {-2.23618f, 0.00000f, -2.43645f}, {2.14958f, 0.18500f, 2.34262f}, {0.00000f, 0.06558f, 0.00000f}, 31.79795f},
        {"nature.rock_spire 1", 362, 720, 2160, 0x377e47459fa74c6full, {-3.64752f, 0.00000f, -4.11290f}, {3.52583f, 14.78250f, 3.67956f}, {0.01016f, 7.16546f, -0.19037f}, 238.56767f},
        {"nature.rock_spire 3", 170, 336, 1008, 0x42d1be1a89b1029aull, {-3.07933f, 0.00000f, -3.00723f}, {3.88463f, 4.48500f, 2.16148f}, {0.10510f, 2.03278f, 0.13275f}, 84.62290f},
        {"nature.rock_spire 17", 362, 720, 2160, 0x377e47459fa74c6full, {-2.64916f, 0.00000f, -4.44135f}, {3.23760f, 16.22400f, 3.17109f}, {0.33947f, 7.92638f, 0.28505f}, 228.92017f},
        {"nature.basalt_columns 1", 444, 296, 1332, 0x44fbdab783dc67b2ull, {-1.71299f, 0.00000f, -1.55557f}, {1.72809f, 2.70000f, 1.56255f}, {0.00000f, 1.24459f, -0.00000f}, 148.30334f},
        {"nature.basalt_columns 3", 732, 488, 2196, 0x40fce0f82c29d472ull, {-2.57168f, 0.00000f, -2.30863f}, {2.58082f, 2.70000f, 2.31266f}, {0.00000f, 1.19754f, -0.00000f}, 285.46548f},
        {"nature.basalt_columns 17", 84, 56, 252, 0x4d50163cec202f6dull, {-0.72651f, 0.00000f, -0.69813f}, {0.72253f, 2.70000f, 0.68889f}, {0.00000f, 1.32857f, -0.00000f}, 30.45187f},
        {"nature.palm 1", 843, 1495, 4500, 0x5d9ce96fdb009a31ull, {-3.05603f, 0.00000f, -1.48759f}, {2.37736f, 12.33090f, 3.88708f}, {-0.45316f, 10.53339f, 0.92783f}, 58.33212f},
        {"nature.palm 3", 555, 983, 2964, 0x6eefdf6fe4d09787ull, {-3.48997f, 0.00000f, -2.01061f}, {1.86270f, 10.79839f, 3.08687f}, {-0.61547f, 9.04642f, 0.50534f}, 35.82668f},
        {"nature.palm 17", 843, 1495, 4500, 0x5d9ce96fdb009a31ull, {-3.22049f, 0.00000f, -4.16760f}, {2.50953f, 12.78151f, 1.69860f}, {-0.43074f, 10.89694f, -1.16128f}, 58.57428f},
        {"nature.leaf_plant 1", 348, 614, 1848, 0x2f2da5e5ed3504c9ull, {-0.53277f, 0.00000f, -0.40541f}, {0.64207f, 1.24859f, 0.47117f}, {0.01968f, 0.77218f, 0.01167f}, 1.48334f},
        {"nature.leaf_plant 3", 603, 1064, 3198, 0xc7e7e0947ff42590ull, {-0.21391f, 0.00000f, -0.17556f}, {0.15538f, 0.37633f, 0.18560f}, {-0.00717f, 0.26109f, -0.00173f}, 0.28674f},
        {"nature.leaf_plant 17", 348, 614, 1848, 0x2f2da5e5ed3504c9ull, {-0.71894f, 0.00000f, -0.64669f}, {1.00937f, 1.69909f, 0.95500f}, {0.03086f, 1.15130f, 0.08859f}, 3.90585f},
        {"nature.palm_clump 1", 1134, 1865, 5642, 0x344df1114a183a71ull, {-1.56352f, 0.00000f, -2.17078f}, {1.37917f, 4.85206f, 1.75516f}, {-0.03700f, 2.77721f, -0.01980f}, 18.72364f},
        {"nature.palm_clump 3", 670, 1097, 3322, 0x17ae610c8871716dull, {-0.39434f, 0.00000f, -0.74393f}, {0.88112f, 2.23237f, 0.61127f}, {0.11201f, 1.40232f, -0.03305f}, 2.72475f},
        {"nature.palm_clump 17", 506, 825, 2502, 0x6dfb0758e807b7f2ull, {-1.68756f, 0.00000f, -0.69025f}, {0.93280f, 4.08483f, 0.83392f}, {-0.10641f, 2.28608f, -0.02681f}, 5.82635f},
        {"nature.palm_shrub 1", 342, 470, 1524, 0x48d5df08b6c0da18ull, {-1.22554f, 0.00000f, -0.93309f}, {1.12310f, 1.30057f, 1.10304f}, {-0.00991f, 0.68600f, 0.02234f}, 15.22223f},
        {"nature.palm_shrub 3", 288, 396, 1284, 0x84a19d9cbd27a482ull, {-0.82727f, 0.00000f, -0.82216f}, {0.97043f, 1.07892f, 0.80320f}, {0.03224f, 0.52061f, -0.01719f}, 7.73327f},
        {"nature.palm_shrub 17", 396, 544, 1764, 0xfd0d30eb35555863ull, {-1.03428f, 0.00000f, -0.79639f}, {0.85171f, 1.18611f, 0.93885f}, {0.00018f, 0.60575f, 0.01251f}, 13.27199f}
    };
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
    ASSERT_EQ(actual.size(), expected.size()) << table;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        SCOPED_TRACE(fmt::format("{}\n{}", actual[i].name, table));
        EXPECT_EQ(actual[i].name,     expected[i].name);
        EXPECT_EQ(actual[i].vertices, expected[i].vertices);
        EXPECT_EQ(actual[i].facets,   expected[i].facets);
        EXPECT_EQ(actual[i].corners,  expected[i].corners);
        EXPECT_EQ(actual[i].slots,    expected[i].slots);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            EXPECT_NEAR(actual[i].min[axis],      expected[i].min[axis],      position_tolerance);
            EXPECT_NEAR(actual[i].max[axis],      expected[i].max[axis],      position_tolerance);
            EXPECT_NEAR(actual[i].centroid[axis], expected[i].centroid[axis], position_tolerance);
        }
        EXPECT_NEAR(actual[i].area, expected[i].area, area_tolerance * std::max(1.0f, expected[i].area));
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
        for (int seed = 0; seed < 24; ++seed) {
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
TEST(Kit_leaf_plant, every_form_passes_the_checks_at_the_size_corners_over_seeds)
{
    const Kit_shape_definition& plant = get_shape("nature.leaf_plant");
    EXPECT_EQ(plant.collision, editor::Kit_collision::none);
    const int checked = check_combinations(plant, {0, 1, 2, 3}, {
        all_enum_values(plant, "form"), range_ends(plant, "height"), range_ends(plant, "leaves"),
        Sweep_axis{"droop", {0.0f, 1.0f}}, range_ends(plant, "leaf_width")
    });
    EXPECT_EQ(checked, 4 * 5 * 16);
}

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

TEST(Kit_battlement, corners_pass_and_ends_join)
{
    const Kit_shape_definition& battlement = get_shape("architecture.battlement");
    const int checked = check_combinations(battlement, {1}, {
        all_enum_values(battlement, "merlon"), range_ends(battlement, "length"), range_ends(battlement, "merlon_width"),
        range_ends(battlement, "crenel_width"), range_ends(battlement, "height"), range_ends(battlement, "thickness")
    });
    EXPECT_EQ(checked, 3 * 32);
    for (const std::string& merlon : {std::string{"square"}, std::string{"swallowtail"}, std::string{"pointed"}}) {
        for (const float length : {1.0f, 2.0f, 5.5f}) {
            Kit_parameter_values values = explicit_values(battlement, 1);
            ASSERT_TRUE(values.set_enum("merlon", merlon));
            ASSERT_TRUE(values.set_float("length", length));
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            std::vector<std::array<float, 3>> start = vertices_on_x(*mesh, -0.5f * length);
            std::vector<std::array<float, 3>> end   = vertices_on_x(*mesh,  0.5f * length);
            ASSERT_EQ(start.size(), end.size()) << merlon << " " << length;
            for (std::size_t i = 0; i < start.size(); ++i) {
                EXPECT_EQ(start[i][1], end[i][1]);
                EXPECT_EQ(start[i][2], end[i][2]);
            }
        }
    }
}

TEST(Kit_tower, every_profile_and_top_passes_at_the_corners_with_a_walkable_top)
{
    const Kit_shape_definition& tower = get_shape("architecture.tower");
    const int checked = check_combinations(tower, {1}, {
        all_enum_values(tower, "profile"), all_enum_values(tower, "top"), range_ends(tower, "width"), range_ends(tower, "height"),
        range_ends(tower, "batter"), range_ends(tower, "wall"), range_ends(tower, "opening_width"), range_ends(tower, "opening_height")
    });
    EXPECT_EQ(checked, 120); // a narrow tower with thick walls and a wide doorway, or a low one, is refused
    for (const std::string& profile : {std::string{"round"}, std::string{"square"}}) {
        for (const std::string& top : {std::string{"battlement"}, std::string{"flat"}}) {
            Kit_parameter_values values = defaults(tower);
            ASSERT_TRUE(values.set_enum("profile", profile));
            ASSERT_TRUE(values.set_enum("top", top));
            SCOPED_TRACE(values.to_canonical_text());
            // The floor slot is the walkable top: flat, at the height, under the top connector.
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            std::vector<uint32_t> facet_slots;
            editor::read_kit_facet_slots(*mesh, facet_slots);
            for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                if (facet_slots[f] != 1) {
                    continue;
                }
                for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                    EXPECT_EQ(erhe::geometry::get_pointf(mesh->vertices, mesh->facets.vertex(f, c)).y, values.get_float("height"));
                }
            }
            EXPECT_EQ(tower.check_walkability(values, Kit_agent{}), "");
            // The doorway is clear: no vertex inside its box but on its jambs, head and threshold.
            const float half = 0.5f * values.get_float("opening_width");
            for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
                const bool inside = (std::abs(p.x) < half - 1.0e-4f) && (p.y > 1.0e-4f) && (p.y < values.get_float("opening_height") - 1.0e-4f) && (p.z > 0.0f);
                EXPECT_FALSE(inside) << p.x << " " << p.y << " " << p.z;
            }
        }
    }
}
// R2.6 where the triangle maxima lie at mixed values, not at the all-maximum corner (found by a search over every
// combination of range ends and random in-range values): each passes the mesh checks within the kit's ceiling.
TEST(Kit_shape_library, worst_case_combinations_keep_the_ceiling)
{
    const std::vector<std::pair<std::string, std::string>> worst{
        {"architecture.column",      R"({"order":"corinthian","height":"0x1p-1","width":"0x1.99999ap-4","flutes":24,"taper":"0x0p+0","seed":1,"segments":20})"},
        {"architecture.entablature", R"({"length":"0x1p+3","height":"0x1.99999ap-3","depth":"0x1.99999ap-3","cornice":"0x0p+0","frieze":"triglyph","seed":1})"},
        {"architecture.entablature", R"({"length":"0x1p+1","height":"0x1.99999ap-3","depth":"0x1p-1","cornice":"0x1.333334p-3","frieze":"triglyph","seed":1})"},
        {"architecture.torii",       R"({"width":"0x1p+0","height":"0x1p+1","curve":"0x0p+0","post_width":"0x1.99999ap-4","lean":"0x0p+0","seed":1,"segments":24})"},
        {"architecture.battlement",  R"({"length":"0x1.8p+4","height":"0x1.99999ap-3","thickness":"0x1.99999ap-5","merlon_height":"0x1.99999ap-3","merlon_width":"0x1.99999ap-2","crenel_width":"0x1.333334p-2","merlon":"swallowtail","seed":1})"},
        {"architecture.tower",       R"({"profile":"round","width":"0x1p+4","height":"0x1.ep+4","batter":"0x0p+0","wall":"0x1.99999ap-3","opening_width":"0x1.99999ap-1","opening_height":"0x1p+1","top":"battlement","slit_rows":2,"seed":1})"},
        {"architecture.tower",       R"({"profile":"round","width":"0x1p+4","height":"0x1.cp+1","batter":"0x0p+0","wall":"0x1.99999ap-3","opening_width":"0x1.99999ap-1","opening_height":"0x1p+1","top":"battlement","slit_rows":2,"seed":1})"},
        {"nature.leaf_plant",        R"({"form":"banana","height":"0x1.333334p-2","leaves":14,"droop":"0x0p+0","leaf_width":"0x1.99999ap-4","seed":1})"},
        {"nature.palm",              R"({"species":"coconut","height":"0x1.8p+0","bend":"0x0p+0","fronds":24,"frond_length":"0x1.333334p-2","droop":"0x0p+0","seed":1,"segments":12})"},
        {"nature.rock_spire",        R"({"form":"pinnacle","height":"0x1.ep+5","radius":"0x1.99999ap-3","taper":"0x0p+0","lean":"0x0p+0","roughness":"0x0p+0","seed":1,"segments":20})"},
        {"nature.basalt_columns",    R"({"radius":"0x1p+4","column_width":"0x1.333334p-3","height":"0x1.99999ap-3","height_spread":"0x0p+0","step":"0x1.99999ap-5","seed":1})"},
        {"architecture.roof",        R"({"form":"hip","width":"0x1.9cf408p+3","depth":"0x1.76ffb6p+4","pitch":"0x1.6b7964p+5","overhang":"0x1.a87f5ep-4","curve":"0x1.175114p-4","seed":81079})"},
        {"nature.palm_clump",        R"({"species":"areca","stems":9,"height":"0x1p-1","spread":"0x0p+0","fronds":10,"seed":1})"},
        {"nature.palm_shrub",        R"({"species":"cycad","size":"0x1.333334p-2","fronds":16,"seed":1})"},
        {"nature.dune",              R"({"length":"0x1p+2","width":"0x1p+1","height":"0x1.99999ap-5","asymmetry":"0x0p+0","seed":1,"segments":48})"},
        {"nature.ground_patch",      R"({"size":"0x1.99999ap-3","thickness":"0x1.47ae14p-7","irregularity":"0x0p+0","dome":"0x0p+0","seed":1,"segments":48})"},
        {"nature.cliff",             R"({"length":"0x1.ddc0f6p+5","height":"0x1.91c9fcp+3","depth":"0x1.ee5558p+3","strata":"layered","overhang":"0x1.c484fp-5","roughness":"0x1.9873fp-1","ledges":3,"seed":261})"}
    };
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

// One test per shape: 200 mesh checks of one shape stay well inside the per-test time bound of a Debug build.
class Kit_shape_random_values : public testing::TestWithParam<std::string>
{
};

// R2.5 and R2.6 over random in-range values (a fixed stream per shape): every value set the shape accepts passes the
// mesh checks within the kit's ceiling.
TEST_P(Kit_shape_random_values, pass_the_checks)
{
    const Kit_shape_definition& shape = get_shape(GetParam().c_str());
    editor::Kit_random random{777, shape.id};
    int accepted = 0;
    for (int i = 0; i < 200; ++i) {
        Kit_parameter_values values = explicit_values(shape, static_cast<int>(random.next_u32() % 100000u));
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
    }
    EXPECT_GE(accepted, 100) << shape.id;
}

INSTANTIATE_TEST_SUITE_P(Kit_shape_library, Kit_shape_random_values, testing::ValuesIn(get_shape_ids()), get_shape_test_name);

// D1.8 and R2.10 at other level lengths: every derivable parameter of every shape derived from 300 seeds gives values
// the shape accepts, and a walkable shape stays walkable. Six built shapes whose derive ranges scale with the lengths are
// left out of the walkability part (doc/plans/open_items.md "Derived walkability at other level lengths").
TEST(Kit_derive, derived_values_are_accepted_and_walkable_at_other_level_lengths)
{
    const std::set<std::string> walkability_open{"basic.wedge", "basic.stairs", "basic.floor_slab", "architecture.arch", "nature.land_mass", "nature.shore"};
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
            if (!walkability_open.contains(contract_id(shape))) {
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
TEST(Kit_rock_spire, every_form_passes_the_checks_at_the_corners_over_seeds)
{
    const Kit_shape_definition& spire = get_shape("nature.rock_spire");
    // A thin, tall, leaning rock of maximum roughness and a wide low one: the base stays the lowest point.
    const int checked = check_combinations(spire, {0, 1, 2, 3}, {
        all_enum_values(spire, "form"), range_ends(spire, "height"), range_ends(spire, "radius"), range_ends(spire, "taper"),
        range_ends(spire, "lean"), Sweep_axis{"roughness", {0.0f, 1.0f}}, range_ends(spire, "segments")
    });
    EXPECT_EQ(checked, 4 * 4 * 64);
}

TEST(Kit_basalt_columns, corners_pass_and_neighbouring_tops_are_at_most_one_step_apart)
{
    const Kit_shape_definition& basalt = get_shape("nature.basalt_columns");
    const int checked = check_combinations(basalt, {0, 1, 2}, {
        range_ends(basalt, "radius"), range_ends(basalt, "column_width"), range_ends(basalt, "height"), range_ends(basalt, "height_spread"),
        range_ends(basalt, "step")
    });
    EXPECT_EQ(checked, 3 * 32);
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
    const int checked = check_combinations(clump, {0, 1, 2, 3}, {
        all_enum_values(clump, "species"), range_ends(clump, "stems"), range_ends(clump, "height"), range_ends(clump, "spread"),
        range_ends(clump, "fronds")
    });
    EXPECT_EQ(checked, 4 * 3 * 16);
    // The binding combination: nine areca stems at the least height carry the 40 fronds; worst_case_combinations_keep_the_ceiling holds it.
    Kit_parameter_values binding;
    ASSERT_EQ(editor::parse_canonical_text(clump, R"({"species":"areca","stems":9,"height":"0x1p-1","spread":"0x0p+0","fronds":10,"seed":1})", binding), "");
    EXPECT_GE(editor::count_kit_triangles(*generate(binding)), 1940u);
}

TEST(Kit_palm_shrub, every_species_passes_the_checks_at_the_corners_over_seeds)
{
    const Kit_shape_definition& shrub = get_shape("nature.palm_shrub");
    EXPECT_EQ(shrub.collision, editor::Kit_collision::convex_hull);
    const int checked = check_combinations(shrub, {0, 1, 2, 3}, {
        all_enum_values(shrub, "species"), range_ends(shrub, "size"), range_ends(shrub, "fronds")
    });
    EXPECT_EQ(checked, 4 * 3 * 4);
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
        const int checked = check_combinations(shape, {0, 1, 2}, axes);
        // The dune refuses a length under 1.04 widths: every corner with the least length (2 m against 2 m or 32 m).
        const int refused = (std::string_view{id} == "nature.dune") ? (1 << (static_cast<int>(axes.size()) - 1)) : 0;
        EXPECT_EQ(checked, 3 * ((1 << static_cast<int>(axes.size())) - refused));
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

auto get_connectors(const Kit_parameter_values& values) -> std::vector<editor::Kit_connector>
{
    std::vector<editor::Kit_connector> connectors;
    if (values.get_shape()->get_connectors) {
        values.get_shape()->get_connectors(values, connectors);
    }
    return connectors;
}

void expect_equal_parameters(const Kit_parameter& a, const Kit_parameter& b)
{
    SCOPED_TRACE(a.name);
    EXPECT_EQ(a.name,              b.name);
    EXPECT_EQ(a.type,              b.type);
    EXPECT_EQ(a.unit,              b.unit);
    EXPECT_EQ(a.default_scale,     b.default_scale);
    EXPECT_EQ(a.float_default,     b.float_default);
    EXPECT_EQ(a.float_min,         b.float_min);
    EXPECT_EQ(a.float_max,         b.float_max);
    EXPECT_EQ(a.int_default,       b.int_default);
    EXPECT_EQ(a.int_min,           b.int_min);
    EXPECT_EQ(a.int_max,           b.int_max);
    EXPECT_EQ(a.enum_values,       b.enum_values);
    EXPECT_EQ(a.derive,            b.derive);
    EXPECT_EQ(a.derive_float_min,  b.derive_float_min);
    EXPECT_EQ(a.derive_float_max,  b.derive_float_max);
    EXPECT_EQ(a.derive_float_step, b.derive_float_step);
    EXPECT_EQ(a.derive_int_min,    b.derive_int_min);
    EXPECT_EQ(a.derive_int_max,    b.derive_int_max);
    EXPECT_EQ(a.default_source,    b.default_source);
}

// A recreated shape stands directly after its original and states the same contract, so a piece of one is replaced
// by the other value for value (doc/editor/level_tools.md "Recreated shapes").
TEST(Kit_recreated, every_recreated_shape_pairs_with_its_original)
{
    const std::vector<Kit_shape_definition>& shapes = get_library().get_shapes();
    std::vector<std::string> paired;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const Kit_shape_definition& shape = shapes[i];
        if (shape.recreates.empty()) {
            continue;
        }
        paired.push_back(shape.recreates);
        SCOPED_TRACE(shape.id);
        const Kit_shape_definition* const original = get_library().find(shape.recreates);
        ASSERT_NE(original, nullptr);
        EXPECT_TRUE(original->recreates.empty());
        ASSERT_GT(i, 0u);
        EXPECT_EQ(&shapes[i - 1], original);
        EXPECT_EQ(shape.id,           original->id + editor::c_kit_recreated_id_suffix);
        EXPECT_EQ(shape.display_name, original->display_name + editor::c_kit_recreated_name_suffix);
        EXPECT_EQ(shape.kit,            original->kit);
        EXPECT_EQ(shape.material_slots, original->material_slots);
        EXPECT_EQ(shape.origin_rule,    original->origin_rule);
        EXPECT_EQ(shape.collision,      original->collision);
        EXPECT_EQ(shape.mesh_kind,      original->mesh_kind);
        EXPECT_EQ(shape.randomness,     original->randomness);
        EXPECT_EQ(shape.mirror_planes,  original->mirror_planes);
        EXPECT_EQ(shape.tags,           original->tags);
        EXPECT_EQ(shape.description,    original->description);
        EXPECT_EQ(shape.normal_style,   original->normal_style);
        EXPECT_EQ(shape.hard_edge_angle_degrees, original->hard_edge_angle_degrees);
        EXPECT_EQ(static_cast<bool>(shape.check_walkability), static_cast<bool>(original->check_walkability));
        EXPECT_EQ(static_cast<bool>(shape.check_combination), static_cast<bool>(original->check_combination));
        ASSERT_EQ(shape.parameters.size(), original->parameters.size());
        for (std::size_t p = 0; p < shape.parameters.size(); ++p) {
            expect_equal_parameters(shape.parameters[p], original->parameters[p]);
        }
        // Equal parameters derive equal values, so the connectors are compared at the same values.
        for (const int seed : {1, 3, 17}) {
            const Kit_parameter_values values          = all_derived(shape, seed);
            const Kit_parameter_values original_values = all_derived(*original, seed);
            ASSERT_EQ(values.get_values(), original_values.get_values());
            const std::vector<editor::Kit_connector> connectors          = get_connectors(values);
            const std::vector<editor::Kit_connector> original_connectors = get_connectors(original_values);
            ASSERT_EQ(connectors.size(), original_connectors.size());
            for (std::size_t c = 0; c < connectors.size(); ++c) {
                EXPECT_EQ(std::string_view{connectors[c].name}, std::string_view{original_connectors[c].name});
                EXPECT_LT(glm::distance(connectors[c].position, original_connectors[c].position), 1.0e-5f) << connectors[c].name;
                EXPECT_LT(glm::distance(connectors[c].outward,  original_connectors[c].outward),  1.0e-5f) << connectors[c].name;
                EXPECT_LT(glm::distance(connectors[c].up,       original_connectors[c].up),       1.0e-5f) << connectors[c].name;
            }
            if (shape.get_seam_sides) {
                ASSERT_TRUE(static_cast<bool>(original->get_seam_sides));
                EXPECT_EQ(shape.get_seam_sides(values), original->get_seam_sides(original_values));
            }
        }
    }
    std::vector<std::string> expected = recreated_originals();
    std::sort(paired.begin(), paired.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(paired, expected);
}

// The plain building blocks keep their originals' bounds at every value, so the two tile and stack alike.
TEST(Kit_recreated, basic_shapes_keep_their_originals_bounds)
{
    int checked = 0;
    int basic_count = 0;
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        if (shape.recreates.empty() || (shape.kit != "basic")) {
            continue;
        }
        ++basic_count;
        const Kit_shape_definition& original = get_shape(shape.recreates.c_str());
        std::vector<std::pair<Kit_parameter_values, Kit_parameter_values>> cases;
        cases.emplace_back(defaults(shape), defaults(original));
        cases.emplace_back(range_corner(shape, Range_end::min), range_corner(original, Range_end::min));
        cases.emplace_back(range_corner(shape, Range_end::max), range_corner(original, Range_end::max));
        for (const int seed : {1, 3, 17, 4242}) {
            cases.emplace_back(all_derived(shape, seed), all_derived(original, seed));
        }
        for (const std::pair<Kit_parameter_values, Kit_parameter_values>& entry : cases) {
            SCOPED_TRACE(fmt::format("{} {}", shape.id, entry.first.to_canonical_text()));
            const Kit_mesh_report report          = editor::check_kit_shape(entry.first);
            const Kit_mesh_report original_report = editor::check_kit_shape(entry.second);
            EXPECT_TRUE(report.is_valid()) << describe(report);
            const float tolerance = 1.0e-5f * std::max(1.0f, glm::length(original_report.bounds_max - original_report.bounds_min));
            EXPECT_LT(glm::distance(report.bounds_min, original_report.bounds_min), tolerance);
            EXPECT_LT(glm::distance(report.bounds_max, original_report.bounds_max), tolerance);
            ++checked;
        }
    }
    EXPECT_GT(basic_count, 0);
    EXPECT_EQ(checked, basic_count * 7);
}

// The recreated stairs' side walls are a column per step: no vertex gathers a fan of facets, at any step count.
TEST(Kit_recreated, stairs_sides_have_no_fan)
{
    const Kit_shape_definition& stairs = get_shape("basic.stairs.v2");
    for (const float rise : {0.2f, 1.0f, 3.0f, 16.0f}) {
        Kit_parameter_values values = defaults(stairs);
        ASSERT_TRUE(values.set_float("rise", rise));
        ASSERT_TRUE(values.set_float("step_rise_limit", (rise > 8.0f) ? 0.125f : 0.2f));
        ASSERT_EQ(values.validate(), "");
        const Kit_stairs_layout          layout = editor::get_stairs_layout(values);
        const std::unique_ptr<GEO::Mesh> mesh   = generate(values);
        SCOPED_TRACE(fmt::format("rise {}: {} steps", rise, layout.step_count));
        EXPECT_EQ(editor::count_kit_triangles(*mesh), static_cast<unsigned int>(12 * layout.step_count));
        std::vector<int> facets_at(mesh->vertices.nb(), 0);
        for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
            for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                ++facets_at[mesh->facets.vertex(f, c)];
            }
        }
        // A riser foot: its riser, the tread before it, two columns and two step triangles.
        EXPECT_LE(*std::max_element(facets_at.begin(), facets_at.end()), 6);
    }
}

// The recreated cylinder's mirror images are equal to the bit: about z = 0 always, about x = 0 at an even segment count.
TEST(Kit_recreated, cylinder_is_exactly_symmetric)
{
    const Kit_shape_definition& cylinder = get_shape("basic.cylinder.v2");
    for (const int segments : {3, 4, 5, 6, 7, 12, 16, 33, 64}) {
        Kit_parameter_values values = defaults(cylinder);
        ASSERT_TRUE(values.set_int("segments", segments));
        ASSERT_TRUE(values.set_float("radius", 0.7f));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::set<std::array<float, 3>> points;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
            points.insert({p.x, p.y, p.z});
        }
        SCOPED_TRACE(fmt::format("{} segments", segments));
        EXPECT_EQ(points.size(), static_cast<std::size_t>(2 * segments));
        for (const std::array<float, 3>& p : points) {
            EXPECT_TRUE(points.contains({p[0], p[1], -p[2]}));
            if ((segments % 2) == 0) {
                EXPECT_TRUE(points.contains({-p[0], p[1], p[2]}));
            }
        }
    }
}

} // anonymous namespace
