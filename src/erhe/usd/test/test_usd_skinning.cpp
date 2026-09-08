// UsdSkel skinning (doc/usd-compatibility-plan.md K1): a `Skeleton` prim is a
// transformable prim holding one `Xform` prim per joint, a `Mesh` with the
// `SkelBindingAPI` names a Skin whose inverse bind matrices are
// `inverse(bind_j) * geomBindTransform`, the skin primvars land on the
// vertices, and a `SkelAnimation` becomes joint channels of the file's
// animation.

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto find_prim(const std::shared_ptr<erhe::Hierarchy>& root, const std::string& name) -> std::shared_ptr<erhe::Hierarchy>
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : root->get_children()) {
        if (child->get_name() == name) {
            return child;
        }
        const std::shared_ptr<erhe::Hierarchy> found = find_prim(child, name);
        if (found) {
            return found;
        }
    }
    return {};
}

[[nodiscard]] auto prim_type_name(const std::shared_ptr<erhe::Hierarchy>& prim) -> std::string
{
    const erhe::Typed* typed = dynamic_cast<const erhe::Typed*>(prim.get());
    if (typed == nullptr) {
        return {};
    }
    return std::string{typed->get_prim_type_name()};
}

[[nodiscard]] auto load(const std::filesystem::path& path, const std::shared_ptr<erhe::scene::Node>& root) -> erhe::usd::Usd_load_result
{
    const erhe::usd::Usd_load_arguments arguments{
        .path          = path,
        .root_node     = root,
        .mesh_layer_id = 0
    };
    return erhe::usd::load_usd(arguments);
}

[[nodiscard]] auto geometry_of(const erhe::scene::Mesh& mesh) -> std::shared_ptr<erhe::geometry::Geometry>
{
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    if (primitives.size() != 1) {
        return {};
    }
    if (!primitives.front().primitive || !primitives.front().primitive->render_shape) {
        return {};
    }
    return primitives.front().primitive->render_shape->get_geometry_const();
}

void expect_matrix_near(const glm::mat4& value, const glm::mat4& expected, const char* what)
{
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(value[column][row], expected[column][row], 1e-5f)
                << what << " at [" << column << "][" << row << "]";
        }
    }
}

