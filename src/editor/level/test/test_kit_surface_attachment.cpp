#include "level/kit_surface_attachment.hpp"
#include "level/kit_layout.hpp"
#include "level/kit_piece.hpp"

#include "erhe_scene/mesh.hpp"
#include "erhe_scene/xform.hpp"

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

namespace {

using editor::Kit_surface_attachment;
using editor::Surface_orient;
using editor::Surface_pose;


[[nodiscard]] auto make_bounds(const glm::vec3 min, const glm::vec3 max) -> erhe::math::Aabb
{
    erhe::math::Aabb bounds{};
    bounds.min = min;
    bounds.max = max;
    return bounds;
}

const erhe::math::Aabb c_table = make_bounds(glm::vec3{-0.5f, 0.0f, -0.5f}, glm::vec3{0.5f, 1.0f, 0.5f});
const erhe::math::Aabb c_wall  = make_bounds(glm::vec3{-2.0f, 0.0f, -0.125f}, glm::vec3{2.0f, 3.0f, 0.125f});
const erhe::math::Aabb c_crate = make_bounds(glm::vec3{-0.25f, 0.0f, -0.25f}, glm::vec3{0.25f, 0.5f, 0.25f});
const glm::vec3        c_down{0.0f, -1.0f, 0.0f};

// The record of the pose, as an attach writes it.
[[nodiscard]] auto make_record(
    const erhe::math::Aabb& support,
    const erhe::math::Aabb& piece,
    const Surface_pose&     pose,
    const glm::vec3         direction,
    const Surface_orient    orient = Surface_orient::off,
    const Surface_snap_rest rest   = Surface_snap_rest::leading_face
) -> Kit_surface_attachment
{
    const std::optional<glm::dvec3> position = editor::make_surface_attachment_position(support, piece, pose, direction, orient, rest);
    EXPECT_TRUE(position.has_value());
    return Kit_surface_attachment{
        .target    = "SupportUid000001",
        .position  = position.value_or(glm::dvec3{0.0}),
        .direction = direction,
        .orient    = orient,
        .rest      = rest
    };
}

// The record through its text, as a save and a load carry it.
[[nodiscard]] auto through_text(const Kit_surface_attachment& record) -> Kit_surface_attachment
{
    Kit_surface_attachment read;
    EXPECT_EQ(editor::parse_surface_attachment_text(editor::make_surface_attachment_text(record), read), "");
    return read;
}

[[nodiscard]] auto carry(const Kit_surface_attachment& record, const erhe::math::Aabb& support, const erhe::math::Aabb& piece, const Surface_pose& pose) -> glm::vec3
{
    const std::optional<glm::vec3> translation = editor::carry_surface_attachment(record, support, piece, pose.rotation, pose.scale);
    EXPECT_TRUE(translation.has_value());
    return translation.value_or(glm::vec3{0.0f});
}

} // anonymous namespace

TEST(Kit_surface_attachment, text_round_trips_bit_exact)
{
    const Kit_surface_attachment record{
        .target    = "SupportUid000001",
        .position  = glm::dvec3{0.1, 1.0 / 3.0, -0.25},
        .direction = glm::normalize(glm::vec3{0.3f, -0.9f, 0.1f}),
        .orient    = Surface_orient::on,
        .rest      = Surface_snap_rest::anchor,
        .on_surface = false
    };
    const std::string text = editor::make_surface_attachment_text(record);
    Kit_surface_attachment read;
    ASSERT_EQ(editor::parse_surface_attachment_text(text, read), "");
    EXPECT_EQ(read, record);
    EXPECT_EQ(editor::make_surface_attachment_text(read), text);
    // Sorted keys, the floats hexadecimal.
    EXPECT_EQ(text.find("\"direction\""), 1u);
    EXPECT_NE(text.find("\"rest\":\"anchor\""), std::string::npos);
    EXPECT_NE(text.find("\"orient\":true"), std::string::npos);
    EXPECT_NE(text.find("\"on_surface\":false"), std::string::npos);
}

