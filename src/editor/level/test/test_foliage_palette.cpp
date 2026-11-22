#include "level/foliage_palette.hpp"
#include "level/kit_layout_values.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"

#include "kit_shape_test_support.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using editor::Foliage_palette_entry;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;

[[nodiscard]] auto bits(const float value) -> std::uint32_t
{
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] auto find_parameter(const Kit_shape_definition& shape, const char* name) -> std::size_t
{
    for (std::size_t i = 0; i < shape.parameters.size(); ++i) {
        if (shape.parameters[i].name == name) {
            return i;
        }
    }
    ADD_FAILURE() << "no parameter " << name;
    return 0;
}

// A bush with an explicit size; one not above zero is out of its range (L23).
[[nodiscard]] auto bush_parameters(const float size) -> std::string
{
    const Kit_shape_definition& shape = editor::kit_test::get_shape("nature.bush");
    Kit_parameter_values values = editor::kit_test::defaults(shape);
    values.set_value_source(find_parameter(shape, "size"), editor::Kit_value_source::explicit_value);
    EXPECT_TRUE(values.set_float("size", size));
    return values.to_canonical_text();
}

[[nodiscard]] auto make_entries() -> std::vector<Foliage_palette_entry>
{
    std::vector<Foliage_palette_entry> entries;
    entries.push_back(
        Foliage_palette_entry{
            .shape           = "nature.bush",
            .parameters      = bush_parameters(1.3f),
            .materials       = {"Kit Leaf", ""},
            .weight          = 1000,
            .radius          = 0.1f,
            .yaw_min_degrees = -0.0f,
            .yaw_max_degrees = 359.99f,
            .variants        = 64,
            .seed            = 123456
        }
    );
    entries.push_back(
        Foliage_palette_entry{
            .shape           = "basic.block",
            .parameters      = {},
            .materials       = {},
            .weight          = 0,
            .radius          = std::numeric_limits<float>::denorm_min(),
            .yaw_min_degrees = 1.0e-30f,
            .yaw_max_degrees = 1.0e-30f,
            .variants        = 1,
            .seed            = 0
        }
    );
    return entries;
}

void expect_bit_equal(const Foliage_palette_entry& a, const Foliage_palette_entry& b)
{
    EXPECT_EQ(a.shape,      b.shape);
    EXPECT_EQ(a.parameters, b.parameters);
    EXPECT_EQ(a.materials,  b.materials);
    EXPECT_EQ(a.weight,     b.weight);
    EXPECT_EQ(bits(a.radius),          bits(b.radius));
    EXPECT_EQ(bits(a.yaw_min_degrees), bits(b.yaw_min_degrees));
    EXPECT_EQ(bits(a.yaw_max_degrees), bits(b.yaw_max_degrees));
    EXPECT_EQ(a.variants, b.variants);
    EXPECT_EQ(a.seed,     b.seed);
}

[[nodiscard]] auto check_one(const Foliage_palette_entry& entry) -> std::string
{
    const Foliage_palette_entry entries[] = {make_entries()[0], entry};
    return editor::check_foliage_palette(editor::kit_test::get_library(), entries);
}

} // namespace

TEST(Foliage_palette, text_round_trip_is_bit_exact)
{
    const std::vector<Foliage_palette_entry> entries = make_entries();
    const std::string text = editor::make_foliage_palette_text(entries);
    std::vector<Foliage_palette_entry> parsed;
    ASSERT_EQ(editor::parse_foliage_palette_text(text, parsed), "");
    ASSERT_EQ(parsed.size(), entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        expect_bit_equal(parsed[i], entries[i]);
    }
    EXPECT_EQ(editor::make_foliage_palette_text(parsed), text);

    // Sorted keys, the floats as hexadecimal strings.
    EXPECT_LT(text.find("\"materials\""), text.find("\"parameters\""));
    EXPECT_LT(text.find("\"yaw_max\""),   text.find("\"yaw_min\""));
    EXPECT_NE(text.find("\"-0x0p+0\""), std::string::npos) << text;

    std::vector<Foliage_palette_entry> empty;
    ASSERT_EQ(editor::parse_foliage_palette_text("[]", empty), "");
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(editor::make_foliage_palette_text({}), "[]");
}

TEST(Foliage_palette, text_reads_defaults)
{
    std::vector<Foliage_palette_entry> parsed;
    ASSERT_EQ(editor::parse_foliage_palette_text(R"([{"shape":"nature.bush","parameters":"{\"twigs\":2}"}])", parsed), "");
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].parameters, R"({"twigs":2})");
    EXPECT_EQ(parsed[0].weight, Foliage_palette_entry{}.weight);
    EXPECT_EQ(parsed[0].variants, Foliage_palette_entry{}.variants);
}