class Usd_skinning : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("skinning.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

// The `SkelRoot` stays the typeless-carrier `Typed` prim it is, and the
// `Skeleton` is a transformable prim keeping the authored `Skeleton` token.
TEST_F(Usd_skinning, skeleton_is_a_transformable_prim_keeping_its_token)
{
    const std::shared_ptr<erhe::Hierarchy> rig = find_prim(root, "rig");
    ASSERT_NE(rig, nullptr);
    EXPECT_EQ(prim_type_name(rig), "SkelRoot");
    EXPECT_FALSE(erhe::is<erhe::scene::Xformable>(rig.get()));

    const std::shared_ptr<erhe::Hierarchy> skeleton = find_prim(root, "skel");
    ASSERT_NE(skeleton, nullptr);
    EXPECT_EQ(prim_type_name(skeleton), "Skeleton");
    ASSERT_TRUE(erhe::is<erhe::scene::Xformable>(skeleton.get()));
}

// Each entry of `joints` is an `Xform` prim along the joint path, with the
// joint's `restTransforms` entry as its local transform.
TEST_F(Usd_skinning, joints_are_prims_along_the_joint_path_at_the_rest_pose)
{
    const std::shared_ptr<erhe::Hierarchy> skeleton = find_prim(root, "skel");
    ASSERT_NE(skeleton, nullptr);
    ASSERT_EQ(skeleton->get_children().size(), 1u);

    const std::shared_ptr<erhe::Hierarchy> joint_root = skeleton->get_children().front();
    EXPECT_EQ(joint_root->get_name(), "Root");
    ASSERT_TRUE(erhe::is<erhe::scene::Xform>(joint_root.get()));
    ASSERT_EQ(joint_root->get_children().size(), 1u);

    const std::shared_ptr<erhe::Hierarchy> joint_tip = joint_root->get_children().front();
    EXPECT_EQ(joint_tip->get_name(), "Tip");
    ASSERT_TRUE(erhe::is<erhe::scene::Xform>(joint_tip.get()));
    EXPECT_TRUE(joint_tip->get_children().empty());

    const erhe::scene::Node* root_node = static_cast<const erhe::scene::Node*>(joint_root.get());
    const erhe::scene::Node* tip_node  = static_cast<const erhe::scene::Node*>(joint_tip.get());
    expect_matrix_near(root_node->parent_from_node(), glm::mat4{1.0f}, "root joint local transform");
    // The rest pose puts the tip 2 up, where the bind pose puts it 4 up.
    glm::mat4 expected_tip{1.0f};
    expected_tip[3] = glm::vec4{0.0f, 2.0f, 0.0f, 1.0f};
    expect_matrix_near(tip_node->parent_from_node(), expected_tip, "tip joint local transform");
}

// One skin per (skeleton, geomBindTransform), its joints in `joints` order,
// its pivot the skeleton prim, and its inverse bind matrices
// `inverse(bind_j) * geomBindTransform`.
TEST_F(Usd_skinning, skin_carries_the_joints_and_the_inverse_bind_matrices)
{
    ASSERT_EQ(result.data.skins.size(), 1u);
    const std::shared_ptr<erhe::scene::Skin>& skin = result.data.skins.front();
    ASSERT_NE(skin, nullptr);

    const std::shared_ptr<erhe::Hierarchy> skeleton = find_prim(root, "skel");
    ASSERT_NE(skeleton, nullptr);
    EXPECT_EQ(skin->skin_data.skeleton.get(), static_cast<erhe::scene::Node*>(skeleton.get()));

    ASSERT_EQ(skin->skin_data.joints.size(), 2u);
    EXPECT_EQ(skin->skin_data.joints[0]->get_name(), "Root");
    EXPECT_EQ(skin->skin_data.joints[1]->get_name(), "Tip");

    // geomBindTransform is translate(0, 0, 1), bind_0 is the identity and
    // bind_1 is translate(0, 4, 0).
    ASSERT_EQ(skin->skin_data.inverse_bind_matrices.size(), 2u);
    glm::mat4 expected_root{1.0f};
    expected_root[3] = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
    glm::mat4 expected_tip{1.0f};
    expected_tip[3] = glm::vec4{0.0f, -4.0f, 1.0f, 1.0f};
    expect_matrix_near(skin->skin_data.inverse_bind_matrices[0], expected_root, "inverse bind matrix 0");
    expect_matrix_near(skin->skin_data.inverse_bind_matrices[1], expected_tip,  "inverse bind matrix 1");

    // The bound mesh names it.
    const std::shared_ptr<erhe::Hierarchy> body = find_prim(root, "body");
    ASSERT_NE(body, nullptr);
    ASSERT_TRUE(erhe::is<erhe::scene::Mesh>(body.get()));
    EXPECT_EQ(static_cast<const erhe::scene::Mesh*>(body.get())->skin, skin);
}

// The skin primvars land on the geometry vertices: four per set, the
// strongest first and the kept weights normalized.
TEST_F(Usd_skinning, joint_influences_land_on_the_vertices_normalized)
{
    const std::shared_ptr<erhe::Hierarchy> body = find_prim(root, "body");
    ASSERT_NE(body, nullptr);
    ASSERT_TRUE(erhe::is<erhe::scene::Mesh>(body.get()));
    const std::shared_ptr<erhe::geometry::Geometry> geometry = geometry_of(*static_cast<const erhe::scene::Mesh*>(body.get()));
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->get_mesh().vertices.nb(), 4u);

    erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();

    // Authored, per vertex: (joint, weight) pairs
    //   0: (0, 1) (1, 0)  ->  index 0, weight 1
    //   1: (0, 3) (1, 1)  ->  indices 0, 1, weights 0.75, 0.25
    //   2: (1, 3) (0, 1)  ->  indices 1, 0, weights 0.75, 0.25
    //   3: (0, 0) (1, 2)  ->  index 1, weight 1
    const std::uint32_t expected_indices[4][4] = {
        {0u, 0u, 0u, 0u},
        {0u, 1u, 0u, 0u},
        {1u, 0u, 0u, 0u},
        {1u, 0u, 0u, 0u}
    };
    const float expected_weights[4][4] = {
        {1.0f,  0.0f,  0.0f, 0.0f},
        {0.75f, 0.25f, 0.0f, 0.0f},
        {0.75f, 0.25f, 0.0f, 0.0f},
        {1.0f,  0.0f,  0.0f, 0.0f}
    };

    for (GEO::index_t vertex = 0; vertex < 4; ++vertex) {
        const std::optional<GEO::vec4u> indices = attributes.vertex_joint_indices(0).try_get(vertex);
        const std::optional<GEO::vec4f> weights = attributes.vertex_joint_weights(0).try_get(vertex);
        ASSERT_TRUE(indices.has_value()) << "vertex " << vertex;
        ASSERT_TRUE(weights.has_value()) << "vertex " << vertex;
        for (GEO::index_t i = 0; i < 4; ++i) {
            // A zero-weight slot names no joint, so only the index of a
            // weighted slot is asserted.
            if (expected_weights[vertex][i] > 0.0f) {
                EXPECT_EQ(indices.value()[i], expected_indices[vertex][i]) << "vertex " << vertex << " slot " << i;
            }
            EXPECT_NEAR(weights.value()[i], expected_weights[vertex][i], 1e-5f) << "vertex " << vertex << " slot " << i;
        }
        // elementSize is 2, so the second set is not written at all.
        EXPECT_FALSE(attributes.vertex_joint_indices(1).try_get(vertex).has_value()) << "vertex " << vertex;
    }
}

