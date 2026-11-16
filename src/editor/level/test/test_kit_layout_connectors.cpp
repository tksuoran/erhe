#include "level/kit_connector.hpp"
#include "level/kit_layout_connectors.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace {

using editor::Kit_layout_child;
using editor::Kit_layout_child_role;
using editor::Kit_layout_connector_hiding;
using editor::Kit_layout_connector_visibility;
using editor::Kit_layout_direction;
using editor::Kit_layout_hide_reason;
using erhe::math::Curve_closure;

constexpr std::array<Kit_layout_direction, 6> c_directions{
    Kit_layout_direction::plus_tangent,  Kit_layout_direction::minus_tangent,
    Kit_layout_direction::plus_normal,   Kit_layout_direction::minus_normal,
    Kit_layout_direction::plus_binormal, Kit_layout_direction::minus_binormal
};

// The unit outward of each direction in the element frame (tangent x, binormal y, normal z).
auto direction_vector(const Kit_layout_direction direction) -> glm::vec3
{
    switch (direction) {
        case Kit_layout_direction::plus_tangent:   return glm::vec3{ 1.0f,  0.0f,  0.0f};
        case Kit_layout_direction::minus_tangent:  return glm::vec3{-1.0f,  0.0f,  0.0f};
        case Kit_layout_direction::plus_normal:    return glm::vec3{ 0.0f,  0.0f,  1.0f};
        case Kit_layout_direction::minus_normal:   return glm::vec3{ 0.0f,  0.0f, -1.0f};
        case Kit_layout_direction::plus_binormal:  return glm::vec3{ 0.0f,  1.0f,  0.0f};
        case Kit_layout_direction::minus_binormal:
        default:                                   return glm::vec3{ 0.0f, -1.0f,  0.0f};
    }
}

auto hiding_of(const std::optional<Kit_layout_direction> direction, const Kit_layout_connector_visibility all) -> Kit_layout_connector_hiding
{
    Kit_layout_connector_hiding hiding;
    hiding.all = all;
    if (direction.has_value()) {
        hiding.set(direction.value(), Kit_layout_connector_visibility::hidden);
    }
    return hiding;
}

auto is_hidden(const Kit_layout_connector_hiding& hiding, const Kit_layout_child_role role, const Kit_layout_direction direction) -> bool
{
    return editor::get_layout_hide_reason(hiding, role, direction) != Kit_layout_hide_reason::shown;
}

TEST(KitLayoutConnectors, ClassifiesEachAxisDirection)
{
    for (const Kit_layout_direction direction : c_directions) {
        EXPECT_EQ(editor::classify_layout_direction(direction_vector(direction)), direction) << editor::c_str(direction);
    }
    EXPECT_EQ(editor::classify_layout_direction(glm::vec3{0.2f, -0.9f, 0.3f}), Kit_layout_direction::minus_binormal);
    EXPECT_EQ(editor::classify_layout_direction(glm::vec3{0.6f, 0.1f, -0.7f}), Kit_layout_direction::minus_normal);
}

TEST(KitLayoutConnectors, TiesGoToTangentThenNormalThenBinormal)
{
    const float d = glm::one_over_root_two<float>();
    EXPECT_EQ(editor::classify_layout_direction(glm::vec3{ d, 0.0f,  d}),  Kit_layout_direction::plus_tangent);
    EXPECT_EQ(editor::classify_layout_direction(glm::vec3{-d, d,    0.0f}), Kit_layout_direction::minus_tangent);
    EXPECT_EQ(editor::classify_layout_direction(glm::vec3{0.0f, d,  -d}),  Kit_layout_direction::minus_normal);
    const float t = 1.0f / std::sqrt(3.0f);
    EXPECT_EQ(editor::classify_layout_direction(glm::vec3{-t, t, t}), Kit_layout_direction::minus_tangent);
}

TEST(KitLayoutConnectors, ChildRoles)
{
    EXPECT_EQ(editor::get_layout_child_role(0, 1, Curve_closure::open),   Kit_layout_child_role::only);
    EXPECT_EQ(editor::get_layout_child_role(0, 1, Curve_closure::closed), Kit_layout_child_role::only);
    EXPECT_EQ(editor::get_layout_child_role(0, 2, Curve_closure::open),   Kit_layout_child_role::head);
    EXPECT_EQ(editor::get_layout_child_role(1, 2, Curve_closure::open),   Kit_layout_child_role::tail);
    EXPECT_EQ(editor::get_layout_child_role(0, 5, Curve_closure::open),   Kit_layout_child_role::head);
    EXPECT_EQ(editor::get_layout_child_role(2, 5, Curve_closure::open),   Kit_layout_child_role::inner);
    EXPECT_EQ(editor::get_layout_child_role(4, 5, Curve_closure::open),   Kit_layout_child_role::tail);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(editor::get_layout_child_role(i, 4, Curve_closure::closed), Kit_layout_child_role::inner) << i;
    }
}

