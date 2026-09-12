#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene/xform_op.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_arc_supplied_xform_op_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto find_node(const erhe::usd::Usd_data& data, const std::string& name) -> std::shared_ptr<erhe::scene::Node>
{
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        if (node && (node->get_name() == name)) {
            return node;
        }
    }
    return {};
}

[[nodiscard]] auto translation_of(const erhe::scene::Xform_op& op) -> glm::dvec3
{
    const glm::dmat4 matrix = op.to_matrix();
    return glm::dvec3{matrix[3]};
}

void expect_near(const glm::dvec3 value, const glm::dvec3 expected)
{
    EXPECT_NEAR(value.x, expected.x, 1e-9);
    EXPECT_NEAR(value.y, expected.y, 1e-9);
    EXPECT_NEAR(value.z, expected.z, 1e-9);
}

// `arc_supplied_xform_op.usda` is the shape `full_assets/Teapot/DrawModes.usd`
// authors (doc/usd-compatibility-plan.md C6): a prim whose
// `xformOpOrder` names an op its reference target authors and it does not.
class Arc_supplied_xform_op : public testing::Test
{
protected:
    void SetUp() override
    {
        source_path = test_data_path("arc_supplied_xform_op.usda");
        root        = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = source_path,
            .root_node     = root,
            .mesh_layer_id = 0
        };
        loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    }

    std::filesystem::path              source_path;
    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         loaded;
};

// The whole stack is readable: the op the order names and the prim does not
// author comes from the prim's reference target, in the place it is named.
TEST_F(Arc_supplied_xform_op, the_reference_target_supplies_the_missing_op)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "B");
    ASSERT_TRUE(node.operator bool());
    const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
    ASSERT_TRUE(stack != nullptr);
    ASSERT_EQ(stack->ops.size(), 2u);
    EXPECT_EQ(stack->ops[0].type, erhe::scene::Xform_op_type::transform);
    EXPECT_EQ(stack->ops[0].suffix, "");
    EXPECT_EQ(stack->ops[1].type, erhe::scene::Xform_op_type::transform);
    EXPECT_EQ(stack->ops[1].suffix, "dup");

    // The supplied op has the target's own value.
    const std::shared_ptr<erhe::scene::Node> target = find_node(loaded.data, "A");
    ASSERT_TRUE(target.operator bool());
    const erhe::scene::Xform_op_stack* target_stack = target->get_xform_op_stack();
    ASSERT_TRUE(target_stack != nullptr);
    ASSERT_EQ(target_stack->ops.size(), 1u);
    EXPECT_EQ(stack->ops[0].value, target_stack->ops[0].value);
    expect_near(translation_of(stack->ops[0]), glm::dvec3{2.0, 0.0, 0.0});
    expect_near(translation_of(stack->ops[1]), glm::dvec3{0.0, 3.0, 0.0});
}

// The prim's transform is what the two ops compose to, rather than the
// composed transform the stage evaluates for a carrier (the identity).
TEST_F(Arc_supplied_xform_op, the_prim_transform_is_the_whole_stack)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "B");
    ASSERT_TRUE(node.operator bool());
    const glm::mat4 local = node->parent_from_node();
    EXPECT_NEAR(local[3].x, 2.0f, 1e-5f);
    EXPECT_NEAR(local[3].y, 3.0f, 1e-5f);
    EXPECT_NEAR(local[3].z, 0.0f, 1e-5f);
}

// The search follows a chain of internal references: `C` references `B`,
// which does not author `xformOp:transform` either - `A`, which `B`
// references, does.
TEST_F(Arc_supplied_xform_op, the_search_follows_a_chain_of_arcs)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "C");
    ASSERT_TRUE(node.operator bool());
    const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
    ASSERT_TRUE(stack != nullptr);
    ASSERT_EQ(stack->ops.size(), 2u);
    EXPECT_EQ(stack->ops[1].suffix, "dup2");
    expect_near(translation_of(stack->ops[0]), glm::dvec3{2.0, 0.0, 0.0});

    const glm::mat4 local = node->parent_from_node();
    EXPECT_NEAR(local[3].x, 2.0f, 1e-5f);
    EXPECT_NEAR(local[3].y, 0.0f, 1e-5f);
    EXPECT_NEAR(local[3].z, 4.0f, 1e-5f);
}

// A save writes the resolved op as a local value of the carrier, which is
// composition-equivalent, so a reload of the written file is the same prim
// transform and a second save is a fixed point.
TEST_F(Arc_supplied_xform_op, a_round_trip_keeps_the_prim_transform)
{
    const std::filesystem::path         written_path = temporary_path("arc_supplied_xform_op.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = written_path,
        .root_node = root
    };
    const erhe::usd::Usd_save_result save_result = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save_result.error.empty()) << save_result.error;

    const std::shared_ptr<erhe::scene::Node> second_root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments      load_arguments{
        .path          = written_path,
        .root_node     = second_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    const std::shared_ptr<erhe::scene::Node> node = find_node(reloaded.data, "B");
    ASSERT_TRUE(node.operator bool());
    const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
    ASSERT_TRUE(stack != nullptr);
    ASSERT_EQ(stack->ops.size(), 2u);
    const glm::mat4 local = node->parent_from_node();
    EXPECT_NEAR(local[3].x, 2.0f, 1e-5f);
    EXPECT_NEAR(local[3].y, 3.0f, 1e-5f);
    EXPECT_NEAR(local[3].z, 0.0f, 1e-5f);
}

} // namespace
