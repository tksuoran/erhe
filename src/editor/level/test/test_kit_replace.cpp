#include "level/kit_replace.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"

#include <gtest/gtest.h>

#include <algorithm>

#include <optional>
#include <string>
#include <vector>

namespace {

using editor::Kit_carry;
using editor::Kit_derive;
using editor::Kit_lengths;
using editor::Kit_parameter;
using editor::Kit_parameter_carry;
using editor::Kit_parameter_type;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::Kit_shape_library;
using editor::Kit_value_source;

const Kit_lengths c_lengths{.module = 1.0f, .wall_height = 3.0f};

auto get_library() -> const Kit_shape_library&
{
    static const Kit_shape_library library;
    return library;
}

auto float_parameter(const char* name, const float value, const float min_value, const float max_value) -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name          = name;
    parameter.type          = Kit_parameter_type::float_value;
    parameter.float_default = value;
    parameter.float_min     = min_value;
    parameter.float_max     = max_value;
    return parameter;
}

auto int_parameter(const char* name, const int value, const int min_value, const int max_value) -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name        = name;
    parameter.type        = Kit_parameter_type::int_value;
    parameter.unit        = editor::Kit_unit::count;
    parameter.int_default = value;
    parameter.int_min     = min_value;
    parameter.int_max     = max_value;
    return parameter;
}

auto enum_parameter(const char* name, const std::vector<std::string>& names, const int value) -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name        = name;
    parameter.type        = Kit_parameter_type::enum_value;
    parameter.unit        = editor::Kit_unit::none;
    parameter.enum_values = names;
    parameter.int_default = value;
    parameter.int_max     = static_cast<int>(names.size()) - 1;
    return parameter;
}

auto derivable(Kit_parameter parameter, const float min_value, const float max_value) -> Kit_parameter
{
    parameter.derive            = Kit_derive::derivable;
    parameter.derive_float_min  = min_value;
    parameter.derive_float_max  = max_value;
    parameter.derive_float_step = 0.0f;
    return parameter;
}

auto carry_of(const std::vector<Kit_parameter_carry>& carry, const std::string& name) -> std::optional<Kit_carry>
{
    for (const Kit_parameter_carry& entry : carry) {
        if (entry.name == name) {
            return entry.carry;
        }
    }
    return {};
}

auto make_from_shape() -> Kit_shape_definition
{
    Kit_shape_definition shape;
    shape.id         = "test.from";
    shape.parameters = {
        float_parameter("size",  5.0f, 0.0f, 10.0f),
        int_parameter  ("count", 4, 0, 5),
        enum_parameter ("form",  {"a", "b", "c"}, 0),
        float_parameter("kind",  1.0f, 0.0f, 2.0f),
        derivable(float_parameter("jitter", 0.5f, 0.0f, 1.0f), 0.0f, 1.0f),
        int_parameter  ("seed",  7, 0, editor::kit_seed_max)
    };
    return shape;
}

auto make_to_shape() -> Kit_shape_definition
{
    Kit_shape_definition shape;
    shape.id         = "test.to";
    shape.parameters = {
        float_parameter("size",  1.5f, 1.0f, 2.0f),
        int_parameter  ("count", 1, 0, 2),
        enum_parameter ("form",  {"b", "c"}, 1),
        int_parameter  ("kind",  0, 0, 3),
        derivable(float_parameter("jitter", 0.25f, 0.2f, 0.4f), 0.2f, 0.4f),
        float_parameter("extra", 0.75f, 0.0f, 1.0f),
        int_parameter  ("seed",  1, 0, editor::kit_seed_max)
    };
    return shape;
}

