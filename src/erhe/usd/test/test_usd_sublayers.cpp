#include "erhe_item/item.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene/xform_op.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_sublayer_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream     stream{path, std::ios::binary};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
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

[[nodiscard]] auto local_translation(const std::shared_ptr<erhe::scene::Node>& node) -> glm::vec3
{
    const glm::mat4 parent_from_node = node->parent_from_node();
    return glm::vec3{parent_from_node[3]};
}

[[nodiscard]] auto find_class(
    const std::vector<erhe::usd::Usd_class_prim>& classes,
    const std::string&                            stage_path
) -> const erhe::usd::Usd_class_prim*
{
    for (const erhe::usd::Usd_class_prim& class_prim : classes) {
        if (class_prim.stage_path == stage_path) {
            return &class_prim;
        }
        const erhe::usd::Usd_class_prim* found = find_class(class_prim.children, stage_path);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_value(const erhe::usd::Usd_class_prim& class_prim, const std::string& name) -> std::string
{
    for (const erhe::scene::Instance_override_value& value : class_prim.values) {
        if (value.name == name) {
            return value.text;
        }
    }
    return {};
}

[[nodiscard]] auto find_inherits(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path
) -> const erhe::usd::Usd_prim_inherits*
{
    for (const erhe::usd::Usd_prim_inherits& entry : data.prim_inherits) {
        if (entry.stage_path == stage_path) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_references(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path
) -> const erhe::usd::Usd_prim_references*
{
    for (const erhe::usd::Usd_prim_references& entry : data.references) {
        if (entry.stage_path == stage_path) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_variant_set(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path,
    const std::string&         set_name
) -> const erhe::usd::Usd_variant_set*
{
    for (const erhe::usd::Usd_variant_set& set : data.variant_sets) {
        if ((set.stage_path == stage_path) && (set.set_name == set_name)) {
            return &set;
        }
    }
    return nullptr;
}

// `sublayers.usda` lists two sublayers that disagree, and overrides one of
// their prims with an `over` of its own (doc/usd-compatibility-plan.md S1).
class Sublayers_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("sublayers.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Sublayers_import, every_sublayer_prim_is_in_the_composed_tree)
{
    // `World` and `Shared` and `RootOver` come from the stronger sublayer,
    // `WeakOnly` only from the weaker one, and the root layer authors no prim
    // of its own beyond the `over`.
    for (const char* name : {"World", "Shared", "RootOver", "WeakOnly"}) {
        EXPECT_TRUE(find_node(result.data, name).operator bool()) << name;
    }
}

TEST_F(Sublayers_import, the_stronger_sublayer_wins_over_the_weaker)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "Shared");
    ASSERT_TRUE(node.operator bool());
    EXPECT_FLOAT_EQ(local_translation(node).x, 1.0f);
}

TEST_F(Sublayers_import, the_root_layers_over_wins_over_every_sublayer)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "RootOver");
    ASSERT_TRUE(node.operator bool());
    EXPECT_FLOAT_EQ(local_translation(node).x, 9.0f);
}

TEST_F(Sublayers_import, a_prim_only_the_weaker_sublayer_authors_is_added_whole)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "WeakOnly");
    ASSERT_TRUE(node.operator bool());
    EXPECT_FLOAT_EQ(local_translation(node).x, 3.0f);
    // Its child mesh came with it.
    EXPECT_FALSE(result.data.meshes.empty());
}

TEST_F(Sublayers_import, the_sublayers_are_reported)
{
    ASSERT_EQ(result.data.sublayers.size(), 2u);
    EXPECT_EQ(result.data.sublayers[0], "./sublayer_strong.usda");
    EXPECT_EQ(result.data.sublayers[1], "./sublayer_weak.usda");
}

TEST_F(Sublayers_import, stage_metadata_the_root_leaves_unauthored_comes_from_the_strongest_sublayer)
{
    EXPECT_DOUBLE_EQ(result.data.meters_per_unit, 0.01);
    EXPECT_EQ(result.data.up_axis, "Y");
}

TEST(Sublayers_stage_metadata, the_root_layers_own_opinion_wins)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments      load_arguments{
        .path          = test_data_path("sublayers_meters.usda"),
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result result = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_DOUBLE_EQ(result.data.meters_per_unit, 1.0);
}

// A sublayer is reached the way a `references` asset path is, parent-relative
// segments included - the shape a stack whose shared layers sit beside the
// tree uses.
TEST(Sublayers_asset_paths, a_parent_relative_sublayer_resolves)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments      load_arguments{
        .path          = test_data_path("sublayer_stack/parent_relative.usda"),
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result result = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_TRUE(find_node(result.data, "Shared").operator bool());
    EXPECT_DOUBLE_EQ(result.data.meters_per_unit, 0.01);
}

// A USD-backed scene is edited as the one composed stage it became, so a save
// writes the composed content into one layer and authors no `subLayers`
// (src/erhe/usd/notes.md).
TEST_F(Sublayers_import, a_save_writes_one_layer_and_is_a_fixed_point)
{
    const std::filesystem::path         first_path = temporary_path("sublayers.usda");
    const erhe::usd::Usd_save_arguments save_arguments{
        .path      = first_path,
        .root_node = root
    };
    const erhe::usd::Usd_save_result save_result = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save_result.error.empty()) << save_result.error;

    const std::string written = read_file(first_path);
    EXPECT_EQ(written.find("subLayers"), std::string::npos);
    EXPECT_NE(written.find("\"WeakOnly\""), std::string::npos);
    EXPECT_NE(written.find("\"Shared\""), std::string::npos);

    const std::shared_ptr<erhe::scene::Node> second_root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments      reload_arguments{
        .path          = first_path,
        .root_node     = second_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(reload_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    EXPECT_TRUE(reloaded.data.sublayers.empty());

    const std::filesystem::path         second_path = temporary_path("sublayers_again.usda");
    const erhe::usd::Usd_save_arguments second_save_arguments{
        .path      = second_path,
        .root_node = second_root
    };
    const erhe::usd::Usd_save_result second_save_result = erhe::usd::save_usda(second_save_arguments);
    ASSERT_TRUE(second_save_result.error.empty()) << second_save_result.error;

    EXPECT_EQ(read_file(second_path), written);
}

// A sublayer's prims are prims of the composed layer, not just of the composed
// stage: `sublayer_authored.usda` authors nothing but one `over` and lets its
// sublayer author a `class` prim, an `inherits` arc, an `over` below a
// reference carrier, an `xformOp` stack, an `erhe:` custom attribute and a
// `variantSet` (doc/usd-compatibility-plan.md S1).
class Sublayer_authored_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("sublayer_authored.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    [[nodiscard]] auto find_mesh(const std::string& name) const -> std::shared_ptr<erhe::scene::Mesh>
    {
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : result.data.meshes) {
            if (mesh && (mesh->get_name() == name)) {
                return mesh;
            }
        }
        return {};
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Sublayer_authored_import, a_sublayers_class_prim_is_a_class_prim)
{
    const erhe::usd::Usd_class_prim* metal = find_class(result.data.classes, "/World/Styles/Metal");
    ASSERT_NE(metal, nullptr);
    EXPECT_EQ(metal->name, "Metal");
    EXPECT_EQ(find_value(*metal, "Mesh.shadow_cast"), "0"); // the USDA literal of a bool
}

TEST_F(Sublayer_authored_import, a_sublayers_inherits_arc_is_recorded)
{
    const erhe::usd::Usd_prim_inherits* entry = find_inherits(result.data, "/World/quad");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->inherits.size(), 1u);
    EXPECT_EQ(entry->inherits.front(), "/World/Styles/Metal");
}

TEST_F(Sublayer_authored_import, a_sublayers_over_below_a_carrier_is_an_override)
{
    const erhe::usd::Usd_prim_references* carrier = find_references(result.data, "/World/Instance");
    ASSERT_NE(carrier, nullptr);
    ASSERT_EQ(carrier->references.size(), 1u);
    ASSERT_EQ(carrier->overrides.size(), 1u);
    EXPECT_EQ(carrier->overrides.front().relative_path, "plate");
    ASSERT_EQ(carrier->overrides.front().values.size(), 1u);
    EXPECT_EQ(carrier->overrides.front().values.front().name, "Mesh.shadow_cast");
}

TEST_F(Sublayer_authored_import, a_sublayers_xform_op_stack_is_read_as_a_stack)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "Stacked");
    ASSERT_TRUE(node.operator bool());
    const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
    ASSERT_NE(stack, nullptr);
    ASSERT_EQ(stack->ops.size(), 2u);
    EXPECT_EQ(stack->ops[0].type, erhe::scene::Xform_op_type::translate);
    EXPECT_EQ(stack->ops[1].type, erhe::scene::Xform_op_type::rotate_xyz);
}

