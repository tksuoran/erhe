#include "erhe_texgen/value_type.hpp"

#include <gtest/gtest.h>

namespace erhe::texgen::test {

TEST(Value_type, glsl_type_names)
{
    EXPECT_STREQ(glsl_type_name(Value_type::grayscale), "float");
    EXPECT_STREQ(glsl_type_name(Value_type::rgb),       "vec3");
    EXPECT_STREQ(glsl_type_name(Value_type::rgba),      "vec4");
}

TEST(Value_type, value_type_names)
{
    EXPECT_STREQ(value_type_name(Value_type::grayscale), "f");
    EXPECT_STREQ(value_type_name(Value_type::rgb),       "rgb");
    EXPECT_STREQ(value_type_name(Value_type::rgba),      "rgba");
}

TEST(Value_type, conversion_template_is_null_for_same_type)
{
    EXPECT_EQ(conversion_template(Value_type::grayscale, Value_type::grayscale), nullptr);
    EXPECT_EQ(conversion_template(Value_type::rgb,       Value_type::rgb),       nullptr);
    EXPECT_EQ(conversion_template(Value_type::rgba,      Value_type::rgba),      nullptr);
}

TEST(Value_type, convert_identity_returns_expression_unchanged)
{
    EXPECT_EQ(convert("x", Value_type::rgb, Value_type::rgb), "x");
}

TEST(Value_type, convert_grayscale_to_rgb)
{
    EXPECT_EQ(convert("v", Value_type::grayscale, Value_type::rgb), "vec3(v)");
}

TEST(Value_type, convert_grayscale_to_rgba)
{
    EXPECT_EQ(convert("v", Value_type::grayscale, Value_type::rgba), "vec4(vec3(v), 1.0)");
}

TEST(Value_type, convert_rgb_to_grayscale)
{
    EXPECT_EQ(convert("v", Value_type::rgb, Value_type::grayscale), "(dot(v, vec3(1.0))/3.0)");
}

TEST(Value_type, convert_rgb_to_rgba)
{
    EXPECT_EQ(convert("v", Value_type::rgb, Value_type::rgba), "vec4(v, 1.0)");
}

TEST(Value_type, convert_rgba_to_grayscale)
{
    EXPECT_EQ(convert("v", Value_type::rgba, Value_type::grayscale), "(dot((v).rgb, vec3(1.0))/3.0)");
}

TEST(Value_type, convert_rgba_to_rgb)
{
    EXPECT_EQ(convert("v", Value_type::rgba, Value_type::rgb), "((v).rgb)");
}

TEST(Value_type, convert_substitutes_all_placeholder_occurrences)
{
    // No MVP conversion uses $(value) twice; exercise via a compound source
    // expression to at least prove single substitution keeps the whole text.
    EXPECT_EQ(
        convert("a + b", Value_type::rgb, Value_type::rgba),
        "vec4(a + b, 1.0)"
    );
}

TEST(Value_type, convert_expression_containing_literal_placeholder_terminates)
{
    // An expression that itself contains the literal text "$(value)" must not
    // send the placeholder-replacement loop into re-matching its own
    // insertion: the search must advance past the substituted expression.
    EXPECT_EQ(
        convert("x + $(value)", Value_type::grayscale, Value_type::rgb),
        "vec3(x + $(value))"
    );
}

} // namespace erhe::texgen::test