TEST(KitLayoutConnectors, DefaultsHideNothing)
{
    const Kit_layout_connector_hiding hiding;
    EXPECT_FALSE(hiding.hides_any());
    for (const Kit_layout_child_role role : {Kit_layout_child_role::inner, Kit_layout_child_role::head, Kit_layout_child_role::tail, Kit_layout_child_role::only}) {
        for (const Kit_layout_direction direction : c_directions) {
            EXPECT_FALSE(is_hidden(hiding, role, direction));
        }
    }
}

TEST(KitLayoutConnectors, EachSwitchAloneHidesItsDirectionOnInnerChildren)
{
    for (const Kit_layout_direction hidden : c_directions) {
        const Kit_layout_connector_hiding hiding = hiding_of(hidden, Kit_layout_connector_visibility::shown);
        EXPECT_TRUE(hiding.hides_any());
        for (const Kit_layout_direction direction : c_directions) {
            EXPECT_EQ(is_hidden(hiding, Kit_layout_child_role::inner, direction), direction == hidden) << editor::c_str(hidden) << " " << editor::c_str(direction);
            EXPECT_EQ(editor::get_layout_hide_reason(hiding, Kit_layout_child_role::inner, direction),
                      (direction == hidden) ? Kit_layout_hide_reason::direction : Kit_layout_hide_reason::shown);
        }
    }
}

TEST(KitLayoutConnectors, CombinationsAreIndependent)
{
    Kit_layout_connector_hiding hiding;
    hiding.set(Kit_layout_direction::plus_tangent,  Kit_layout_connector_visibility::hidden);
    hiding.set(Kit_layout_direction::minus_tangent, Kit_layout_connector_visibility::hidden);
    hiding.set(Kit_layout_direction::minus_normal,  Kit_layout_connector_visibility::hidden);
    EXPECT_TRUE (is_hidden(hiding, Kit_layout_child_role::inner, Kit_layout_direction::plus_tangent));
    EXPECT_TRUE (is_hidden(hiding, Kit_layout_child_role::inner, Kit_layout_direction::minus_tangent));
    EXPECT_FALSE(is_hidden(hiding, Kit_layout_child_role::inner, Kit_layout_direction::plus_normal));
    EXPECT_TRUE (is_hidden(hiding, Kit_layout_child_role::inner, Kit_layout_direction::minus_normal));
    EXPECT_FALSE(is_hidden(hiding, Kit_layout_child_role::inner, Kit_layout_direction::plus_binormal));
    EXPECT_FALSE(is_hidden(hiding, Kit_layout_child_role::inner, Kit_layout_direction::minus_binormal));
}

TEST(KitLayoutConnectors, HideAllHidesEveryDirection)
{
    const Kit_layout_connector_hiding hiding = hiding_of(std::nullopt, Kit_layout_connector_visibility::hidden);
    for (const Kit_layout_direction direction : c_directions) {
        EXPECT_EQ(editor::get_layout_hide_reason(hiding, Kit_layout_child_role::inner, direction), Kit_layout_hide_reason::hide_all);
    }
}

TEST(KitLayoutConnectors, HeadAndTailHideOnlyTheNeighbourFacingDirection)
{
    for (const Kit_layout_connector_visibility all : {Kit_layout_connector_visibility::shown, Kit_layout_connector_visibility::hidden}) {
        for (const Kit_layout_direction hidden : c_directions) {
            const Kit_layout_connector_hiding hiding = hiding_of(hidden, all);
            for (const Kit_layout_direction direction : c_directions) {
                const bool switch_on = (all == Kit_layout_connector_visibility::hidden) || (direction == hidden);
                EXPECT_EQ(is_hidden(hiding, Kit_layout_child_role::head, direction), switch_on && (direction == Kit_layout_direction::plus_tangent));
                EXPECT_EQ(is_hidden(hiding, Kit_layout_child_role::tail, direction), switch_on && (direction == Kit_layout_direction::minus_tangent));
                EXPECT_FALSE(is_hidden(hiding, Kit_layout_child_role::only, direction));
            }
        }
    }
}

