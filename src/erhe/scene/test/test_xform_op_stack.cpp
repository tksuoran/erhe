// Authored USD xformOp stacks on a prim (doc/usd-compatibility-plan.md M8):
// op matrices, the composition order of a stack, and the write-back that
// lands an erhe TRS edit in the op the stack designates.

#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene/xform_op.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <memory>

using erhe::scene::Xform_op;
using erhe::scene::Xform_op_precision;
using erhe::scene::Xform_op_stack;
using erhe::scene::Xform_op_type;
using erhe::scene::Xform_op_write_back_result;

namespace {

auto approx(const glm::dmat4& a, const glm::dmat4& b, const double eps = 1e-9) -> bool
{
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[column][row] - b[column][row]) > eps) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto rotation_matrix(const int axis, const double degrees) -> glm::dmat4
{
    glm::dvec3 axis_vector{0.0, 0.0, 0.0};
    axis_vector[axis] = 1.0;
    return glm::rotate(glm::dmat4{1.0}, glm::radians(degrees), axis_vector);
}

[[nodiscard]] auto translate_op(const glm::dvec3 value) -> Xform_op
{
    return Xform_op{.type = Xform_op_type::translate, .value = value};
}

[[nodiscard]] auto scale_op(const glm::dvec3 value) -> Xform_op
{
    return Xform_op{.type = Xform_op_type::scale, .value = value};
}

[[nodiscard]] auto orient_op(const glm::dquat value) -> Xform_op
{
    return Xform_op{.type = Xform_op_type::orient, .value = value};
}

} // anonymous namespace

TEST(Xform_op, translate_and_scale_matrices)
{
    const Xform_op t = translate_op(glm::dvec3{1.0, 2.0, 3.0});
    EXPECT_TRUE(approx(t.to_matrix(), glm::translate(glm::dmat4{1.0}, glm::dvec3{1.0, 2.0, 3.0})));

    const Xform_op s = scale_op(glm::dvec3{2.0, 3.0, 4.0});
    EXPECT_TRUE(approx(s.to_matrix(), glm::scale(glm::dmat4{1.0}, glm::dvec3{2.0, 3.0, 4.0})));
}

TEST(Xform_op, inverted_op_is_the_inverse)
{
    Xform_op t = translate_op(glm::dvec3{1.0, 2.0, 3.0});
    t.inverted = true;
    EXPECT_TRUE(approx(t.to_matrix(), glm::translate(glm::dmat4{1.0}, glm::dvec3{-1.0, -2.0, -3.0})));
}

TEST(Xform_op, single_axis_rotate_matrix)
{
    const Xform_op r = Xform_op{.type = Xform_op_type::rotate_y, .value = 90.0};
    // Rotating (0, 0, 1) by 90 degrees about y gives (1, 0, 0).
    const glm::dvec4 rotated = r.to_matrix() * glm::dvec4{0.0, 0.0, 1.0, 1.0};
    EXPECT_NEAR(rotated.x, 1.0, 1e-12);
    EXPECT_NEAR(rotated.y, 0.0, 1e-12);
    EXPECT_NEAR(rotated.z, 0.0, 1e-12);
}

TEST(Xform_op, orient_matrix)
{
    const glm::dquat q = glm::angleAxis(glm::radians(90.0), glm::dvec3{0.0, 1.0, 0.0});
    const Xform_op   o = orient_op(q);
    EXPECT_TRUE(approx(o.to_matrix(), glm::dmat4{glm::mat4_cast(q)}));
}

TEST(Xform_op, rotate_xyz_matrix_is_hand_computed)
{
    // rotateXYZ (90, 90, 0): rotate about x first, then about y. Column-vector
    // form is therefore Ry(90) * Rx(90). Applying that to the basis vectors by
    // hand: x -> (0, 0, -1), y -> (1, 0, 0), z -> (0, -1, 0).
    const Xform_op   r = Xform_op{.type = Xform_op_type::rotate_xyz, .value = glm::dvec3{90.0, 90.0, 0.0}};
    const glm::dmat4 expected{
        glm::dvec4{ 0.0,  0.0, -1.0, 0.0},
        glm::dvec4{ 1.0,  0.0,  0.0, 0.0},
        glm::dvec4{ 0.0, -1.0,  0.0, 0.0},
        glm::dvec4{ 0.0,  0.0,  0.0, 1.0}
    };
    EXPECT_TRUE(approx(r.to_matrix(), expected, 1e-12));
}