TEST_F(Sublayer_authored_import, a_sublayers_erhe_attribute_is_an_authored_local_value)
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = find_mesh("quad");
    ASSERT_TRUE(mesh.operator bool());
    EXPECT_TRUE(mesh->has_local_value(erhe::scene::Mesh::lightmapped_property.get()));
    EXPECT_TRUE(mesh->get_value(erhe::scene::Mesh::lightmapped_property));
}

TEST_F(Sublayer_authored_import, a_sublayers_variant_set_is_in_the_table)
{
    const erhe::usd::Usd_variant_set* set = find_variant_set(result.data, "/World/Swap", "shape");
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->selected, "first");
    ASSERT_EQ(set->variants.size(), 2u);
    for (const erhe::usd::Usd_variant& variant : set->variants) {
        EXPECT_EQ(variant.prims.size(), 1u) << variant.name;
    }
    // The hoisted prims are in the tree, one per variant, sibling-unique.
    EXPECT_TRUE(find_node(result.data, "tri").operator bool());
    EXPECT_TRUE(find_node(result.data, "tri_1").operator bool());
}

TEST_F(Sublayer_authored_import, a_root_layer_over_on_a_sublayer_def_is_one_prim)
{
    std::size_t shared_count = 0;
    for (const std::shared_ptr<erhe::scene::Node>& node : result.data.nodes) {
        if (node && (node->get_name() == "Shared")) {
            ++shared_count;
        }
    }
    EXPECT_EQ(shared_count, 1u);
    const std::shared_ptr<erhe::scene::Node> node = find_node(result.data, "Shared");
    ASSERT_TRUE(node.operator bool());
    EXPECT_FLOAT_EQ(local_translation(node).x, 9.0f);
}

} // anonymous namespace