namespace {

// The parse's refusal, expected to contain the text.
void expect_refusal(const char* text, const char* contains)
{
    std::vector<Foliage_palette_entry> parsed = make_entries();
    const std::size_t count = parsed.size();
    const std::string error = editor::parse_foliage_palette_text(text, parsed);
    EXPECT_NE(error.find(contains), std::string::npos) << text << " -> '" << error << "'";
    // A refused text leaves the entries as they were.
    EXPECT_EQ(parsed.size(), count);
}

} // namespace

TEST(Foliage_palette, text_refusals)
{
    expect_refusal("{}",       "not a JSON list");
    expect_refusal("not json", "syntax error at byte 2");
    expect_refusal("[{\"shape\":\"nature.bush\",}]", "syntax error at byte 25");
    expect_refusal("[1]",      "entry 0: it is not an object");
    expect_refusal(R"([{"shape":"nature.bush","seed":1e999}])", "a number out of range at byte");
    expect_refusal(R"([{"weight":1}])", "'shape' is missing");
    expect_refusal(R"([{"shape":"nature.bush","colour":1}])", "'colour' is not an entry key");
    expect_refusal(R"([{"shape":"nature.bush","shape":"basic.block"}])", "repeats the key 'shape'");
    expect_refusal(R"([{"shape":"nature.bush"},{"shape":"basic.block","seed":1,"seed":2}])", "repeats the key 'seed'");
    expect_refusal(R"([{"shape":"nature.bush","radius":1.0}])", "'radius' is not a hexadecimal float string");
    expect_refusal(R"([{"shape":"nature.bush","weight":-1}])", "'weight' is -1, outside");
    expect_refusal(R"([{"shape":"nature.bush","weight":4294967296}])", "'weight' is 4294967296, outside");
    expect_refusal(R"([{"shape":"nature.bush","seed":18446744073709551615}])", "'seed' is 18446744073709551615, outside");
    expect_refusal(R"([{"shape":"nature.bush","seed":9223372036854775808}])", "'seed' is 9223372036854775808, outside");
    expect_refusal(R"([{"shape":"nature.bush","seed":2147483648}])", "'seed' is 2147483648, outside");
    expect_refusal(R"([{"shape":"nature.bush","variants":-2147483649}])", "'variants' is -2147483649, outside");
    expect_refusal(R"([{"shape":"nature.bush","seed":1.5}])", "'seed' is not an integer");
    expect_refusal(R"([{"shape":"nature.bush","materials":[1]}])", "'materials' is not a list of names");
    expect_refusal(R"([{"shape":"nature.bush","parameters":3}])", "'parameters' is not a string");
    expect_refusal(R"([{"shape":"nature.bush","parameters":{"twigs":2}}])", "'parameters' is not a string");

    // The ends of the integer ranges read.
    std::vector<Foliage_palette_entry> parsed;
    ASSERT_EQ(editor::parse_foliage_palette_text(R"([{"shape":"nature.bush","seed":2147483647,"variants":-2147483648,"weight":4294967295}])", parsed), "");
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].seed, std::numeric_limits<int>::max());
    EXPECT_EQ(parsed[0].variants, std::numeric_limits<int>::min());
    EXPECT_EQ(parsed[0].weight, std::numeric_limits<std::uint32_t>::max());
    // The same key in two objects is not a repeat.
    ASSERT_EQ(editor::parse_foliage_palette_text(R"([{"shape":"nature.bush"},{"shape":"basic.block"}])", parsed), "");
    EXPECT_EQ(parsed.size(), 2u);
}

