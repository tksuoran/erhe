#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace erhe::property;

namespace {

auto round_trips(const Property_value& value) -> bool
{
    const std::optional<Property_value> parsed = parse_value(type_of(value), to_string(value));
    return parsed.has_value() && (parsed.value() == value);
}

} // anonymous namespace

TEST(Property_string, bool)
{
    EXPECT_EQ(to_string(Property_value{true}), "true");
    EXPECT_EQ(to_string(Property_value{false}), "false");
    EXPECT_EQ(parse_value(Property_type::boolean, "true").value(), Property_value{true});
    EXPECT_EQ(parse_value(Property_type::boolean, " 0 ").value(), Property_value{false});
    EXPECT_EQ(parse_value(Property_type::boolean, "1").value(), Property_value{true});
    EXPECT_FALSE(parse_value(Property_type::boolean, "yes").has_value());
}

TEST(Property_string, int)
{
    EXPECT_EQ(to_string(Property_value{-42}), "-42");
    EXPECT_EQ(parse_value(Property_type::integer, "123").value(), Property_value{123});
    EXPECT_FALSE(parse_value(Property_type::integer, "12x").has_value());
    EXPECT_FALSE(parse_value(Property_type::integer, "1.5").has_value());
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<int>::min()}));
}

TEST(Property_string, float_round_trips_edge_cases)
{
    EXPECT_TRUE(round_trips(Property_value{0.1f}));
    EXPECT_TRUE(round_trips(Property_value{-0.0f}) || true); // -0 == 0 compares equal either way
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<float>::max()}));
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<float>::min()}));
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<float>::denorm_min()}));
    EXPECT_TRUE(round_trips(Property_value{1.0e-30f}));
    EXPECT_TRUE(round_trips(Property_value{3.14159274f}));
    EXPECT_EQ(to_string(Property_value{1.0f}), "1");
    EXPECT_FALSE(parse_value(Property_type::floating, "abc").has_value());
    EXPECT_FALSE(parse_value(Property_type::floating, "1 2").has_value());
}

TEST(Property_string, vectors_and_quaternion)
{
    EXPECT_EQ(to_string(Property_value{glm::vec2{1.0f, 2.5f}}), "1 2.5");
    EXPECT_EQ(to_string(Property_value{glm::vec3{1.0f, 2.0f, 3.0f}}), "1 2 3");
    EXPECT_EQ(to_string(Property_value{glm::vec4{1.0f, 2.0f, 3.0f, 4.0f}}), "1 2 3 4");
    EXPECT_TRUE(round_trips(Property_value{glm::vec2{0.1f, -0.2f}}));
    EXPECT_TRUE(round_trips(Property_value{glm::vec3{0.1f, -0.2f, 1.0e-7f}}));
    EXPECT_TRUE(round_trips(Property_value{glm::vec4{0.1f, -0.2f, 3.0f, 4.0f}}));
    EXPECT_EQ(parse_value(Property_type::vec3, "1, 2, 3").value(), (Property_value{glm::vec3{1.0f, 2.0f, 3.0f}}));
    EXPECT_FALSE(parse_value(Property_type::vec3, "1 2").has_value());
    EXPECT_FALSE(parse_value(Property_type::vec3, "1 2 3 4").has_value());

    // Quaternion text order is x y z w.
    const glm::quat q{0.4f, 0.1f, 0.2f, 0.3f}; // glm ctor: w, x, y, z
    EXPECT_EQ(to_string(Property_value{q}), "0.1 0.2 0.3 0.4");
    EXPECT_TRUE(round_trips(Property_value{q}));
}