TEST(Xform_op, every_rotate_order_applies_its_axes_in_order)
{
    // The value components are always the x, y and z angles; the type names
    // the order they are applied to a point in, so the column-vector matrix
    // product runs the other way around.
    const glm::dvec3 angles{10.0, 20.0, 30.0};
    const glm::dmat4 rx = rotation_matrix(0, angles.x);
    const glm::dmat4 ry = rotation_matrix(1, angles.y);
    const glm::dmat4 rz = rotation_matrix(2, angles.z);

    struct Case { Xform_op_type type; glm::dmat4 expected; };
    const Case cases[] = {
        {Xform_op_type::rotate_xyz, rz * ry * rx},
        {Xform_op_type::rotate_xzy, ry * rz * rx},
        {Xform_op_type::rotate_yxz, rz * rx * ry},
        {Xform_op_type::rotate_yzx, rx * rz * ry},
        {Xform_op_type::rotate_zxy, ry * rx * rz},
        {Xform_op_type::rotate_zyx, rx * ry * rz}
    };
    for (const Case& c : cases) {
        const Xform_op op = Xform_op{.type = c.type, .value = angles};
        EXPECT_TRUE(approx(op.to_matrix(), c.expected, 1e-12));
    }
}

TEST(Xform_op, euler_extraction_round_trips_in_every_order)
{
    const Xform_op_type types[] = {
        Xform_op_type::rotate_xyz,
        Xform_op_type::rotate_xzy,
        Xform_op_type::rotate_yxz,
        Xform_op_type::rotate_yzx,
        Xform_op_type::rotate_zxy,
        Xform_op_type::rotate_zyx
    };
    const glm::dquat rotations[] = {
        glm::angleAxis(glm::radians(37.0), glm::normalize(glm::dvec3{1.0, 2.0, 3.0})),
        glm::angleAxis(glm::radians(155.0), glm::normalize(glm::dvec3{-2.0, 1.0, 0.5})),
        glm::angleAxis(glm::radians(-80.0), glm::normalize(glm::dvec3{0.0, 1.0, 0.0}))
    };
    for (const Xform_op_type type : types) {
        for (const glm::dquat& rotation : rotations) {
            const glm::dvec3 angles = erhe::scene::euler_degrees_from_rotation(type, rotation);
            EXPECT_TRUE(approx(erhe::scene::euler_matrix(type, angles), glm::dmat4{glm::mat4_cast(rotation)}, 1e-9));
        }
    }
}

TEST(Xform_op_stack, composes_in_order_as_t_r_s)
{
    const glm::dvec3 translation{1.0, 2.0, 3.0};
    const glm::dquat rotation = glm::angleAxis(glm::radians(30.0), glm::dvec3{0.0, 1.0, 0.0});
    const glm::dvec3 scale{2.0, 2.0, 2.0};

    Xform_op_stack stack;
    stack.ops.push_back(translate_op(translation));
    stack.ops.push_back(orient_op(rotation));
    stack.ops.push_back(scale_op(scale));

    const glm::dmat4 expected =
        glm::translate(glm::dmat4{1.0}, translation) *
        glm::dmat4{glm::mat4_cast(rotation)} *
        glm::scale(glm::dmat4{1.0}, scale);
    EXPECT_TRUE(approx(stack.compose(), expected, 1e-12));
}

TEST(Xform_op_stack, set_xform_op_stack_sets_the_local_transform)
{
    auto node = std::make_shared<erhe::scene::Xform>("n");

    Xform_op_stack stack;
    stack.ops.push_back(translate_op(glm::dvec3{1.0, 2.0, 3.0}));
    stack.ops.push_back(scale_op(glm::dvec3{2.0, 2.0, 2.0}));
    node->set_xform_op_stack(stack);

    ASSERT_TRUE(node->has_xform_op_stack());
    EXPECT_TRUE(approx(glm::dmat4{node->parent_from_node()}, stack.compose(), 1e-6));
    EXPECT_EQ(node->parent_from_node_transform().get_translation(), (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(node->parent_from_node_transform().get_scale(), glm::vec3{2.0f});
}

TEST(Xform_op_stack, write_back_lands_in_the_designated_ops)
{
    Xform_op_stack stack;
    stack.ops.push_back(translate_op(glm::dvec3{1.0, 0.0, 0.0}));
    stack.ops.push_back(orient_op(glm::dquat{1.0, 0.0, 0.0, 0.0}));
    stack.ops.push_back(scale_op(glm::dvec3{1.0, 1.0, 1.0}));

    const glm::vec3 translation{4.0f, 5.0f, 6.0f};
    const glm::quat rotation = glm::angleAxis(glm::radians(45.0f), glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f}));
    const glm::vec3 scale{2.0f, 3.0f, 4.0f};
    EXPECT_EQ(
        erhe::scene::write_trs_into_xform_op_stack(stack, translation, rotation, scale),
        Xform_op_write_back_result::written
    );

    EXPECT_EQ(std::get<glm::dvec3>(stack.ops.at(0).value), glm::dvec3{translation});
    EXPECT_EQ(std::get<glm::dvec3>(stack.ops.at(2).value), glm::dvec3{scale});
    const glm::dmat4 expected =
        glm::translate(glm::dmat4{1.0}, glm::dvec3{translation}) *
        glm::dmat4{glm::mat4_cast(glm::dquat{rotation})} *
        glm::scale(glm::dmat4{1.0}, glm::dvec3{scale});
    EXPECT_TRUE(approx(stack.compose(), expected, 1e-6));
}