TEST(Foliage_palette, check_refusals)
{
    const editor::Kit_shape_library& library = editor::kit_test::get_library();
    const std::vector<Foliage_palette_entry> entries = make_entries();
    EXPECT_EQ(editor::check_foliage_palette(library, entries), "");
    EXPECT_EQ(editor::check_foliage_palette(library, {}), "");

    Foliage_palette_entry unknown = entries[0];
    unknown.shape = "nature.no_such_shape";
    const std::string unknown_error = check_one(unknown);
    EXPECT_NE(unknown_error.find("entry 1"), std::string::npos) << unknown_error;
    EXPECT_NE(unknown_error.find("no shape 'nature.no_such_shape'"), std::string::npos) << unknown_error;

    Foliage_palette_entry out_of_range = entries[0];
    out_of_range.parameters = bush_parameters(-1.0f);
    const std::string range_error = check_one(out_of_range);
    EXPECT_NE(range_error.find("entry 1"), std::string::npos) << range_error;
    EXPECT_NE(range_error.find("parameter 'size'"), std::string::npos) << range_error;

    Foliage_palette_entry unreadable = entries[0];
    unreadable.parameters = "{\"size\":";
    EXPECT_NE(check_one(unreadable).find("do not read"), std::string::npos);

    // A key repeated inside the parameters text reaches the kit parameter parse at check time.
    Foliage_palette_entry repeated = entries[0];
    repeated.parameters = R"({"size":"0x1p+0","size":"0x1p+1","twigs":3})";
    const std::string repeated_error = check_one(repeated);
    EXPECT_NE(repeated_error.find("parameter 'size' appears twice"), std::string::npos) << "a repeated parameter key is accepted: '" << repeated_error << "'";

    Foliage_palette_entry heavy = entries[0];
    heavy.weight = 1001;
    EXPECT_NE(check_one(heavy).find("the weight is 1001, outside 0 - 1000"), std::string::npos);

    Foliage_palette_entry yaw = entries[0];
    yaw.yaw_min_degrees = 90.0f;
    yaw.yaw_max_degrees = 45.0f;
    EXPECT_NE(check_one(yaw).find("yaw limits are out of order"), std::string::npos);

    Foliage_palette_entry materials = entries[0];
    materials.materials = {"a", "b", "c"};
    EXPECT_NE(check_one(materials).find("3 materials for the shape's 2 slots"), std::string::npos);

    Foliage_palette_entry variants = entries[0];
    variants.variants = 0;
    EXPECT_NE(check_one(variants).find("the variants are"), std::string::npos);
    variants.variants = 65;
    EXPECT_NE(check_one(variants).find("the variants are"), std::string::npos);

    Foliage_palette_entry radius = entries[0];
    for (const float value : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        radius.radius = value;
        EXPECT_NE(check_one(radius).find("the radius is"), std::string::npos) << value;
    }

    Foliage_palette_entry not_finite = entries[0];
    for (const float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        not_finite.yaw_min_degrees = value;
        not_finite.yaw_max_degrees = 360.0f;
        EXPECT_NE(check_one(not_finite).find("yaw limits are not finite"), std::string::npos) << value;
        not_finite.yaw_min_degrees = 0.0f;
        not_finite.yaw_max_degrees = value;
        EXPECT_NE(check_one(not_finite).find("yaw limits are not finite"), std::string::npos) << value;
    }

    // A palette whose weights sum to 0 is not refused (F3.3: nothing to paint).
    Foliage_palette_entry idle = entries[0];
    idle.weight = 0;
    EXPECT_EQ(check_one(idle), "");
}

// FD7.4: the shape's combination check runs on every variant's derived values at the palette's check, not only when a
// stroke begins; explicit values are refused by the parameters' read (validate() runs the same check).
TEST(Foliage_palette, check_runs_the_shape_combination_check)
{
    const Kit_shape_definition& shape = editor::kit_test::get_shape("architecture.stairs");
    ASSERT_TRUE(static_cast<bool>(shape.check_combination));
    Kit_parameter_values values = editor::kit_test::defaults(shape);
    ASSERT_TRUE(values.set_float("rise", 4.0f));
    ASSERT_TRUE(values.set_float("step_rise_limit", 0.05f));
    Foliage_palette_entry stairs{.shape = "architecture.stairs", .parameters = values.to_canonical_text(), .radius = 1.0f, .variants = 1};
    const std::string explicit_error = check_one(stairs);
    EXPECT_NE(explicit_error.find("entry 1"), std::string::npos) << explicit_error;
    EXPECT_NE(explicit_error.find("more than 64"), std::string::npos) << explicit_error;

    // The rise drawn from each variant's seed: some variants make more than 64 steps of 0.05 m.
    ASSERT_TRUE(values.set_float("rise", 1.0f));
    values.set_value_source(find_parameter(shape, "rise"), editor::Kit_value_source::seed);
    values.resolve_derived(1.0f, 3.0f);
    ASSERT_EQ(values.validate(), "");
    stairs.parameters = values.to_canonical_text();
    stairs.variants   = 64;
    const std::string variant_error = check_one(stairs);
    EXPECT_NE(variant_error.find(": variant "), std::string::npos) << variant_error;
    EXPECT_NE(variant_error.find("more than 64"), std::string::npos) << variant_error;
    stairs.variants = 1;
    std::size_t passing = 0;
    for (int seed = 0; seed < 64; ++seed) {
        stairs.seed = seed;
        passing += check_one(stairs).empty() ? 1u : 0u;
    }
    EXPECT_GT(passing, 0u) << "every single-variant seed is refused: the check refuses more than the variants it paints";

    stairs.parameters.clear();
    stairs.seed = 0;
    EXPECT_EQ(check_one(stairs), "") << "the shape's defaults pass its combination check";
}

TEST(Foliage_palette, variant_seed_and_count)
{
    EXPECT_EQ(editor::get_foliage_variant_seed(77, 3), editor::derive_layout_element_seed(77, 3));
    EXPECT_NE(editor::get_foliage_variant_seed(77, 3), editor::get_foliage_variant_seed(77, 4));

    Foliage_palette_entry entry;
    entry.variants = 8;
    const Kit_parameter_values bush  = editor::kit_test::defaults(editor::kit_test::get_shape("nature.bush"));
    const Kit_parameter_values block = editor::kit_test::defaults(editor::kit_test::get_shape("basic.block"));
    EXPECT_EQ(editor::get_foliage_variant_count(entry, bush), 8u);
    EXPECT_EQ(editor::get_foliage_variant_count(entry, block), 1u);
}