TEST(Kit_surface_attachment, text_refusals)
{
    Kit_surface_attachment read;
    const std::string valid = R"({"direction":["0x0p+0","-0x1p+0","0x0p+0"],"on_surface":true,"orient":false,"position":["0x1p-1","0x1p+0","0x0p+0"],"rest":"leading_face","target":"t"})";
    ASSERT_EQ(editor::parse_surface_attachment_text(valid, read), "");
    EXPECT_NE(editor::parse_surface_attachment_text("", read), "");
    // The five-key form of the record before on_surface does not read.
    EXPECT_NE(editor::parse_surface_attachment_text(R"({"direction":["0x0p+0","-0x1p+0","0x0p+0"],"orient":false,"position":["0x1p-1","0x1p+0","0x0p+0"],"rest":"leading_face","target":"t"})", read), "");
    EXPECT_NE(editor::parse_surface_attachment_text(R"({"direction":["0x0p+0","-0x1p+0","0x0p+0"],"on_surface":1,"orient":false,"position":["0x1p-1","0x1p+0","0x0p+0"],"rest":"leading_face","target":"t"})", read), "");
    EXPECT_NE(editor::parse_surface_attachment_text("[]", read), "");
    EXPECT_NE(editor::parse_surface_attachment_text(R"({"direction":["0x0p+0","-0x1p+0","0x0p+0"],"on_surface":true,"orient":false,"position":["0x1p-1","0x1p+0","0x0p+0"],"rest":"leading_face","target":""})", read), "");
    EXPECT_NE(editor::parse_surface_attachment_text(R"({"direction":["0x0p+0","-0x1p+0","0x0p+0"],"on_surface":true,"orient":false,"position":[0.5,1.0,0.0],"rest":"leading_face","target":"t"})", read), "");
    EXPECT_NE(editor::parse_surface_attachment_text(R"({"direction":["0x0p+0","-0x1p+1","0x0p+0"],"on_surface":true,"orient":false,"position":["0x1p-1","0x1p+0","0x0p+0"],"rest":"leading_face","target":"t"})", read), "");
    EXPECT_NE(editor::parse_surface_attachment_text(R"({"direction":["0x0p+0","-0x1p+0"],"on_surface":true,"orient":false,"position":["0x1p-1","0x1p+0","0x0p+0"],"rest":"leading_face","target":"t"})", read), "");
    EXPECT_NE(editor::parse_surface_attachment_text(R"({"direction":["0x0p+0","-0x1p+0","0x0p+0"],"on_surface":true,"orient":0,"position":["0x1p-1","0x1p+0","0x0p+0"],"rest":"leading_face","target":"t"})", read), "");
    EXPECT_NE(editor::parse_surface_attachment_text(R"({"direction":["0x0p+0","-0x1p+0","0x0p+0"],"on_surface":true,"orient":false,"position":["0x1p-1","0x1p+0","0x0p+0"],"rest":"base","target":"t"})", read), "");
    EXPECT_NE(editor::parse_surface_attachment_text(R"({"direction":["0x0p+0","-0x1p+0","0x0p+0"],"extra":1,"on_surface":true,"orient":false,"position":["0x1p-1","0x1p+0","0x0p+0"],"rest":"leading_face","target":"t"})", read), "");
}

// The local value only; a record that does not read names its problem.
TEST(Kit_surface_attachment, record_reads_from_the_node)
{
    std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>("crate");
    editor::write_kit_piece(*node, editor::Kit_piece_data{.shape = "basic.block", .parameters = "{}"});
    EXPECT_FALSE(editor::read_kit_surface_attachment(*node).has_value());
    const Kit_surface_attachment record = make_record(c_table, c_crate, Surface_pose{.position = glm::vec3{0.1f, 1.0f, 0.2f}}, c_down);
    node->set_value(editor::Kit_piece::surface_attachment_property, editor::make_surface_attachment_text(record));
    EXPECT_EQ(editor::read_kit_surface_attachment(*node), std::optional<Kit_surface_attachment>{record});
    node->set_value(editor::Kit_piece::surface_attachment_property, std::string{"{}"});
    std::string problem;
    EXPECT_FALSE(editor::read_kit_surface_attachment(*node, &problem).has_value());
    EXPECT_NE(problem, "");
}

TEST(Kit_surface_attachment, position_is_the_rest_point_in_the_support_bounds)
{
    // A crate on the table top: the base center at (0.1, 1, 0.2) is 0.6, 1, 0.7 of the table's bounds.
    const Kit_surface_attachment record = make_record(c_table, c_crate, Surface_pose{.position = glm::vec3{0.1f, 1.0f, 0.2f}}, c_down);
    EXPECT_NEAR(record.position.x, 0.6, 1.0e-7);
    EXPECT_EQ  (record.position.y, 1.0);
    EXPECT_NEAR(record.position.z, 0.7, 1.0e-7);
    // A picture flush against the wall's front face (searching -Z), a quarter along and halfway up: its back face rests.
    const erhe::math::Aabb picture = make_bounds(glm::vec3{-0.3f, -0.2f, 0.0f}, glm::vec3{0.3f, 0.2f, 0.05f});
    const Kit_surface_attachment on_wall = make_record(c_wall, picture, Surface_pose{.position = glm::vec3{-1.0f, 1.5f, 0.125f}}, glm::vec3{0.0f, 0.0f, -1.0f});
    EXPECT_EQ(on_wall.position, (glm::dvec3{0.25, 0.5, 1.0}));
}