TEST(Kit_replace, same_name_and_type_carries_clamped_into_the_new_range)
{
    const Kit_shape_definition from_shape = make_from_shape();
    const Kit_shape_definition to_shape   = make_to_shape();
    Kit_parameter_values from = editor::make_default_values(from_shape, c_lengths.module, c_lengths.wall_height);
    ASSERT_TRUE(from.set_float("size", 1.25f));
    ASSERT_TRUE(from.set_int("count", 2));
    ASSERT_TRUE(from.set_enum("form", "c"));
    ASSERT_TRUE(from.set_int("seed", 42));

    Kit_parameter_values             to;
    std::vector<Kit_parameter_carry> carry;
    editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
    ASSERT_EQ(to.get_shape(), &to_shape);
    ASSERT_EQ(carry.size(), to_shape.parameters.size());
    EXPECT_EQ(to.get_float("size"), 1.25f);
    EXPECT_EQ(carry_of(carry, "size"), Kit_carry::carried);
    EXPECT_EQ(to.get_int("count"), 2);
    EXPECT_EQ(carry_of(carry, "count"), Kit_carry::carried);
    EXPECT_EQ(to.get_enum("form"), "c");
    EXPECT_EQ(carry_of(carry, "form"), Kit_carry::carried);
    EXPECT_EQ(to.get_int("seed"), 42);
    EXPECT_EQ(carry_of(carry, "seed"), Kit_carry::carried);
    EXPECT_TRUE(to.validate().empty()) << to.validate();

    // Out of the new range: clamped to its ends.
    ASSERT_TRUE(from.set_float("size", 9.0f));
    ASSERT_TRUE(from.set_int("count", 5));
    editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
    EXPECT_EQ(to.get_float("size"), 2.0f);
    EXPECT_EQ(carry_of(carry, "size"), Kit_carry::clamped);
    EXPECT_EQ(to.get_int("count"), 2);
    EXPECT_EQ(carry_of(carry, "count"), Kit_carry::clamped);
    ASSERT_TRUE(from.set_float("size", 0.25f));
    editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
    EXPECT_EQ(to.get_float("size"), 1.0f);
    EXPECT_TRUE(to.validate().empty()) << to.validate();
}

TEST(Kit_replace, other_type_missing_name_and_missing_enum_name_take_the_defaults)
{
    const Kit_shape_definition from_shape = make_from_shape();
    const Kit_shape_definition to_shape   = make_to_shape();
    Kit_parameter_values from = editor::make_default_values(from_shape, c_lengths.module, c_lengths.wall_height);
    ASSERT_TRUE(from.set_enum("form", "a")); // the new shape has no "a"
    ASSERT_TRUE(from.set_float("kind", 2.0f)); // a float, the new shape's kind an int

    Kit_parameter_values             to;
    std::vector<Kit_parameter_carry> carry;
    editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
    EXPECT_EQ(to.get_enum("form"), "c");
    EXPECT_EQ(carry_of(carry, "form"), Kit_carry::defaulted);
    EXPECT_EQ(to.get_int("kind"), 0);
    EXPECT_EQ(carry_of(carry, "kind"), Kit_carry::defaulted);
    EXPECT_EQ(to.get_float("extra"), 0.75f);
    EXPECT_EQ(carry_of(carry, "extra"), Kit_carry::defaulted);
    EXPECT_FALSE(carry_of(carry, "jitter") == Kit_carry::defaulted);
}