TEST(Xform_op_stack, suffixed_and_inverted_ops_are_never_written)
{
    // A pivot pair around the ops the write-back designates.
    Xform_op_stack stack;
    stack.ops.push_back(Xform_op{.type = Xform_op_type::translate, .suffix = "pivot", .value = glm::dvec3{5.0, 0.0, 0.0}});
    stack.ops.push_back(translate_op(glm::dvec3{1.0, 0.0, 0.0}));
    stack.ops.push_back(Xform_op{.type = Xform_op_type::translate, .suffix = "pivot", .inverted = true, .value = glm::dvec3{5.0, 0.0, 0.0}});
    const Xform_op_stack authored = stack;

    EXPECT_EQ(
        erhe::scene::write_trs_into_xform_op_stack(
            stack,
            glm::vec3{4.0f, 5.0f, 6.0f},
            glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
            glm::vec3{1.0f}
        ),
        Xform_op_write_back_result::written
    );
    // The pivot ops are byte-identical; only the plain translate took the write.
    EXPECT_EQ(stack.ops.at(0), authored.ops.at(0));
    EXPECT_EQ(stack.ops.at(2), authored.ops.at(2));
    EXPECT_EQ(std::get<glm::dvec3>(stack.ops.at(1).value), (glm::dvec3{4.0, 5.0, 6.0}));
}

namespace {

// [translate, translate:pivot, rotateXYZ, !invert!translate:pivot, scale]:
// the pivot pair carries a rotation edit away from the requested transform.
[[nodiscard]] auto make_pivot_stack() -> Xform_op_stack
{
    Xform_op_stack stack;
    stack.ops.push_back(translate_op(glm::dvec3{1.0, 0.0, 0.0}));
    stack.ops.push_back(Xform_op{.type = Xform_op_type::translate, .suffix = "pivot", .value = glm::dvec3{0.0, 2.0, 0.0}});
    stack.ops.push_back(Xform_op{.type = Xform_op_type::rotate_xyz, .value = glm::dvec3{0.0, 0.0, 0.0}});
    stack.ops.push_back(Xform_op{.type = Xform_op_type::translate, .suffix = "pivot", .inverted = true, .value = glm::dvec3{0.0, 2.0, 0.0}});
    stack.ops.push_back(scale_op(glm::dvec3{1.0, 1.0, 1.0}));
    return stack;
}

} // anonymous namespace

TEST(Xform_op_stack, a_pivot_stack_cannot_carry_a_rotation_edit)
{
    Xform_op_stack       stack   = make_pivot_stack();
    const Xform_op_stack authored = stack;
    const glm::quat      rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    EXPECT_EQ(
        erhe::scene::write_trs_into_xform_op_stack(stack, glm::vec3{1.0f, 0.0f, 0.0f}, rotation, glm::vec3{1.0f}),
        Xform_op_write_back_result::not_representable
    );
    // Nothing was left written.
    EXPECT_EQ(stack, authored);
}

TEST(Xform_op_stack, a_pivot_stack_does_carry_a_translation_edit)
{
    Xform_op_stack stack = make_pivot_stack();
    const glm::vec3 translation{4.0f, 5.0f, 6.0f};
    EXPECT_EQ(
        erhe::scene::write_trs_into_xform_op_stack(stack, translation, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f}),
        Xform_op_write_back_result::written
    );
    EXPECT_EQ(stack.ops.size(), 5);
    EXPECT_EQ(std::get<glm::dvec3>(stack.ops.at(0).value), glm::dvec3{translation});
    EXPECT_TRUE(approx(stack.compose(), glm::translate(glm::dmat4{1.0}, glm::dvec3{translation}), 1e-6));
}

