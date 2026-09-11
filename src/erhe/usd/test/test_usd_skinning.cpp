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
#include "erhe_property/property_metadata.hpp"
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
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_skinning_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
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

// The export half (doc/usd-compatibility-plan.md K1): the `Skeleton` prim is
// written back from the joint prims and the skin, the skinned mesh carries
// the `SkelBindingAPI`, and reading either file back gives the same skin.

class Skinning_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("skinning.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("skinning.usda");
        erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = source_root
        };
        save_arguments.time_codes_per_second = source.data.time_codes.time_codes_per_second;
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        reloaded      = load(written_path, reloaded_root);
        ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    std::filesystem::path              written_path;
    erhe::usd::Usd_load_result         source;
    erhe::usd::Usd_load_result         reloaded;
};

// The skeleton is written as the `Skeleton` prim it was read as, with the
// joint paths and the two poses; the joints themselves are those arrays
// rather than prims of the stage.
TEST_F(Skinning_round_trip, the_skeleton_prim_carries_the_joint_arrays)
{
    const std::string written = read_file(written_path);
    EXPECT_NE(written.find("def SkelRoot \"rig\""), std::string::npos) << written;
    EXPECT_NE(written.find("def Skeleton \"skel\""), std::string::npos) << written;
    EXPECT_NE(written.find("uniform token[] joints = [\"Root\", \"Root/Tip\"]"), std::string::npos) << written;
    EXPECT_NE(written.find("bindTransforms"), std::string::npos) << written;
    EXPECT_NE(written.find("restTransforms"), std::string::npos) << written;
    EXPECT_EQ(written.find("def Xform \"Root\""), std::string::npos) << written;
    EXPECT_EQ(written.find("def Xform \"Tip\""), std::string::npos) << written;
}

// The skinned mesh binds its skeleton and applies the schema next to the
// relationship - usdchecker fails a prim that has one without the other -
// and carries the influences as `vertex` primvars of the width erhe uses.
TEST_F(Skinning_round_trip, the_skinned_mesh_carries_the_binding)
{
    const std::string written = read_file(written_path);
    EXPECT_NE(written.find("apiSchemas = [\"SkelBindingAPI\"]"), std::string::npos) << written;
    EXPECT_NE(written.find("rel skel:skeleton = </root/rig/skel>"), std::string::npos) << written;
    EXPECT_NE(written.find("int[] primvars:skel:jointIndices"), std::string::npos) << written;
    EXPECT_NE(written.find("float[] primvars:skel:jointWeights"), std::string::npos) << written;
    EXPECT_NE(written.find("elementSize = 2"), std::string::npos) << written;
    // erhe keeps only `inverse(bind_j) * geomBindTransform`, so the first
    // skin of a skeleton is written through the identity geometry bind
    // transform with the product folded into `bindTransforms`.
    EXPECT_EQ(written.find("geomBindTransform"), std::string::npos) << written;
}

// The skin the reload gives is the one the source file gave: the same joint
// prims in the same order and the same inverse bind matrices, which is what
// makes the two files pose the mesh alike.
TEST_F(Skinning_round_trip, the_skin_survives_the_round_trip)
{
    ASSERT_EQ(source.data.skins.size(), 1u);
    ASSERT_EQ(reloaded.data.skins.size(), 1u);
    const erhe::scene::Skin& before = *source.data.skins.front().get();
    const erhe::scene::Skin& after  = *reloaded.data.skins.front().get();

    ASSERT_EQ(after.skin_data.joints.size(), before.skin_data.joints.size());
    for (std::size_t joint_index = 0, end = before.skin_data.joints.size(); joint_index < end; ++joint_index) {
        EXPECT_EQ(after.skin_data.joints[joint_index]->get_name(), before.skin_data.joints[joint_index]->get_name());
        expect_matrix_near(
            after.skin_data.inverse_bind_matrices[joint_index],
            before.skin_data.inverse_bind_matrices[joint_index],
            "inverse bind matrix"
        );
    }

    const std::shared_ptr<erhe::Hierarchy> skeleton = find_prim(reloaded_root, "skel");
    ASSERT_NE(skeleton, nullptr);
    EXPECT_EQ(prim_type_name(skeleton), "Skeleton");
    EXPECT_EQ(after.skin_data.skeleton.get(), static_cast<erhe::scene::Node*>(skeleton.get()));

    const std::shared_ptr<erhe::Hierarchy> body = find_prim(reloaded_root, "body");
    ASSERT_NE(body, nullptr);
    ASSERT_TRUE(erhe::is<erhe::scene::Mesh>(body.get()));
    EXPECT_EQ(static_cast<const erhe::scene::Mesh*>(body.get())->skin, reloaded.data.skins.front());
}