TEST(Kit_replace, seed_source_carries_and_is_derived_again_in_the_new_range)
{
    const Kit_shape_definition from_shape = make_from_shape();
    const Kit_shape_definition to_shape   = make_to_shape();
    const std::size_t from_jitter = from_shape.find_parameter("jitter").value();
    const std::size_t to_jitter   = to_shape.find_parameter("jitter").value();
    Kit_parameter_values from = editor::make_default_values(from_shape, c_lengths.module, c_lengths.wall_height);
    from.set_value_source(from_jitter, Kit_value_source::seed);
    from.resolve_derived(c_lengths.module, c_lengths.wall_height);

    Kit_parameter_values             to;
    std::vector<Kit_parameter_carry> carry;
    editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
    EXPECT_EQ(to.get_value_source(to_jitter), Kit_value_source::seed);
    EXPECT_EQ(carry_of(carry, "jitter"), Kit_carry::carried);
    EXPECT_GE(to.get_float("jitter"), 0.2f);
    EXPECT_LE(to.get_float("jitter"), 0.4f);
    EXPECT_EQ(to.get_values()[to_jitter], editor::derive_parameter_value(to_shape.parameters[to_jitter], to.get_seed(), c_lengths.module, c_lengths.wall_height));

    // An explicit value stays explicit, clamped.
    from.set_value_source(from_jitter, Kit_value_source::explicit_value);
    ASSERT_TRUE(from.set_float("jitter", 0.9f));
    editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
    EXPECT_EQ(to.get_value_source(to_jitter), Kit_value_source::explicit_value);
    EXPECT_EQ(to.get_float("jitter"), 0.4f);
    EXPECT_EQ(carry_of(carry, "jitter"), Kit_carry::clamped);
}

TEST(Kit_replace, a_rederived_value_is_carried_even_when_the_old_value_lay_outside_the_new_range)
{
    Kit_shape_definition from_shape = make_from_shape();
    const std::size_t    from_jitter = from_shape.find_parameter("jitter").value();
    from_shape.parameters[from_jitter].derive_float_min = 0.6f; // every derived old value above the new 0.4 maximum
    from_shape.parameters[from_jitter].derive_float_max = 1.0f;
    const Kit_shape_definition to_shape = make_to_shape();
    Kit_parameter_values from = editor::make_default_values(from_shape, c_lengths.module, c_lengths.wall_height);
    from.set_value_source(from_jitter, Kit_value_source::seed);
    from.resolve_derived(c_lengths.module, c_lengths.wall_height);
    ASSERT_GT(from.get_float("jitter"), 0.4f);

    Kit_parameter_values             to;
    std::vector<Kit_parameter_carry> carry;
    editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
    EXPECT_EQ(carry_of(carry, "jitter"), Kit_carry::carried);
    EXPECT_LE(to.get_float("jitter"), 0.4f);
}

TEST(Kit_replace, a_seed_derived_value_on_a_fixed_new_parameter_carries_explicitly)
{
    const Kit_shape_definition from_shape = make_from_shape();
    Kit_shape_definition       to_shape   = make_to_shape();
    const std::size_t to_jitter = to_shape.find_parameter("jitter").value();
    to_shape.parameters[to_jitter].derive = Kit_derive::fixed;
    Kit_parameter_values from = editor::make_default_values(from_shape, c_lengths.module, c_lengths.wall_height);
    from.set_value_source(from_shape.find_parameter("jitter").value(), Kit_value_source::seed);
    from.resolve_derived(c_lengths.module, c_lengths.wall_height);
    const float old_value = from.get_float("jitter");

    Kit_parameter_values             to;
    std::vector<Kit_parameter_carry> carry;
    editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
    EXPECT_EQ(to.get_value_source(to_jitter), Kit_value_source::explicit_value);
    const float expected = std::clamp(old_value, 0.2f, 0.4f);
    EXPECT_EQ(to.get_float("jitter"), expected);
    EXPECT_EQ(carry_of(carry, "jitter"), (expected == old_value) ? Kit_carry::carried : Kit_carry::clamped);
    EXPECT_TRUE(to.validate().empty()) << to.validate();
}