TEST(Xform_op_stack, a_rotation_edit_on_a_pivot_stack_lands_the_prim_exactly)
{
    auto node = std::make_shared<erhe::scene::Xform>("n");
    node->set_xform_op_stack(make_pivot_stack());

    const glm::mat4 requested =
        glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f}) *
        glm::mat4_cast(glm::angleAxis(glm::radians(30.0f), glm::vec3{0.0f, 0.0f, 1.0f}));
    node->set_parent_from_node(requested);

    ASSERT_TRUE(node->has_xform_op_stack());
    const Xform_op_stack& collapsed = *node->get_xform_op_stack();
    ASSERT_EQ(collapsed.ops.size(), 1);
    EXPECT_EQ(collapsed.ops.front().type, Xform_op_type::transform);
    EXPECT_TRUE(approx(glm::dmat4{node->parent_from_node()}, glm::dmat4{requested}, 1e-6));
    EXPECT_TRUE(approx(collapsed.compose(), glm::dmat4{requested}, 1e-6));
}

TEST(Xform_op_stack, write_back_into_a_rotate_xyz_op_gives_euler_degrees)
{
    Xform_op_stack stack;
    stack.ops.push_back(translate_op(glm::dvec3{0.0, 0.0, 0.0}));
    stack.ops.push_back(Xform_op{.type = Xform_op_type::rotate_xyz, .value = glm::dvec3{0.0, 0.0, 0.0}});

    const glm::quat rotation = glm::angleAxis(glm::radians(63.0f), glm::normalize(glm::vec3{1.0f, 2.0f, -1.0f}));
    EXPECT_EQ(
        erhe::scene::write_trs_into_xform_op_stack(stack, glm::vec3{0.0f}, rotation, glm::vec3{1.0f}),
        Xform_op_write_back_result::written
    );
    const glm::dvec3 angles = std::get<glm::dvec3>(stack.ops.at(1).value);
    EXPECT_TRUE(
        approx(
            erhe::scene::euler_matrix(Xform_op_type::rotate_xyz, angles),
            glm::dmat4{glm::mat4_cast(glm::dquat{rotation})},
            1e-6
        )
    );
    EXPECT_TRUE(approx(stack.compose(), glm::dmat4{glm::mat4_cast(glm::dquat{rotation})}, 1e-6));
}