// The influences the vertices carry are the ones the source file gave.
TEST_F(Skinning_round_trip, the_joint_influences_survive_the_round_trip)
{
    const std::shared_ptr<erhe::Hierarchy> before = find_prim(source_root,   "body");
    const std::shared_ptr<erhe::Hierarchy> after  = find_prim(reloaded_root, "body");
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after,  nullptr);
    const std::shared_ptr<erhe::geometry::Geometry> before_geometry = geometry_of(*static_cast<const erhe::scene::Mesh*>(before.get()));
    const std::shared_ptr<erhe::geometry::Geometry> after_geometry  = geometry_of(*static_cast<const erhe::scene::Mesh*>(after.get()));
    ASSERT_NE(before_geometry, nullptr);
    ASSERT_NE(after_geometry,  nullptr);
    ASSERT_EQ(after_geometry->get_mesh().vertices.nb(), before_geometry->get_mesh().vertices.nb());

    erhe::geometry::Mesh_attributes& before_attributes = before_geometry->get_attributes();
    erhe::geometry::Mesh_attributes& after_attributes  = after_geometry->get_attributes();
    for (GEO::index_t vertex = 0; vertex < before_geometry->get_mesh().vertices.nb(); ++vertex) {
        const std::optional<GEO::vec4u> before_indices = before_attributes.vertex_joint_indices(0).try_get(vertex);
        const std::optional<GEO::vec4u> after_indices  = after_attributes.vertex_joint_indices(0).try_get(vertex);
        const std::optional<GEO::vec4f> before_weights = before_attributes.vertex_joint_weights(0).try_get(vertex);
        const std::optional<GEO::vec4f> after_weights  = after_attributes.vertex_joint_weights(0).try_get(vertex);
        ASSERT_TRUE(before_indices.has_value() && after_indices.has_value()) << "vertex " << vertex;
        ASSERT_TRUE(before_weights.has_value() && after_weights.has_value()) << "vertex " << vertex;
        for (GEO::index_t slot = 0; slot < 4; ++slot) {
            EXPECT_NEAR(after_weights.value()[slot], before_weights.value()[slot], 1e-5f) << "vertex " << vertex << " slot " << slot;
            if (before_weights.value()[slot] > 0.0f) {
                EXPECT_EQ(after_indices.value()[slot], before_indices.value()[slot]) << "vertex " << vertex << " slot " << slot;
            }
        }
    }
}

// Writing what was just read back reaches a fixed point: the second file is
// the first one, byte for byte.
TEST_F(Skinning_round_trip, second_save_is_byte_identical)
{
    const std::filesystem::path second_path = temporary_path("skinning_second.usda");
    erhe::usd::Usd_save_arguments save_arguments{
        .path      = second_path,
        .root_node = reloaded_root
    };
    save_arguments.time_codes_per_second = reloaded.data.time_codes.time_codes_per_second;
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;
    EXPECT_EQ(read_file(second_path), read_file(written_path));
}

class Skel_animation_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        source      = load(test_data_path("skel_animation.usda"), source_root);
        ASSERT_TRUE(source.error.empty()) << source.error;

        written_path = temporary_path("skel_animation.usda");
        erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = source_root
        };
        save_arguments.animations            = source.data.animations;
        save_arguments.time_codes_per_second = source.data.time_codes.time_codes_per_second;
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        reloaded      = load(written_path, reloaded_root);
        ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    }

    [[nodiscard]] auto find_channel(
        const erhe::scene::Animation&     animation,
        const std::string&                joint_name,
        const erhe::scene::Animation_path path
    ) const -> const erhe::scene::Animation_channel*
    {
        for (const erhe::scene::Animation_channel& channel : animation.channels) {
            if (channel.target && (channel.target->get_name() == joint_name) && (channel.path == path)) {
                return &channel;
            }
        }
        return nullptr;
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    std::filesystem::path              written_path;
    erhe::usd::Usd_load_result         source;
    erhe::usd::Usd_load_result         reloaded;
};