TEST(Kit_replace, a_seed_derived_enum_stays_derived)
{
    Kit_shape_definition from_shape;
    from_shape.id         = "test.enum_from";
    from_shape.parameters = {
        editor::derive_enum(enum_parameter("form", {"a", "b", "c"}, 0)),
        int_parameter("seed", 5, 0, editor::kit_seed_max)
    };
    Kit_shape_definition to_shape;
    to_shape.id         = "test.enum_to";
    to_shape.parameters = {
        int_parameter("seed", 1, 0, editor::kit_seed_max),
        editor::derive_enum(enum_parameter("form", {"a", "b", "c", "d"}, 3))
    };
    Kit_parameter_values from = editor::make_default_values(from_shape, c_lengths.module, c_lengths.wall_height);
    from.set_value_source(0, Kit_value_source::seed);
    from.resolve_derived(c_lengths.module, c_lengths.wall_height);

    Kit_parameter_values             to;
    std::vector<Kit_parameter_carry> carry;
    editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
    EXPECT_EQ(to.get_value_source(1), Kit_value_source::seed);
    EXPECT_EQ(carry_of(carry, "form"), Kit_carry::carried);
    EXPECT_EQ(to.get_int("seed"), 5);
    EXPECT_EQ(to.get_values()[1], editor::derive_parameter_value(to_shape.parameters[1], 5, c_lengths.module, c_lengths.wall_height));
    EXPECT_TRUE(to.validate().empty()) << to.validate();
}

TEST(Kit_replace, kit_shapes_carry_the_seed)
{
    const Kit_shape_library& library = get_library();
    const Kit_shape_definition* const block = library.find("basic.block");
    const Kit_shape_definition* const rock  = library.find("nature.rock");
    ASSERT_NE(block, nullptr);
    ASSERT_NE(rock, nullptr);
    Kit_parameter_values from = editor::make_default_values(*block, c_lengths.module, c_lengths.wall_height);
    ASSERT_TRUE(from.set_int("seed", 1234));
    ASSERT_TRUE(from.set_float("width", 2.0f));

    Kit_parameter_values             to;
    std::vector<Kit_parameter_carry> carry;
    editor::carry_parameter_values(from, *rock, c_lengths, to, carry);
    EXPECT_EQ(to.get_int("seed"), 1234);
    EXPECT_EQ(carry_of(carry, "seed"), Kit_carry::carried);
    EXPECT_EQ(carry_of(carry, "size"), Kit_carry::defaulted);
    // The rock's size derives from the seed by default: derived from the carried seed.
    Kit_parameter_values expected = editor::make_default_values(*rock, c_lengths.module, c_lengths.wall_height);
    ASSERT_TRUE(expected.set_int("seed", 1234));
    expected.resolve_derived(c_lengths.module, c_lengths.wall_height);
    EXPECT_TRUE(to == expected);
}

TEST(Kit_replace, defaults_of_every_shape_carry_to_valid_values_of_every_shape)
{
    const Kit_shape_library& library = get_library();
    Kit_parameter_values             to;
    std::vector<Kit_parameter_carry> carry;
    for (const Kit_shape_definition& from_shape : library.get_shapes()) {
        const Kit_parameter_values from = editor::make_default_values(from_shape, c_lengths.module, c_lengths.wall_height);
        for (const Kit_shape_definition& to_shape : library.get_shapes()) {
            editor::carry_parameter_values(from, to_shape, c_lengths, to, carry);
            EXPECT_TRUE(to.validate().empty()) << from_shape.id << " -> " << to_shape.id << ": " << to.validate();
            if (&from_shape == &to_shape) {
                EXPECT_TRUE(to == from) << from_shape.id;
            }
        }
    }
}

TEST(Kit_replace, slots_match_by_name)
{
    const std::vector<std::string> tree {"bark", "foliage"};
    const std::vector<std::string> bush {"foliage"};
    const std::vector<std::string> block{"stone"};
    std::vector<std::optional<std::size_t>> matches;
    editor::match_material_slots(tree, bush, matches);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], std::optional<std::size_t>{1});
    editor::match_material_slots(bush, tree, matches);
    ASSERT_EQ(matches.size(), 2u);
    EXPECT_FALSE(matches[0].has_value());
    EXPECT_EQ(matches[1], std::optional<std::size_t>{0});
    editor::match_material_slots(block, bush, matches);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_FALSE(matches[0].has_value());
}

} // anonymous namespace