TEST(Kit_surface_attachment, carry_at_unchanged_bounds_gives_the_pose_back)
{
    const glm::quat yaw  = glm::angleAxis(0.7f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat tilt = glm::angleAxis(0.3f, glm::normalize(glm::vec3{1.0f, 0.0f, 0.4f})) * yaw;
    const glm::quat side = glm::angleAxis(glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f}); // a piece lying on its side
    const Surface_pose poses[]{
        Surface_pose{.position = glm::vec3{0.1f, 1.0f, 0.2f}},
        Surface_pose{.position = glm::vec3{0.0f, 1.0f, 0.0f}},
        Surface_pose{.position = glm::vec3{0.137f, 1.0f, -0.311f}, .rotation = yaw},
        Surface_pose{.position = glm::vec3{-0.2f, 1.03f, 0.05f}, .rotation = tilt, .scale = glm::vec3{1.5f, 0.75f, 2.0f}},
        Surface_pose{.position = glm::vec3{0.21f, 1.25f, -0.13f}, .rotation = side}
    };
    for (const Surface_pose& pose : poses) {
        for (const Surface_orient orient : {Surface_orient::off, Surface_orient::on}) {
            for (const Surface_snap_rest rest : {Surface_snap_rest::leading_face, Surface_snap_rest::anchor}) {
                const Kit_surface_attachment record = through_text(make_record(c_table, c_crate, pose, c_down, orient, rest));
                const glm::vec3 translation = carry(record, c_table, c_crate, pose);
                for (int axis = 0; axis < 3; ++axis) {
                    // Bit exact but for a translation component of 0, which may come back within 1e-12 m.
                    if (pose.position[axis] == 0.0f) {
                        EXPECT_NEAR(translation[axis], 0.0f, 1.0e-12f);
                    } else {
                        EXPECT_EQ(translation[axis], pose.position[axis]) << "axis " << axis;
                    }
                }
            }
        }
    }
}

TEST(Kit_surface_attachment, carry_keeps_the_proportion_when_the_support_grows)
{
    // S3.2: the picture a quarter along and halfway up stays so on a wall twice as long and 4 m tall.
    const erhe::math::Aabb picture = make_bounds(glm::vec3{-0.3f, -0.2f, 0.0f}, glm::vec3{0.3f, 0.2f, 0.05f});
    const glm::vec3 into_wall{0.0f, 0.0f, -1.0f};
    const Surface_pose pose{.position = glm::vec3{-1.0f, 1.5f, 0.125f}};
    const Kit_surface_attachment record = make_record(c_wall, picture, pose, into_wall);
    const erhe::math::Aabb longer = make_bounds(glm::vec3{-4.0f, 0.0f, -0.125f}, glm::vec3{4.0f, 4.0f, 0.125f});
    EXPECT_EQ(carry(record, longer, picture, pose), (glm::vec3{-2.0f, 2.0f, 0.125f}));
    // A crate on the table top stays on the top at any height (proportion 1 along Y).
    const Surface_pose on_table{.position = glm::vec3{0.1f, 1.0f, 0.2f}};
    const Kit_surface_attachment crate = make_record(c_table, c_crate, on_table, c_down);
    const erhe::math::Aabb taller = make_bounds(glm::vec3{-0.5f, 0.0f, -0.5f}, glm::vec3{0.5f, 1.75f, 0.5f});
    const glm::vec3 carried = carry(crate, taller, c_crate, on_table);
    EXPECT_EQ(carried.y, 1.75f);
    EXPECT_NEAR(carried.x, 0.1f, 1.0e-6f);
    EXPECT_NEAR(carried.z, 0.2f, 1.0e-6f);
}