TEST(KitLayoutConnectors, TwoChildrenFaceEachOther)
{
    const Kit_layout_connector_hiding hiding = hiding_of(std::nullopt, Kit_layout_connector_visibility::hidden);
    const Kit_layout_child_role first  = editor::get_layout_child_role(0, 2, Curve_closure::open);
    const Kit_layout_child_role second = editor::get_layout_child_role(1, 2, Curve_closure::open);
    EXPECT_TRUE (is_hidden(hiding, first,  Kit_layout_direction::plus_tangent));
    EXPECT_FALSE(is_hidden(hiding, first,  Kit_layout_direction::minus_tangent));
    EXPECT_TRUE (is_hidden(hiding, second, Kit_layout_direction::minus_tangent));
    EXPECT_FALSE(is_hidden(hiding, second, Kit_layout_direction::plus_tangent));
    // Closed, both are inner.
    EXPECT_TRUE(is_hidden(hiding, editor::get_layout_child_role(0, 2, Curve_closure::closed), Kit_layout_direction::minus_tangent));
}

TEST(KitLayoutConnectors, UnitScaleKeepsTheLocalOutward)
{
    // The child's local rotation is its element frame, so a node-local axis faces its own direction.
    for (const Kit_layout_direction direction : c_directions) {
        const std::optional<glm::vec3> outward = editor::get_outward_in_element(direction_vector(direction), glm::vec3{1.0f});
        ASSERT_TRUE(outward.has_value());
        EXPECT_EQ(editor::classify_layout_direction(outward.value()), direction) << editor::c_str(direction);
    }
}

TEST(KitLayoutConnectors, ScaledChildKeepsAxisDirectionsAndReweighsDiagonals)
{
    // A segments piece stretched 4x along x: a face normal leans away from the stretched axis.
    const glm::vec3 stretch{4.0f, 1.0f, 1.0f};
    for (const Kit_layout_direction direction : c_directions) {
        const std::optional<glm::vec3> outward = editor::get_outward_in_element(direction_vector(direction), stretch);
        ASSERT_TRUE(outward.has_value());
        EXPECT_EQ(editor::classify_layout_direction(outward.value()), direction) << editor::c_str(direction);
    }
    const glm::vec3 diagonal = glm::normalize(glm::vec3{0.8f, 0.0f, 0.6f});
    EXPECT_EQ(editor::classify_layout_direction(diagonal), Kit_layout_direction::plus_tangent);
    const std::optional<glm::vec3> stretched = editor::get_outward_in_element(diagonal, stretch);
    ASSERT_TRUE(stretched.has_value());
    EXPECT_EQ(editor::classify_layout_direction(stretched.value()), Kit_layout_direction::plus_normal);
}

TEST(KitLayoutConnectors, MirroredChildFlipsTheMirroredAxis)
{
    const glm::vec3 mirror_x{-1.0f, 1.0f, 1.0f};
    const std::optional<glm::vec3> plus_x = editor::get_outward_in_element(glm::vec3{1.0f, 0.0f, 0.0f}, mirror_x);
    ASSERT_TRUE(plus_x.has_value());
    EXPECT_EQ(editor::classify_layout_direction(plus_x.value()), Kit_layout_direction::minus_tangent);
    const std::optional<glm::vec3> plus_z = editor::get_outward_in_element(glm::vec3{0.0f, 0.0f, 1.0f}, mirror_x);
    ASSERT_TRUE(plus_z.has_value());
    EXPECT_EQ(editor::classify_layout_direction(plus_z.value()), Kit_layout_direction::plus_normal);
}

TEST(KitLayoutConnectors, DegenerateChildHidesNothing)
{
    Kit_layout_child child;
    child.hiding.all       = Kit_layout_connector_visibility::hidden;
    child.scale            = glm::vec3{1.0f, 0.0f, 1.0f};
    const editor::Kit_layout_connector_verdict verdict = editor::get_layout_connector_verdict(child, glm::vec3{1.0f, 0.0f, 0.0f});
    EXPECT_FALSE(verdict.direction.has_value());
    EXPECT_EQ(verdict.reason, Kit_layout_hide_reason::shown);
}