// The joint channels are written as the skeleton's own `SkelAnimation` prim,
// which the skeleton names in `skel:animationSource`.
TEST_F(Skel_animation_round_trip, the_skeleton_names_its_animation)
{
    const std::string written = read_file(written_path);
    EXPECT_NE(written.find("def SkelAnimation \"anim\""), std::string::npos) << written;
    EXPECT_NE(written.find("rel skel:animationSource = </root/rig/skel/anim>"), std::string::npos) << written;
    EXPECT_NE(written.find("translations.timeSamples"), std::string::npos) << written;
    EXPECT_NE(written.find("rotations.timeSamples"), std::string::npos) << written;
    EXPECT_NE(written.find("scales.timeSamples"), std::string::npos) << written;
    // The layer's time coordinates are authored because samples were written.
    EXPECT_NE(written.find("timeCodesPerSecond = 24"), std::string::npos) << written;
    EXPECT_NE(written.find("endTimeCode = 24"), std::string::npos) << written;
}

// The reload gives the same joint channels, keyed at the same seconds with
// the same values.
TEST_F(Skel_animation_round_trip, the_joint_channels_survive_the_round_trip)
{
    ASSERT_EQ(source.data.animations.size(), 1u);
    ASSERT_EQ(reloaded.data.animations.size(), 1u);
    const erhe::scene::Animation& before = *source.data.animations.front().get();
    const erhe::scene::Animation& after  = *reloaded.data.animations.front().get();
    EXPECT_EQ(after.channels.size(), before.channels.size());

    for (const char* const joint_name : {"Root", "Tip"}) {
        for (const erhe::scene::Animation_path path : {
            erhe::scene::Animation_path::TRANSLATION,
            erhe::scene::Animation_path::ROTATION,
            erhe::scene::Animation_path::SCALE
        }) {
            const erhe::scene::Animation_channel* before_channel = find_channel(before, joint_name, path);
            const erhe::scene::Animation_channel* after_channel  = find_channel(after,  joint_name, path);
            ASSERT_NE(before_channel, nullptr) << joint_name;
            ASSERT_NE(after_channel,  nullptr) << joint_name;
            const erhe::scene::Animation_sampler& before_sampler = before.samplers[before_channel->sampler_index];
            const erhe::scene::Animation_sampler& after_sampler  = after.samplers[after_channel->sampler_index];
            ASSERT_EQ(after_sampler.timestamps.size(), before_sampler.timestamps.size()) << joint_name;
            ASSERT_EQ(after_sampler.data.size(),       before_sampler.data.size())       << joint_name;
            for (std::size_t key = 0; key < before_sampler.timestamps.size(); ++key) {
                EXPECT_NEAR(after_sampler.timestamps[key], before_sampler.timestamps[key], 1e-5f) << joint_name;
            }
            for (std::size_t value = 0; value < before_sampler.data.size(); ++value) {
                EXPECT_NEAR(after_sampler.data[value], before_sampler.data[value], 1e-3f) << joint_name << " value " << value;
            }
        }
    }
}

TEST_F(Skel_animation_round_trip, second_save_is_byte_identical)
{
    const std::filesystem::path second_path = temporary_path("skel_animation_second.usda");
    erhe::usd::Usd_save_arguments save_arguments{
        .path      = second_path,
        .root_node = reloaded_root
    };
    save_arguments.animations            = reloaded.data.animations;
    save_arguments.time_codes_per_second = reloaded.data.time_codes.time_codes_per_second;
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;
    EXPECT_EQ(read_file(second_path), read_file(written_path));
}

// `visibility` and `purpose` are GPrim attributes that the UsdSkel types
// carry copies of, so they are read from the concrete prim class: a
// `Skeleton` authoring `purpose = "guide"` and a `SkelRoot` authoring
// `visibility = "invisible"` land on the imported prims' own properties
// (usd-wg full_assets/ElephantWithMonochord authors the first on both of
// its skeletons).
class Usd_skel_visibility_and_purpose : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("skel_purpose.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Usd_skel_visibility_and_purpose, skeleton_purpose_is_local)
{
    const std::shared_ptr<erhe::Hierarchy> skel = find_prim(root, "skel");
    ASSERT_NE(skel, nullptr);
    EXPECT_EQ(skel->get_value_source(erhe::Item_base::purpose_property.get()), erhe::property::Value_source::local);
    EXPECT_EQ(skel->get_value(erhe::Item_base::purpose_property), erhe::Purpose::guide);
}

TEST_F(Usd_skel_visibility_and_purpose, skel_root_visibility_is_local)
{
    const std::shared_ptr<erhe::Hierarchy> rig = find_prim(root, "rig");
    ASSERT_NE(rig, nullptr);
    EXPECT_EQ(rig->get_value_source(erhe::Item_base::visible_property.get()), erhe::property::Value_source::local);
    EXPECT_FALSE(rig->get_value(erhe::Item_base::visible_property));
}

} // anonymous namespace