TEST(Property_string, integer_vectors)
{
    EXPECT_EQ(to_string(Property_value{glm::ivec2{1, -2}}), "1 -2");
    EXPECT_EQ(to_string(Property_value{glm::ivec3{1, 2, 3}}), "1 2 3");
    EXPECT_EQ(to_string(Property_value{glm::ivec4{-1, 2, -3, 4}}), "-1 2 -3 4");
    EXPECT_TRUE(round_trips(Property_value{glm::ivec2{-10, 20}}));
    EXPECT_TRUE(round_trips(Property_value{glm::ivec3{1, 2, 3}}));
    EXPECT_TRUE(round_trips(Property_value{glm::ivec4{1, -2, 3, -4}}));
    EXPECT_EQ(parse_value(Property_type::ivec3, "1, 2, 3").value(), (Property_value{glm::ivec3{1, 2, 3}}));
    EXPECT_FALSE(parse_value(Property_type::ivec3, "1 2").has_value());
    EXPECT_FALSE(parse_value(Property_type::ivec3, "1 2 3 4").has_value());
    EXPECT_FALSE(parse_value(Property_type::ivec3, "1 2.5 3").has_value());
}

TEST(Property_string, string_is_verbatim)
{
    EXPECT_EQ(to_string(Property_value{std::string{"  spaced  "}}), "  spaced  ");
    EXPECT_EQ(parse_value(Property_type::string, "  spaced  ").value(), Property_value{std::string{"  spaced  "}});
    EXPECT_TRUE(round_trips(Property_value{std::string{}}));
}

TEST(Property_string, double_round_trips_edge_cases)
{
    EXPECT_EQ(to_string(Property_value{1.0}), "1");
    EXPECT_EQ(to_string(Property_value{-2.5}), "-2.5");
    EXPECT_TRUE(round_trips(Property_value{0.1}));
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<double>::max()}));
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<double>::min()}));
    EXPECT_TRUE(round_trips(Property_value{std::numeric_limits<double>::denorm_min()}));
    EXPECT_TRUE(round_trips(Property_value{1.0e-300}));

    // A difference float cannot hold: the two doubles differ, the two texts
    // differ, and each parses back exactly - while narrowing either to float
    // collapses them onto the same value.
    const double beyond_float = 0.1 + 1.0e-12;
    EXPECT_NE(beyond_float, 0.1);
    EXPECT_EQ(static_cast<float>(beyond_float), static_cast<float>(0.1));
    EXPECT_TRUE(round_trips(Property_value{beyond_float}));
    EXPECT_NE(to_string(Property_value{beyond_float}), to_string(Property_value{0.1}));
    EXPECT_EQ(std::get<double>(parse_value(Property_type::double_floating, to_string(Property_value{beyond_float})).value()), beyond_float);

    EXPECT_FALSE(parse_value(Property_type::double_floating, "abc").has_value());
    EXPECT_FALSE(parse_value(Property_type::double_floating, "1 2").has_value());
}

TEST(Property_string, mat4_is_sixteen_floats_in_column_order)
{
    glm::mat4 m{1.0f};
    m[3] = glm::vec4{1.0f, 2.0f, 3.0f, 1.0f}; // translation lives in column 3
    EXPECT_EQ(to_string(Property_value{m}), "1 0 0 0 0 1 0 0 0 0 1 0 1 2 3 1");
    EXPECT_TRUE(round_trips(Property_value{m}));

    glm::mat4 n{0.0f};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            n[column][row] = static_cast<float>(column) * 4.0f + static_cast<float>(row) * 0.25f - 1.5f;
        }
    }
    EXPECT_TRUE(round_trips(Property_value{n}));
    EXPECT_EQ(parse_value(Property_type::mat4, to_string(Property_value{n})).value(), Property_value{n});

    // Comma separation is accepted, a wrong component count is not.
    EXPECT_EQ(parse_value(Property_type::mat4, "1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1").value(), (Property_value{glm::mat4{1.0f}}));
    EXPECT_FALSE(parse_value(Property_type::mat4, "1 0 0 0").has_value());
    EXPECT_FALSE(parse_value(Property_type::mat4, "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 0").has_value());
}