class Usd_skel_animation : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("skel_animation.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    [[nodiscard]] auto find_channel(
        const erhe::scene::Animation&      animation,
        const std::string&                 joint_name,
        const erhe::scene::Animation_path  path
    ) const -> const erhe::scene::Animation_channel*
    {
        for (const erhe::scene::Animation_channel& channel : animation.channels) {
            if (channel.target && (channel.target->get_name() == joint_name) && (channel.path == path)) {
                return &channel;
            }
        }
        return nullptr;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

// A `SkelAnimation` becomes channels of the file's one animation, targeting
// the joint prims and keyed in seconds.
TEST_F(Usd_skel_animation, joint_channels_target_the_joint_prims)
{
    ASSERT_EQ(result.data.animations.size(), 1u);
    const erhe::scene::Animation& animation = *result.data.animations.front().get();
    // Two joints, translation / rotation / scale each.
    EXPECT_EQ(animation.channels.size(), 6u);
    EXPECT_EQ(animation.samplers.size(), 6u);

    for (const char* const joint_name : {"Root", "Tip"}) {
        EXPECT_NE(find_channel(animation, joint_name, erhe::scene::Animation_path::TRANSLATION), nullptr) << joint_name;
        EXPECT_NE(find_channel(animation, joint_name, erhe::scene::Animation_path::ROTATION),    nullptr) << joint_name;
        EXPECT_NE(find_channel(animation, joint_name, erhe::scene::Animation_path::SCALE),       nullptr) << joint_name;
    }

    const std::shared_ptr<erhe::Hierarchy> skeleton = find_prim(root, "skel");
    ASSERT_NE(skeleton, nullptr);
    const erhe::scene::Animation_channel* tip = find_channel(animation, "Tip", erhe::scene::Animation_path::TRANSLATION);
    ASSERT_NE(tip, nullptr);
    // The channel drives the joint prim of the tree, not a copy of it.
    EXPECT_EQ(tip->target->get_parent().lock()->get_name(), "Root");
}

// The samples are the file's: keyed at time code 0 and 24 of a 24-per-second
// stage, so at 0 s and 1 s.
TEST_F(Usd_skel_animation, joint_samples_carry_the_authored_values)
{
    ASSERT_EQ(result.data.animations.size(), 1u);
    const erhe::scene::Animation& animation = *result.data.animations.front().get();

    const erhe::scene::Animation_channel* translation = find_channel(animation, "Tip", erhe::scene::Animation_path::TRANSLATION);
    ASSERT_NE(translation, nullptr);
    const erhe::scene::Animation_sampler& translation_sampler = animation.samplers[translation->sampler_index];
    ASSERT_EQ(translation_sampler.timestamps.size(), 2u);
    EXPECT_NEAR(translation_sampler.timestamps[0], 0.0f, 1e-5f);
    EXPECT_NEAR(translation_sampler.timestamps[1], 1.0f, 1e-5f);
    ASSERT_EQ(translation_sampler.data.size(), 6u);
    EXPECT_NEAR(translation_sampler.data[1], 2.0f, 1e-5f);
    EXPECT_NEAR(translation_sampler.data[4], 3.0f, 1e-5f);

    // A `quatf` sample is (x, y, z, w): the file authors (w, x, y, z) as
    // (0, 0, 0, 1), which is a half turn about z.
    const erhe::scene::Animation_channel* rotation = find_channel(animation, "Tip", erhe::scene::Animation_path::ROTATION);
    ASSERT_NE(rotation, nullptr);
    const erhe::scene::Animation_sampler& rotation_sampler = animation.samplers[rotation->sampler_index];
    ASSERT_EQ(rotation_sampler.data.size(), 8u);
    EXPECT_NEAR(rotation_sampler.data[3], 1.0f, 1e-5f); // w of the first key
    EXPECT_NEAR(rotation_sampler.data[6], 1.0f, 1e-5f); // z of the second key
    EXPECT_NEAR(rotation_sampler.data[7], 0.0f, 1e-5f); // w of the second key

    const erhe::scene::Animation_channel* scale = find_channel(animation, "Tip", erhe::scene::Animation_path::SCALE);
    ASSERT_NE(scale, nullptr);
    const erhe::scene::Animation_sampler& scale_sampler = animation.samplers[scale->sampler_index];
    ASSERT_EQ(scale_sampler.data.size(), 6u);
    EXPECT_NEAR(scale_sampler.data[0], 1.0f, 1e-5f);
    EXPECT_NEAR(scale_sampler.data[3], 2.0f, 1e-5f);
}

// A mesh with no `geomBindTransform` of its own binds through the identity,
// so its inverse bind matrices are the plain inverse bind transforms.
TEST_F(Usd_skel_animation, an_unauthored_geom_bind_transform_is_the_identity)
{
    ASSERT_EQ(result.data.skins.size(), 1u);
    const std::shared_ptr<erhe::scene::Skin>& skin = result.data.skins.front();
    ASSERT_EQ(skin->skin_data.inverse_bind_matrices.size(), 2u);
    glm::mat4 expected_tip{1.0f};
    expected_tip[3] = glm::vec4{0.0f, -4.0f, 0.0f, 1.0f};
    expect_matrix_near(skin->skin_data.inverse_bind_matrices[0], glm::mat4{1.0f}, "inverse bind matrix 0");
    expect_matrix_near(skin->skin_data.inverse_bind_matrices[1], expected_tip,    "inverse bind matrix 1");
}

} // anonymous namespace
