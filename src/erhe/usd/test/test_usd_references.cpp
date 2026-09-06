#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace {

[[nodiscard]] auto reference_test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
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

class References_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = reference_test_data_path("references.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(References_import, load_succeeds)
{
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_FALSE(result.data.nodes.empty());
    EXPECT_EQ(result.data.default_prim, "World");
}

TEST_F(References_import, arcs_are_reported_in_authored_order)
{
    ASSERT_EQ(result.data.references.size(), 5u);

    const erhe::usd::Usd_prim_references& default_ref = result.data.references[0];
    EXPECT_EQ(default_ref.stage_path, "/World/DefaultRef");
    ASSERT_EQ(default_ref.references.size(), 1u);
    EXPECT_EQ(default_ref.references[0].asset_path, "cube.usda");
    EXPECT_TRUE(default_ref.references[0].prim_path.empty());
    EXPECT_EQ(default_ref.references[0].kind, erhe::usd::Usd_reference_kind::reference);

    const erhe::usd::Usd_prim_references& path_ref = result.data.references[1];
    EXPECT_EQ(path_ref.stage_path, "/World/PathRef");
    ASSERT_EQ(path_ref.references.size(), 1u);
    EXPECT_EQ(path_ref.references[0].asset_path, "reftarget.usda");
    EXPECT_EQ(path_ref.references[0].prim_path, "/Library/Gadget");

    const erhe::usd::Usd_prim_references& internal_ref = result.data.references[2];
    EXPECT_EQ(internal_ref.stage_path, "/World/InternalRef");
    ASSERT_EQ(internal_ref.references.size(), 1u);
    EXPECT_TRUE(internal_ref.references[0].asset_path.empty());
    EXPECT_EQ(internal_ref.references[0].prim_path, "/World/Source");

    // A list-edited `references` op: `prepend` before `append`.
    const erhe::usd::Usd_prim_references& multi_ref = result.data.references[3];
    EXPECT_EQ(multi_ref.stage_path, "/World/MultiRef");
    ASSERT_EQ(multi_ref.references.size(), 2u);
    EXPECT_EQ(multi_ref.references[0].prim_path, "/Library/Gadget");
    EXPECT_TRUE(multi_ref.references[1].prim_path.empty());
    EXPECT_EQ(multi_ref.references[1].asset_path, "reftarget.usda");

    // A payload is read as a reference.
    const erhe::usd::Usd_prim_references& payload_ref = result.data.references[4];
    EXPECT_EQ(payload_ref.stage_path, "/World/PayloadRef");
    ASSERT_EQ(payload_ref.references.size(), 1u);
    EXPECT_EQ(payload_ref.references[0].kind, erhe::usd::Usd_reference_kind::payload);
    EXPECT_EQ(payload_ref.references[0].asset_path, "reftarget.usda");
    EXPECT_EQ(payload_ref.references[0].prim_path, "/Library/Gadget");
}

TEST_F(References_import, carrier_prims_are_imported)
{
    for (const char* name : {"DefaultRef", "PathRef", "InternalRef", "MultiRef", "PayloadRef"}) {
        EXPECT_TRUE(find_node(result.data, name).operator bool()) << name;
    }
}

TEST_F(References_import, carriers_are_the_reported_items)
{
    for (const erhe::usd::Usd_prim_references& entry : result.data.references) {
        ASSERT_TRUE(entry.item.operator bool()) << entry.stage_path;
    }
    EXPECT_EQ(result.data.references[0].item, find_node(result.data, "DefaultRef"));
}

TEST_F(References_import, referenced_prims_are_not_imported)
{
    // The arcs' targets supply them: the caller instantiates each target under
    // the carrier prim.
    for (const char* name : {"cube", "cam", "sun", "bar", "plate", "Gadget"}) {
        EXPECT_FALSE(find_node(result.data, name).operator bool()) << name;
    }
    // The internal reference's target is authored in this layer and imports as
    // the ordinary prim it is.
    EXPECT_TRUE(find_node(result.data, "quad").operator bool());

    const std::shared_ptr<erhe::scene::Node> carrier = find_node(result.data, "DefaultRef");
    ASSERT_TRUE(carrier.operator bool());
    EXPECT_TRUE(carrier->get_children().empty());
}

TEST_F(References_import, carrier_keeps_its_own_transform)
{
    const std::shared_ptr<erhe::scene::Node> carrier = find_node(result.data, "DefaultRef");
    ASSERT_TRUE(carrier.operator bool());
    const glm::vec3 translation = glm::vec3{carrier->parent_from_node_transform().get_matrix()[3]};
    EXPECT_FLOAT_EQ(translation.x, 5.0f);
}

// The arc targets load as ordinary files: this is what the prefab library
// reads to build the template a carrier instantiates.
TEST(Reference_target_import, target_file_loads)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments arguments{
        .path          = reference_test_data_path("reftarget.usda"),
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result result = erhe::usd::load_usd(arguments);
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.data.default_prim, "Widget");
    EXPECT_TRUE(result.data.references.empty());
    EXPECT_TRUE(find_node(result.data, "Widget").operator bool());
    EXPECT_TRUE(find_node(result.data, "Gadget").operator bool());
    EXPECT_TRUE(find_node(result.data, "bar").operator bool());
}

} // anonymous namespace