TEST(KitLayoutConnectors, VerdictCombinesFrameRoleAndSwitches)
{
    Kit_layout_child child;
    child.role = Kit_layout_child_role::head;
    child.hiding.set(Kit_layout_direction::plus_tangent, Kit_layout_connector_visibility::hidden);
    child.hiding.set(Kit_layout_direction::plus_normal,  Kit_layout_connector_visibility::hidden);
    const editor::Kit_layout_connector_verdict end   = editor::get_layout_connector_verdict(child, glm::vec3{1.0f, 0.0f, 0.0f});
    const editor::Kit_layout_connector_verdict front = editor::get_layout_connector_verdict(child, glm::vec3{0.0f, 0.0f, 1.0f});
    EXPECT_EQ(end.direction,   std::optional<Kit_layout_direction>{Kit_layout_direction::plus_tangent});
    EXPECT_EQ(end.reason,      Kit_layout_hide_reason::direction);
    EXPECT_EQ(front.direction, std::optional<Kit_layout_direction>{Kit_layout_direction::plus_normal});
    EXPECT_EQ(front.reason,    Kit_layout_hide_reason::shown); // a head keeps its other five directions
}

enum class Range_end : unsigned int { min = 0, max };

// Every parameter at the minimum or the maximum of its range (test_kit_shapes.cpp range_corner()'s approach).
auto range_corner(const editor::Kit_shape_definition& shape, const Range_end range_end) -> editor::Kit_parameter_values
{
    const bool at_max = (range_end == Range_end::max);
    editor::Kit_parameter_values values = editor::make_default_values(shape, 1.0f, 3.0f);
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        const editor::Kit_parameter& parameter = shape.parameters[i];
        if (parameter.name == "seed") {
            continue;
        }
        values.set_value_source(i, editor::Kit_value_source::explicit_value);
        editor::Kit_parameter_value value = values.get_values()[i];
        if (parameter.type == editor::Kit_parameter_type::float_value) {
            value.float_value = at_max ? parameter.float_max : parameter.float_min;
        } else {
            value.int_value = at_max ? parameter.int_max : parameter.int_min;
        }
        values.set_value(i, value);
    }
    return values;
}

// One parameter at one end of its range, the others at their defaults.
auto range_end_of(const editor::Kit_shape_definition& shape, const std::size_t index, const Range_end range_end) -> editor::Kit_parameter_values
{
    const bool at_max = (range_end == Range_end::max);
    editor::Kit_parameter_values values = editor::make_default_values(shape, 1.0f, 3.0f);
    const editor::Kit_parameter& parameter = shape.parameters[index];
    editor::Kit_parameter_value  value     = values.get_values()[index];
    if (parameter.type == editor::Kit_parameter_type::float_value) {
        value.float_value = at_max ? parameter.float_max : parameter.float_min;
    } else {
        value.int_value = at_max ? parameter.int_max : parameter.int_min;
    }
    values.set_value(index, value);
    return values;
}

TEST(KitLayoutConnectors, EveryShapeKeepsWithinTheConnectorBound)
{
    static const editor::Kit_shape_library library;
    std::vector<editor::Kit_connector> connectors;
    for (const editor::Kit_shape_definition& shape : library.get_shapes()) {
        std::vector<editor::Kit_parameter_values> cases;
        cases.push_back(editor::make_default_values(shape, 1.0f, 3.0f));
        cases.push_back(range_corner(shape, Range_end::min));
        cases.push_back(range_corner(shape, Range_end::max));
        for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
            cases.push_back(range_end_of(shape, i, Range_end::min));
            cases.push_back(range_end_of(shape, i, Range_end::max));
        }
        for (const editor::Kit_parameter_values& values : cases) {
            editor::get_shape_connectors(values, connectors);
            EXPECT_LE(connectors.size(), editor::c_max_shape_connectors) << shape.id << " " << values.to_canonical_text();
        }
    }
}

TEST(KitLayoutConnectors, SwitchesReadTheLayoutHiddenBits)
{
    editor::Kit_connector_switches switches;
    switches.hide_by_layout(2);
    EXPECT_FALSE(switches.is_enabled("left", 2));
    EXPECT_TRUE (switches.is_hidden_by_layout(2));
    EXPECT_FALSE(switches.is_disabled("left"));
    EXPECT_TRUE (switches.is_enabled("right", 3));
    switches.hide_by_layout(editor::c_max_shape_connectors); // past the bound: stays shown
    EXPECT_TRUE (switches.is_enabled("far", editor::c_max_shape_connectors));
    switches.layout_hidden_bits = 0;
    EXPECT_TRUE (switches.is_enabled("left", 2));
}

} // anonymous namespace