TEST(Kit_surface_attachment, carry_of_a_turned_piece_moves_its_rest_point)
{
    // A crate turned 90 degrees about X lies on its side: its leading face down is its local +Z face, at z 0.25.
    const glm::quat side = glm::angleAxis(glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
    const glm::vec3 rest_point = editor::get_surface_rest_point(c_crate, side, glm::vec3{1.0f}, c_down, Surface_orient::off, Surface_snap_rest::leading_face);
    EXPECT_NEAR(rest_point.x, 0.0f,  1.0e-6f);
    EXPECT_NEAR(rest_point.y, 0.25f, 1.0e-6f);
    EXPECT_NEAR(rest_point.z, 0.25f, 1.0e-6f);
    const Surface_pose pose{.position = glm::vec3{0.0f, 1.25f, 0.0f}, .rotation = side};
    const Kit_surface_attachment record = make_record(c_table, c_crate, pose, c_down);
    EXPECT_NEAR(record.position.y, 1.0, 1.0e-6);
    // The table grows 0.5 m: the lying crate rises with its top, its local rotation kept.
    const erhe::math::Aabb taller = make_bounds(glm::vec3{-0.5f, 0.0f, -0.5f}, glm::vec3{0.5f, 1.5f, 0.5f});
    const glm::vec3 carried = carry(record, taller, c_crate, pose);
    EXPECT_NEAR(carried.y, 1.75f, 1.0e-6f);
}

TEST(Kit_surface_attachment, an_axis_without_extent_carries_the_bounds_value)
{
    // A flat support (no extent along Y): the record holds 0 there and the carry puts the rest point on the plane.
    const erhe::math::Aabb plane = make_bounds(glm::vec3{-1.0f, 0.5f, -1.0f}, glm::vec3{1.0f, 0.5f, 1.0f});
    const Surface_pose pose{.position = glm::vec3{0.3f, 0.5f, -0.4f}};
    const Kit_surface_attachment record = make_record(plane, c_crate, pose, c_down);
    EXPECT_EQ(record.position.y, 0.0);
    EXPECT_EQ(carry(record, plane, c_crate, pose), pose.position);
    const erhe::math::Aabb raised = make_bounds(glm::vec3{-2.0f, 0.9f, -2.0f}, glm::vec3{2.0f, 0.9f, 2.0f});
    const glm::vec3 carried = carry(record, raised, c_crate, pose);
    EXPECT_EQ(carried.y, 0.9f);
    EXPECT_NEAR(carried.x, 0.6f, 1.0e-6f);
    EXPECT_NEAR(carried.z, -0.8f, 1.0e-6f);
}

TEST(Kit_surface_attachment, invalid_bounds_give_no_place)
{
    const erhe::math::Aabb empty{};
    EXPECT_FALSE(editor::make_surface_attachment_position(empty, c_crate, Surface_pose{}, c_down, Surface_orient::off, Surface_snap_rest::leading_face).has_value());
    EXPECT_FALSE(editor::carry_surface_attachment(Kit_surface_attachment{}, empty, c_crate, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f}).has_value());
}

// S5.1: a record naming the parent mesh is an attachment, and the lock of a locked kit layout above ends at it.
TEST(Kit_surface_attachment, the_lock_of_a_locked_layout_ends_at_an_attached_piece)
{
    std::shared_ptr<erhe::scene::Xform> layout = std::make_shared<erhe::scene::Xform>("layout");
    editor::Kit_layout_data data;
    data.items.push_back(editor::Kit_layout_item{.shape = "basic.block", .parameters = "{}"});
    data.lock = editor::Kit_layout_lock::locked;
    editor::write_kit_layout(*layout, data);
    std::shared_ptr<erhe::scene::Mesh> element = std::make_shared<erhe::scene::Mesh>("element");
    element->set_parent(layout);
    std::shared_ptr<erhe::scene::Mesh> piece = std::make_shared<erhe::scene::Mesh>("piece");
    piece->set_parent(element);
    std::shared_ptr<erhe::scene::Xform> below = std::make_shared<erhe::scene::Xform>("below");
    below->set_parent(piece);
    EXPECT_EQ(editor::find_locking_kit_layout(*element), layout);
    EXPECT_EQ(editor::find_locking_kit_layout(*piece), layout);
    EXPECT_FALSE(editor::is_surface_attached_to_parent(*piece));

    Kit_surface_attachment record = make_record(c_table, c_crate, Surface_pose{.position = glm::vec3{0.1f, 1.0f, 0.2f}}, c_down);
    record.target = element->get_gltf_uid();
    piece->set_value(editor::Kit_piece::surface_attachment_property, editor::make_surface_attachment_text(record));
    EXPECT_TRUE(editor::is_surface_attached_to_parent(*piece));
    EXPECT_EQ(editor::find_locking_kit_layout(*piece), nullptr);
    EXPECT_EQ(editor::find_locking_kit_layout(*below), nullptr);
    EXPECT_EQ(editor::find_locking_kit_layout(*element), layout);
    EXPECT_FALSE(editor::locked_layout_child_refusal(*piece).has_value());
    EXPECT_TRUE(editor::locked_layout_child_refusal(*element).has_value());

    // A record naming another node, or a parent that is no mesh, is no attachment.
    record.target = "SomeOtherUid0001";
    piece->set_value(editor::Kit_piece::surface_attachment_property, editor::make_surface_attachment_text(record));
    EXPECT_FALSE(editor::is_surface_attached_to_parent(*piece));
    EXPECT_EQ(editor::find_locking_kit_layout(*piece), layout);
    record.target = layout->get_gltf_uid();
    std::shared_ptr<erhe::scene::Xform> on_layout = std::make_shared<erhe::scene::Xform>("on layout");
    on_layout->set_parent(layout);
    on_layout->set_value(editor::Kit_piece::surface_attachment_property, editor::make_surface_attachment_text(record));
    EXPECT_FALSE(editor::is_surface_attached_to_parent(*on_layout));
}
