#include "kit_shape_test_support.hpp"

#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

// The level of detail every shape takes (doc/editor/level_kit_shapes.md "Level of detail").
namespace {

using editor::Kit_mesh_report;
using editor::Kit_parameter;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using namespace editor::kit_test;

[[nodiscard]] auto with_detail(const Kit_shape_definition& shape, const float detail) -> Kit_parameter_values
{
    Kit_parameter_values values = defaults(shape);
    EXPECT_TRUE(values.set_float(editor::c_kit_detail_parameter, detail));
    return values;
}

TEST(Kit_detail, every_shape_takes_the_detail_last)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        SCOPED_TRACE(shape.id);
        ASSERT_FALSE(shape.parameters.empty());
        const Kit_parameter& detail = shape.parameters.back();
        EXPECT_EQ(detail.name, editor::c_kit_detail_parameter);
        EXPECT_EQ(detail.type, editor::Kit_parameter_type::float_value);
        EXPECT_EQ(detail.unit, editor::Kit_unit::ratio);
        EXPECT_EQ(detail.float_default, editor::c_kit_detail_default);
        EXPECT_EQ(detail.float_min, editor::c_kit_detail_min);
        EXPECT_EQ(detail.float_max, 1.0f);
        EXPECT_EQ(detail.derive, editor::Kit_derive::fixed);
        EXPECT_EQ(detail.text_presence, editor::Kit_text_presence::unless_default);
        EXPECT_TRUE(shape.detail_counts.empty() || (shape.detail_response == editor::Kit_detail_response::scaled));
        for (const editor::Kit_detail_count& count : shape.detail_counts) {
            const std::optional<std::size_t> index = shape.find_parameter(count.parameter);
            ASSERT_TRUE(index.has_value()) << count.parameter;
            EXPECT_EQ(shape.parameters[index.value()].type, editor::Kit_parameter_type::int_value) << count.parameter;
            EXPECT_GE(count.dimensions, 1);
        }
    }
}

// The default detail is left out of the text, so records written before the parameter read and keep their mesh key.
TEST(Kit_detail, the_default_detail_is_left_out_of_the_text)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        SCOPED_TRACE(shape.id);
        const Kit_parameter_values full = defaults(shape);
        const std::string          text = full.to_canonical_text();
        EXPECT_EQ(text.find("\"detail\""), std::string::npos) << text;
        Kit_parameter_values read{shape};
        ASSERT_EQ(editor::parse_canonical_text(shape, text, read), "");
        EXPECT_EQ(read.get_float(editor::c_kit_detail_parameter), editor::c_kit_detail_default);
        EXPECT_EQ(read, full);

        const Kit_parameter_values lower      = with_detail(shape, 0.25f);
        const std::string          lower_text = lower.to_canonical_text();
        EXPECT_NE(lower_text.find("\"detail\":\"0x1p-2\""), std::string::npos) << lower_text;
        ASSERT_EQ(editor::parse_canonical_text(shape, lower_text, read), "");
        EXPECT_EQ(read, lower);
    }
}

// The value sets the detail is checked at: the defaults and every value of each enum parameter.
[[nodiscard]] auto detail_value_sets(const Kit_shape_definition& shape) -> std::vector<Kit_parameter_values>
{
    std::vector<Kit_parameter_values> sets{defaults(shape)};
    for (const Kit_parameter& parameter : shape.parameters) {
        if (parameter.type != editor::Kit_parameter_type::enum_value) {
            continue;
        }
        for (const std::string& value_name : parameter.enum_values) {
            Kit_parameter_values values = defaults(shape);
            EXPECT_TRUE(values.set_enum(parameter.name, value_name));
            sets.push_back(values);
        }
    }
    return sets;
}

// A shape with detail counts makes no fewer triangles at a higher detail, fewer than its own at the least detail and
// more at the most for some of its forms, and passes the mesh checks at every detail; a shape without makes the same
// mesh at every detail.
TEST(Kit_detail, the_detail_scales_the_counts_of_the_shapes_that_state_them)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        SCOPED_TRACE(shape.id);
        bool fewer = false;
        bool more  = false;
        for (Kit_parameter_values values : detail_value_sets(shape)) {
            const unsigned int own = editor::count_kit_triangles(*generate(values));
            if (shape.detail_response == editor::Kit_detail_response::fixed) {
                const std::unique_ptr<GEO::Mesh> mesh = generate(values);
                for (const float detail : {editor::c_kit_detail_min, 1.0f}) {
                    ASSERT_TRUE(values.set_float(editor::c_kit_detail_parameter, detail));
                    EXPECT_TRUE(editor::kit_meshes_equal(*mesh, *generate(values))) << values.to_canonical_text();
                }
                continue;
            }
            unsigned int previous = 0;
            for (int step = 1; step <= 10; ++step) {
                ASSERT_TRUE(values.set_float(editor::c_kit_detail_parameter, 0.1f * static_cast<float>(step)));
                SCOPED_TRACE(values.to_canonical_text());
                const Kit_mesh_report report = editor::check_kit_shape(values);
                EXPECT_TRUE(report.is_valid()) << describe(report);
                EXPECT_LE(report.triangle_count, get_library().get_triangle_ceiling(values));
                EXPECT_GE(report.triangle_count, previous);
                fewer = fewer || ((step == 1) && (report.triangle_count < own));
                more  = more  || ((step == 10) && (report.triangle_count > own));
                previous = report.triangle_count;
            }
        }
        EXPECT_TRUE((shape.detail_response == editor::Kit_detail_response::fixed) || (fewer && more)) << "fewer " << fewer << ", more " << more;
    }
}

} // anonymous namespace