TEST(Xform_op_stack, write_back_reports_an_edit_the_stack_cannot_carry)
{
    // No scale op: a scale change has nowhere to go.
    Xform_op_stack no_scale;
    no_scale.ops.push_back(translate_op(glm::dvec3{0.0, 0.0, 0.0}));
    EXPECT_EQ(
        erhe::scene::write_trs_into_xform_op_stack(no_scale, glm::vec3{0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{2.0f}),
        Xform_op_write_back_result::not_representable
    );

    // A rotate_x op and a rotation that is not about x.
    Xform_op_stack rotate_x_only;
    rotate_x_only.ops.push_back(Xform_op{.type = Xform_op_type::rotate_x, .value = 0.0});
    EXPECT_EQ(
        erhe::scene::write_trs_into_xform_op_stack(
            rotate_x_only,
            glm::vec3{0.0f},
            glm::angleAxis(glm::radians(30.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
            glm::vec3{1.0f}
        ),
        Xform_op_write_back_result::not_representable
    );

    // The same op does carry a rotation about x.
    EXPECT_EQ(
        erhe::scene::write_trs_into_xform_op_stack(
            rotate_x_only,
            glm::vec3{0.0f},
            glm::angleAxis(glm::radians(30.0f), glm::vec3{1.0f, 0.0f, 0.0f}),
            glm::vec3{1.0f}
        ),
        Xform_op_write_back_result::written
    );
    EXPECT_NEAR(std::get<double>(rotate_x_only.ops.front().value), 30.0, 1e-4);
}

TEST(Xform_op_stack, a_single_transform_op_takes_the_whole_matrix)
{
    Xform_op_stack stack;
    stack.ops.push_back(Xform_op{.type = Xform_op_type::transform, .precision = Xform_op_precision::double_});

    const glm::vec3 translation{4.0f, 5.0f, 6.0f};
    const glm::quat rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::vec3 scale{2.0f, 2.0f, 2.0f};
    EXPECT_EQ(
        erhe::scene::write_trs_into_xform_op_stack(stack, translation, rotation, scale),
        Xform_op_write_back_result::written
    );
    const glm::dmat4 expected =
        glm::translate(glm::dmat4{1.0}, glm::dvec3{translation}) *
        glm::dmat4{glm::mat4_cast(glm::dquat{rotation})} *
        glm::scale(glm::dmat4{1.0}, glm::dvec3{scale});
    EXPECT_TRUE(approx(stack.compose(), expected, 1e-6));
}

TEST(Xform_op_stack, a_node_edit_lands_in_the_designated_op)
{
    auto node = std::make_shared<erhe::scene::Xform>("n");
    Xform_op_stack stack;
    stack.ops.push_back(translate_op(glm::dvec3{1.0, 0.0, 0.0}));
    stack.ops.push_back(Xform_op{.type = Xform_op_type::rotate_xyz, .value = glm::dvec3{0.0, 0.0, 0.0}});
    stack.ops.push_back(scale_op(glm::dvec3{1.0, 1.0, 1.0}));
    node->set_xform_op_stack(stack);

    node->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{7.0f, 8.0f, 9.0f}));

    ASSERT_TRUE(node->has_xform_op_stack());
    const Xform_op_stack& written = *node->get_xform_op_stack();
    ASSERT_EQ(written.ops.size(), 3);
    EXPECT_EQ(std::get<glm::dvec3>(written.ops.at(0).value), (glm::dvec3{7.0, 8.0, 9.0}));
    // The other ops kept their values.
    EXPECT_EQ(written.ops.at(1), stack.ops.at(1));
    EXPECT_EQ(written.ops.at(2), stack.ops.at(2));
    EXPECT_EQ(node->parent_from_node_transform().get_translation(), (glm::vec3{7.0f, 8.0f, 9.0f}));
}

TEST(Xform_op_stack, an_unrepresentable_node_edit_collapses_the_stack)
{
    auto node = std::make_shared<erhe::scene::Xform>("n");
    Xform_op_stack stack;
    stack.ops.push_back(translate_op(glm::dvec3{1.0, 0.0, 0.0}));
    node->set_xform_op_stack(stack);

    const glm::mat4 scaled = glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f, 2.0f, 2.0f});
    node->set_parent_from_node(scaled);

    ASSERT_TRUE(node->has_xform_op_stack());
    const Xform_op_stack& collapsed = *node->get_xform_op_stack();
    ASSERT_EQ(collapsed.ops.size(), 1);
    EXPECT_EQ(collapsed.ops.front().type, Xform_op_type::transform);
    EXPECT_TRUE(approx(collapsed.compose(), glm::dmat4{scaled}, 1e-6));
}

TEST(Xform_op_stack, a_clone_copies_the_stack)
{
    auto node = std::make_shared<erhe::scene::Xform>("n");
    Xform_op_stack stack;
    stack.reset_xform_stack = true;
    stack.ops.push_back(translate_op(glm::dvec3{1.0, 2.0, 3.0}));
    node->set_xform_op_stack(stack);

    std::shared_ptr<erhe::Item_base>    clone_item = node->clone();
    std::shared_ptr<erhe::scene::Xform> clone      = std::dynamic_pointer_cast<erhe::scene::Xform>(clone_item);
    ASSERT_TRUE(clone.operator bool());
    ASSERT_TRUE(clone->has_xform_op_stack());
    EXPECT_EQ(*clone->get_xform_op_stack(), stack);
}

TEST(Xform_op_stack, restore_does_not_run_the_write_back)
{
    auto node = std::make_shared<erhe::scene::Xform>("n");
    Xform_op_stack stack;
    stack.ops.push_back(translate_op(glm::dvec3{1.0, 0.0, 0.0}));
    node->set_xform_op_stack(stack);

    const erhe::scene::Transform before{node->parent_from_node()};
    const std::optional<Xform_op_stack> stack_before = node->copy_xform_op_stack();

    // An edit the stack cannot carry collapses it ...
    node->set_parent_from_node(glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f}));
    EXPECT_EQ(node->get_xform_op_stack()->ops.front().type, Xform_op_type::transform);

    // ... and the restore brings the authored stack back verbatim.
    node->restore_local_transform(before, stack_before);
    ASSERT_TRUE(node->has_xform_op_stack());
    EXPECT_EQ(*node->get_xform_op_stack(), stack);
    EXPECT_EQ(node->parent_from_node_transform().get_translation(), (glm::vec3{1.0f, 0.0f, 0.0f}));

    // Restoring "no stack" clears it.
    node->restore_local_transform(before, std::optional<Xform_op_stack>{});
    EXPECT_FALSE(node->has_xform_op_stack());
}
